#include "breathon_ui.h"
#include "capture.h"
#include <assert.h>
#include <stdio.h>

static void qa_sink(breathon_audio_action_t action) { (void)action; }

static unsigned wave_geometry(lv_obj_t *object, int expected_max)
{
    unsigned count = 0;
    if (lv_obj_get_width(object) == 4) {
        int height = lv_obj_get_height(object);
        assert(height >= 2 && height <= expected_max);
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        if (area.x1 == 118) assert(height == expected_max);
        ++count;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) {
        count += wave_geometry(lv_obj_get_child(object, (int32_t)i), expected_max);
    }
    return count;
}

static void frames(const char *name)
{
    for (int i = 0; i < 5; ++i) {
        char path[80];
        snprintf(path, sizeof(path), "motion-%s-%03d", name, i * 160);
        sim_capture(path);
        if (i < 4) sim_advance(160);
    }
}

void sim_motion_tests(void)
{
    breathon_ui_show(BREATHON_DEVICE);
    unsigned base_timers = sim_timer_count();
    breathon_ui_set_voice("待命", "");
    breathon_ui_show(BREATHON_HOME);
    assert(sim_timer_count() == base_timers + (breathon_ui_page() == BREATHON_VOICE && !BREATHON_UI_REDUCED_MOTION));
    frames("home");
    sim_advance(80);
    unsigned before = sim_flush_count();
    uint32_t first_hash = sim_frame_hash();
    sim_advance(1000);
    unsigned home_frames = sim_flush_count() - before;
    assert(home_frames == 0 && first_hash == sim_frame_hash());
    assert(sim_label(lv_screen_active(), "语音陪伴"));
    breathon_ui_show(BREATHON_VOICE);
    sim_advance(9000);
    sim_capture("motion-hop-rest");
    first_hash = sim_frame_hash();
    sim_advance(200);
    sim_capture("motion-hop-mid");
    assert((first_hash == sim_frame_hash()) == !!BREATHON_UI_REDUCED_MOTION);
    sim_advance(360);
    sim_capture("motion-hop-landed");
    assert(first_hash == sim_frame_hash());
    breathon_ui_show(BREATHON_VOICE);
    assert(sim_timer_count() == base_timers + (breathon_ui_page() == BREATHON_VOICE && !BREATHON_UI_REDUCED_MOTION));
    breathon_ui_set_voice("QA 播放中", "");
    frames("voice");
    breathon_ui_set_voice("待命", "");
    breathon_ui_show(BREATHON_VOICE);
    sim_advance(80);
    before = sim_flush_count();
    first_hash = sim_frame_hash();
    sim_advance(1000);
    unsigned voice_frames = sim_flush_count() - before;
    assert(voice_frames == 0 && first_hash == sim_frame_hash());
    /* Capture the end-of-cycle blink, beyond the short phase-change sequence. */
    breathon_ui_show(BREATHON_VOICE);
    sim_advance(5320);
    sim_capture("motion-blink-5320");
    sim_advance(120);
    sim_capture("motion-blink-5440");
    sim_advance(160);
    sim_capture("motion-blink-5600");
    breathon_ui_show(BREATHON_AUDIO); /* Internal maintenance API only. */
    breathon_ui_set_audio_handler(qa_sink);
    breathon_audio_status_t status = {"QA 正在录音", 2500, 64, false, true};
    breathon_ui_set_audio(&status);
    frames("record");
    assert(wave_geometry(lv_screen_active(), 16) == 25);
    sim_advance(80);
    before = sim_flush_count();
    first_hash = sim_frame_hash();
    sim_advance(1000);
    unsigned record_frames = sim_flush_count() - before;
    if (BREATHON_UI_REDUCED_MOTION) {
        assert(record_frames == 0 && first_hash == sim_frame_hash());
    } else {
        assert(record_frames >= 24 && record_frames <= 26);
        assert(first_hash != sim_frame_hash());
    }
    assert(sim_label(lv_screen_active(), "2.5 秒"));
    assert(sim_label(lv_screen_active(), "电平 64%"));
    status.state = "QA 录音完成";
    status.busy = false;
    status.has_recording = true;
    status.level = 0;
    breathon_ui_set_audio(&status);
    sim_tap(176, 182);
    status.state = "QA 正在回放";
    status.busy = true;
    status.level = 48;
    breathon_ui_set_audio(&status);
    frames("play");

    const uint8_t levels[] = {0, 20, 50, 100};
    for (unsigned i = 0; i < sizeof(levels); ++i) {
        char name[80];
        status.state = "QA 正在录音";
        status.level = levels[i];
        breathon_ui_set_audio(&status);
        snprintf(name, sizeof(name), "motion-level-%03u", (unsigned)levels[i]);
        sim_capture(name);
        assert(wave_geometry(lv_screen_active(), 2 + 22 * levels[i] / 100) == 25);
    }
    status.level = 0;
    breathon_ui_set_audio(&status);
    sim_advance(80);
    first_hash = sim_frame_hash();
    before = sim_flush_count();
    sim_advance(1000);
    assert(sim_frame_hash() == first_hash && sim_flush_count() == before);
    status.level = 45;
    status.busy = false;
    status.state = "QA 待命";
    breathon_ui_set_audio(&status);
    /* Let LVGL's button enabled/disabled theme transitions settle first. */
    sim_advance(240);
    assert(lv_anim_count_running() == 0);
    first_hash = sim_frame_hash();
    sim_advance(1000);
    assert(sim_frame_hash() == first_hash); /* No fake idle amplitude. */

    breathon_ui_show(BREATHON_DEVICE);
    assert(sim_timer_count() == base_timers);
    sim_advance(80);
    first_hash = sim_frame_hash();
    sim_advance(1000);
    assert(first_hash == sim_frame_hash());
    sim_tap(22, 22);
    for (int i = 0; i < 50; ++i) {
        sim_tap(breathon_ui_page()==BREATHON_HOME ? 64 : 180,breathon_ui_page()==BREATHON_HOME ? 230 : 276);
        breathon_ui_show(BREATHON_AUDIO);
        status.busy = true;
        breathon_ui_set_audio(&status);
        sim_advance(80);
        sim_tap(120, 286);
        sim_tap(breathon_ui_page()==BREATHON_HOME ? 64 : 180,breathon_ui_page()==BREATHON_HOME ? 230 : 276);
        assert(breathon_ui_page() == BREATHON_HOME);
        assert(sim_timer_count() == base_timers + (breathon_ui_page() == BREATHON_VOICE && !BREATHON_UI_REDUCED_MOTION));
    }
    breathon_ui_show(BREATHON_DEVICE);
    assert(sim_timer_count() == base_timers);
    breathon_ui_set_audio_handler(NULL);
    printf("PASS: motion reduced=%d, home=%u/1000ms, voice=%u/1000ms, audio=%u/1000ms; level 0/20/50/100 exact bounded amplitude\n",
           BREATHON_UI_REDUCED_MOTION, home_frames, voice_frames, record_frames);
    puts("PASS: zero/idle pause, timer deletion, 50 mixed pointer/maintenance-API round-trips without timer growth");
    FILE *report = fopen("evidence/motion-report.json", "w");
    assert(report);
    fprintf(report, "{\"surface\":\"native LVGL 9.1\",\"viewport\":[240,320],"
            "\"clock\":\"deterministic LVGL ticks, not board FPS\",\"period_ms\":40,"
            "\"home_period_ms\":80,\"home_frames_per_1000ms\":%u,"
            "\"reduced_motion\":%s,\"voice_frames_per_1000ms\":%u,"
            "\"audio_frames_per_1000ms\":%u,\"baseline_timers\":%u,"
            "\"after_50_roundtrips_timers\":%u,\"level_geometry\":\"PASS\"}\n",
            home_frames, BREATHON_UI_REDUCED_MOTION ? "true" : "false", voice_frames, record_frames,
            base_timers, sim_timer_count());
    fclose(report);
}

