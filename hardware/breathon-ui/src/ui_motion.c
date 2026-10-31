#include "ui_internal.h"
#include <string.h>

/* One timer for the visible surface: home 12.5 Hz, voice/audio 25 Hz.
 * No allocation inside a frame. */
#define FRAME_MS 40
#define WAVE_BARS 25
static lv_timer_t *motion_timer;
static uint32_t started_ms;

static lv_obj_t *bars[WAVE_BARS];
static bool is_wave;
static bool quiet_orb;
static bool has_orb;
static uint8_t wave_level;
static uint32_t phase;

static ui_mood_t voice_mood;
static const uint32_t colors[] = {UI_LIGHT_CYAN, UI_LIGHT_BLUE, UI_ORANGE};

static void draw_wave(void)
{
    for (int i = 0; i < WAVE_BARS; ++i) {
        /* The center bar is an exact, phase-independent level reference.
         * All other bars stay within that amplitude; zero stays flat. */
        int distance = i > 12 ? i - 12 : 12 - i;
        int envelope = (12 - distance) * 100 / 12;
        int swell = 65 + (lv_trigo_sin((int)(phase + i * 24) % 360) + 32767) * 35 / 65534;
        int amplitude = i == 12 ? 22 * wave_level / 100 :
                        22 * wave_level * envelope * swell / 1000000;
        lv_obj_set_height(bars[i], 2 + amplitude);
        lv_obj_set_y(bars[i], 41 - amplitude / 2);
    }
}

static void draw_orb(void)
{
    ui_rotom_frame(lv_tick_elaps(started_ms), voice_mood);
}

void ui_motion_voice_state(const char *state)
{
    voice_mood = strstr(state, "聆听") ? UI_MOOD_LISTEN : strstr(state, "思考") ? UI_MOOD_THINK :
                 (strstr(state, "播放") || strstr(state, "说话")) ? UI_MOOD_SPEAK : UI_MOOD_IDLE;
    if (has_orb) draw_orb();
}
static void motion_tick(lv_timer_t *timer)
{
    (void)timer;
    phase = (phase + (is_wave ? 9 : quiet_orb ? 8 : 4)) % 360;
    if (is_wave) draw_wave();
    else draw_orb();
}

void ui_motion_cancel(void)
{
    has_orb = false;
    if (motion_timer) {
        lv_timer_delete(motion_timer);
        motion_timer = NULL;
    }
}

void ui_motion_orb(int y, bool quiet)
{
    has_orb = true;
    is_wave = false;
    quiet_orb = quiet;
    phase = 0;
    started_ms = lv_tick_get();
    lv_obj_t *canvas = ui_panel(ui_root, 60, y, 120, 112, UI_BG);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    ui_rotom_create(canvas);
    draw_orb();
    if (!BREATHON_UI_REDUCED_MOTION) motion_timer = lv_timer_create(motion_tick, quiet ? 80 : FRAME_MS, NULL);
}
void ui_motion_wave(lv_obj_t *parent)
{
    is_wave = true;
    phase = 0;
    for (int i = 0; i < WAVE_BARS; ++i) {
        bars[i] = ui_panel(parent, 10 + i * 8, 41, 4, 2, UI_PALE);
        lv_obj_set_style_radius(bars[i], 2, 0);
        lv_obj_remove_flag(bars[i], LV_OBJ_FLAG_CLICKABLE);
    }
    if (!BREATHON_UI_REDUCED_MOTION) {
        motion_timer = lv_timer_create(motion_tick, FRAME_MS, NULL);
        lv_timer_pause(motion_timer);
    }
}

void ui_motion_level(uint8_t level, bool busy, bool available, breathon_audio_action_t action)
{
    wave_level = available ? level : 0;
    int offset = action == BREATHON_AUDIO_PLAY ? 1 : action == BREATHON_AUDIO_TONE ? 2 : 0;
    for (int i = 0; i < WAVE_BARS; ++i) {
        int segment = i <= 12 ? 0 : 1;
        lv_color_t from = lv_color_hex(colors[(segment + offset) % 3]);
        lv_color_t to = lv_color_hex(colors[(segment + offset + 1) % 3]);
        lv_color_t color = lv_color_mix(to, from, (uint8_t)((i - segment * 12) * 255 / 12));
        lv_obj_set_style_bg_color(bars[i], available ? color : lv_color_hex(UI_PALE), 0);
    }
    draw_wave();
    if (motion_timer) {
        if (available && busy && level > 0) lv_timer_resume(motion_timer);
        else lv_timer_pause(motion_timer);
    }
}
