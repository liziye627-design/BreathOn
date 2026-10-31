#include "session_core.h"

#include <stddef.h>

static bool is_active(session_state_t state)
{
    return state >= SESSION_WARMUP && state <= SESSION_PURGING;
}

static session_result_t result_for(const session_core_t *core,
                                   session_status_t status)
{
    session_result_t result = {0};
    result.status = status;
    if (core != NULL) {
        result.previous_state = core->record.state;
        result.record = core->record;
    }
    return result;
}

static session_result_t enter(session_core_t *core, session_state_t state,
                               session_reason_t reason, uint32_t now_ms)
{
    static const uint32_t intents[SESSION_STATE_COUNT] = {
        [SESSION_WARMUP] = SESSION_ACTION_WARMUP,
        [SESSION_BASELINE] = SESSION_ACTION_BASELINE,
        [SESSION_READY] = SESSION_ACTION_AWAIT_BLOW,
        [SESSION_BLOWING] = SESSION_ACTION_CAPTURE,
        [SESSION_MEASURING] = SESSION_ACTION_MEASURE,
        [SESSION_PURGING] = SESSION_ACTION_PURGE,
        [SESSION_COMPLETED] = SESSION_ACTION_COMPLETED
    };
    session_result_t result = result_for(core, SESSION_ACCEPTED);
    core->record.state = state;
    core->record.reason = reason;
    core->record.state_entered_at_ms = now_ms;
    core->record.updated_at_ms = now_ms;
    core->record.ended_at_ms = state >= SESSION_COMPLETED ? now_ms : 0;
    result.record = core->record;
    result.actions = SESSION_ACTION_STOP | intents[state];
    return result;
}

static session_result_t start(session_core_t *core, session_event_t event,
                               uint32_t now_ms)
{
    session_source_t source = core->record.source;
    const bool retry = event == SESSION_RETRY;
    if (retry) {
        if ((core->record.state != SESSION_INVALID && core->record.state != SESSION_ERROR) ||
            source == SESSION_SOURCE_NONE) {
            return result_for(core, SESSION_ILLEGAL_EVENT);
        }
    } else {
        if (core->record.state != SESSION_IDLE) {
            return result_for(core, SESSION_ILLEGAL_EVENT);
        }
        source = event == SESSION_START_AUDIO_TEST ? SESSION_SOURCE_AUDIO_TEST : SESSION_SOURCE_SENSOR;
    }
    if (core->session_counter == UINT32_MAX) {
        return result_for(core, SESSION_ID_EXHAUSTED);
    }
    core->record.session_id = ++core->session_counter;
    core->record.source = source;
    core->record.started_at_ms = now_ms;
    return enter(core, SESSION_WARMUP, retry ? SESSION_REASON_RETRY : SESSION_REASON_NONE, now_ms);
}

bool session_core_init(session_core_t *core, const session_config_t *config,
                       uint32_t now_ms)
{
    if (core == NULL || config == NULL) { return false; }
    for (unsigned int i = 0; i < SESSION_STATE_COUNT; ++i) {
        const uint32_t timeout = config->timeout_ms[i];
        if (is_active((session_state_t)i)) {
            if (timeout == 0 || timeout > (uint32_t)INT32_MAX) { return false; }
        } else if (timeout != 0) {
            return false;
        }
    }
    *core = (session_core_t){
        .config = *config,
        .record = {.state = SESSION_IDLE, .source = SESSION_SOURCE_NONE,
                   .state_entered_at_ms = now_ms, .updated_at_ms = now_ms}
    };
    return true;
}

session_result_t session_core_step(session_core_t *core, session_event_t event,
                                   uint32_t session_id, uint32_t now_ms)
{
    static const struct {
        session_state_t from;
        session_event_t event;
        session_state_t to;
    } edges[] = {
        {SESSION_WARMUP, SESSION_WARMUP_DONE, SESSION_BASELINE},
        {SESSION_BASELINE, SESSION_BASELINE_DONE, SESSION_READY},
        {SESSION_READY, SESSION_AUDIO_ACTIVITY, SESSION_BLOWING},
        {SESSION_READY, SESSION_BLOW_STARTED, SESSION_BLOWING},
        {SESSION_BLOWING, SESSION_BLOW_FINISHED, SESSION_MEASURING},
        {SESSION_MEASURING, SESSION_SENSOR_CAPTURE_DONE, SESSION_PURGING},
        {SESSION_PURGING, SESSION_PURGE_DONE, SESSION_COMPLETED}
    };
    if (core == NULL || (unsigned int)event >= SESSION_EVENT_COUNT) {
        return result_for(core, SESSION_BAD_ARGUMENT);
    }
    if (session_id != core->record.session_id) {
        return result_for(core, SESSION_STALE_SESSION);
    }
    if ((uint32_t)(now_ms - core->record.updated_at_ms) > (uint32_t)INT32_MAX) {
        return result_for(core, SESSION_INVALID_TIME);
    }

    const session_state_t state = core->record.state;
    session_reason_t reason = SESSION_REASON_NONE;
    /* Explicit aborts precede timeout so their causes remain observable. */
    switch (event) {
    case SESSION_RESET:
        core->record.session_id = 0;
        core->record.source = SESSION_SOURCE_NONE;
        core->record.started_at_ms = 0;
        return enter(core, SESSION_IDLE, SESSION_REASON_RESET, now_ms);
    case SESSION_CANCEL: reason = SESSION_REASON_CANCELLED; break;
    case SESSION_INTERRUPT: reason = SESSION_REASON_INTERRUPTED; break;
    case SESSION_SAMPLE_INVALID: reason = SESSION_REASON_SAMPLE_INVALID; break;
    case SESSION_HARDWARE_ERROR: reason = SESSION_REASON_HARDWARE_FAULT; break;
    default: break;
    }
    if (reason != SESSION_REASON_NONE) {
        const bool fault = event == SESSION_HARDWARE_ERROR;
        if (is_active(state) || (fault && state == SESSION_IDLE)) {
            return enter(core, fault ? SESSION_ERROR : SESSION_INVALID, reason, now_ms);
        }
        return result_for(core, SESSION_ILLEGAL_EVENT);
    }
    if (is_active(state) &&
        (uint32_t)(now_ms - core->record.state_entered_at_ms) >= core->config.timeout_ms[state]) {
        const session_state_t terminal = state == SESSION_READY || state == SESSION_BLOWING ?
            SESSION_INVALID : SESSION_ERROR;
        return enter(core, terminal, SESSION_REASON_TIMEOUT, now_ms);
    }
    if (event == SESSION_START_AUDIO_TEST || event == SESSION_START_SENSOR || event == SESSION_RETRY) {
        return start(core, event, now_ms);
    }
    if (event == SESSION_TICK || (event == SESSION_AUDIO_ACTIVITY && state == SESSION_BLOWING)) {
        core->record.updated_at_ms = now_ms;
        return result_for(core, SESSION_ACCEPTED);
    }
    if (event == SESSION_BLOW_FINISHED && state == SESSION_BLOWING &&
        core->record.source == SESSION_SOURCE_AUDIO_TEST) {
        return enter(core, SESSION_INVALID, SESSION_REASON_AUDIO_ONLY, now_ms);
    }
    for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        if (edges[i].from == state && edges[i].event == event) {
            return enter(core, edges[i].to, edges[i].to == SESSION_COMPLETED ?
                         SESSION_REASON_COMPLETED : SESSION_REASON_NONE, now_ms);
        }
    }
    return result_for(core, SESSION_ILLEGAL_EVENT);
}
