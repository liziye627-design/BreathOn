#include "ui_timeline.h"
#include <stddef.h>

typedef struct { uint16_t time; int16_t value; } keyframe_t;
typedef struct { const keyframe_t *keys; size_t count; } track_t;

static int sample(track_t track, uint32_t time)
{
    if (time <= track.keys[0].time) return track.keys[0].value;
    for (size_t i = 1; i < track.count; ++i) {
        const keyframe_t a = track.keys[i - 1], b = track.keys[i];
        if (time <= b.time) {
            int x = (int)(time - a.time) * 1024 / (b.time - a.time);
            int ease = x * x * (3072 - 2 * x) / 1048576;
            return a.value + (b.value - a.value) * ease / 1024;
        }
    }
    return track.keys[track.count - 1].value;
}

ui_pose_t ui_timeline_pose(uint32_t elapsed_ms, ui_mood_t mood)
{
    static const keyframe_t floating[] = {{0, 0}, {9000, 0}, {9200, -4}, {9480, 0}, {12000, 0}};
    static const keyframe_t blink[] = {{0, 100}, {5320, 100}, {5400, 0}, {5440, 0}, {5520, 100}, {6000, 100}};
    static const keyframe_t speech[] = {{0, 6}, {240, 14}, {480, 6}};
    int eye = mood == UI_MOOD_LISTEN ? 18 : mood == UI_MOOD_THINK ? 6 : 14;
    ui_pose_t pose = {
        .float_y = (int16_t)sample((track_t){floating, 5}, elapsed_ms % 12000),
        .eye_height = (uint8_t)(2 + (eye - 2) * sample((track_t){blink, 6}, elapsed_ms % 6000) / 100),
        .mouth_height = mood == UI_MOOD_SPEAK ? (uint8_t)sample((track_t){speech, 3}, elapsed_ms % 480) : 6,
        .glow_opa = 90
    };
    return pose;
}
