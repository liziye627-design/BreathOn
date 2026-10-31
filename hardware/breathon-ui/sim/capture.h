#ifndef BREATHON_CAPTURE_H
#define BREATHON_CAPTURE_H
#include "lvgl.h"
void sim_init(void);
void sim_advance(uint32_t ms);
void sim_press(int x, int y, bool pressed);
void sim_tap(int x, int y);
void sim_capture(const char *name);
void sim_verify(void);
lv_obj_t *sim_label(lv_obj_t *root, const char *text);
unsigned sim_timer_count(void);
uint32_t sim_frame_hash(void);
unsigned sim_flush_count(void);
void sim_audio_tests(void);
void sim_motion_tests(void);
void sim_voice_tests(void);
extern unsigned sim_checks;
#endif
