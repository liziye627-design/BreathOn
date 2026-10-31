#include "app_breathon_realtime_internal.h"
#include "ai_audio_input.h"
#include "cJSON.h"

static const char *string_field(const cJSON *json, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}
static void publish_text(const char *state, const char *text)
{
    char bounded[513];
    size_t n = rt_utf8_prefix(text, sizeof(bounded) - 1);
    memcpy(bounded, text, n); bounded[n] = 0;
    app_breathon_realtime_publish(state, bounded);
}
static bool bounded_json(const uint8_t *data, size_t n)
{
    unsigned depth = 0, tokens = 0; bool quoted = false, escape = false;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = data[i];
        if (!c) return false;
        if (quoted) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 2) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
        else if ((c == ':' || c == ',') && ++tokens > 32) return false;
    }
    return !quoted && !depth;
}
static void cancel_player(rt_session_t *s)
{
    uint32_t irq = tal_system_enter_critical();
    s->generation.open = false; rt_ring_clear(&s->speaker);
    tal_system_exit_critical(irq);
    /* Stop is independent of the feed worker. The SDK memory sink survives stop;
     * an in-flight feed may return, but cannot play or become a newer response.
     * The playback worker rechecks generation after every SDK call. */
    ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
}
static bool control(rt_session_t *s, const rt_header_t *h, const cJSON *json)
{
    const char *type = string_field(json, "type");
    if (!type) return false;
    if (!strcmp(type, "closed")) { s->closed = true; return false; }
    if (!strcmp(type, "error")) { rt_fail(s, "网关会话失败"); return false; }
    if (!strcmp(type, "pong")) return true;
    if (!strcmp(type, "ready")) {
        if (s->ready || h->generation != 0) return false;
        if (ai_audio_input_start() != OPRT_OK) { rt_fail(s, "麦克风启动失败"); return false; }
        uint32_t irq = tal_system_enter_critical();
        s->ready = s->capture_on = true; rt_ring_clear(&s->mic);
        tal_system_exit_critical(irq);
        app_breathon_realtime_publish("listening", "聆听中");
        return true;
    }
    if (!s->ready) return false;
    if (!strcmp(type, "audio_start")) {
        if (!h->generation) return false;
        if (h->generation <= s->generation.latest) return true;
        cancel_player(s);
        uint32_t irq = tal_system_enter_critical();
        rt_generation_start(&s->generation, h->generation);
        tal_system_exit_critical(irq);
        app_breathon_realtime_publish("speaking", "正在回复");
        return true;
    }
    if (!strcmp(type, "interrupt")) {
        if (!h->generation || h->generation > s->generation.latest) return false;
        if (h->generation == s->generation.latest) cancel_player(s);
        return true;
    }
    if (!strcmp(type, "audio_end")) {
        if (!h->generation || h->generation > s->generation.latest) return false;
        uint32_t irq = tal_system_enter_critical();
        rt_generation_end(&s->generation, h->generation);
        tal_system_exit_critical(irq);
        return true;
    }
    if (!strcmp(type, "state")) {
        const char *state = string_field(json, "state");
        if (!state) return false;
        if (h->generation && h->generation < s->generation.latest) return true;
        if (!strcmp(state, "stopped")) { s->closed = true; return false; }
        if (!strcmp(state, "listening")) app_breathon_realtime_publish(state, "聆听中");
        else if (!strcmp(state, "thinking")) app_breathon_realtime_publish(state, "正在思考");
        else if (!strcmp(state, "speaking")) {
            if (s->generation.open) app_breathon_realtime_publish(state, "正在回复");
        } else return false;
        return true;
    }
    if (!strcmp(type, "transcript")) {
        const char *role = string_field(json, "role"), *text = string_field(json, "text");
        if (!role || !text) return false;
        if (!strcmp(role, "user")) publish_text("transcript_user", text);
        else if (!strcmp(role, "assistant")) {
            if (!h->generation || (h->generation == s->generation.latest && s->generation.open))
                publish_text("transcript_assistant", text);
        } else return false;
        return true;
    }
    return false;
}
bool rt_receive_frame(void *arg, const rt_header_t *h, const uint8_t *data)
{
    rt_session_t *s = arg;
    if (rt_stop_requested()) return false;
    if (h->kind == RT_SPEAKER) {
        if (!s->ready || !h->generation || h->generation > s->generation.latest) return false;
        uint32_t irq = tal_system_enter_critical();
        bool ok = !rt_generation_accept(&s->generation, h->generation) ||
                  rt_ring_write(&s->speaker, data, h->length);
        if (!ok) s->failure = "播放队列溢出";
        tal_system_exit_critical(irq);
        return ok;
    }
    if (h->kind != RT_CONTROL || !rt_utf8_valid(data, h->length) || !bounded_json(data, h->length)) return false;
    cJSON *json = cJSON_ParseWithOpts((const char *)data, NULL, true);
    if (!json) return false;
    bool ok = cJSON_IsObject(json) && control(s, h, json);
    cJSON_Delete(json);
    return ok;
}
