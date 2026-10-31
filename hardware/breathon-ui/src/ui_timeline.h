#ifndef BREATHON_UI_TIMELINE_H
#define BREATHON_UI_TIMELINE_H
#include <stdint.h>

typedef enum { UI_MOOD_IDLE, UI_MOOD_LISTEN, UI_MOOD_THINK, UI_MOOD_SPEAK } ui_mood_t;
typedef struct {
    int16_t float_y;
    uint8_t eye_height;
    uint8_t mouth_height;
    uint8_t glow_opa;
} ui_pose_t;

/* A pure frame sampler: milliseconds in, bounded geometry out. */
ui_pose_t ui_timeline_pose(uint32_t elapsed_ms, ui_mood_t mood);
#endif
