#include "../src/ui_timeline.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Given a quiet companion, only the scheduled blink and hop move. */
    ui_pose_t closed = ui_timeline_pose(5400, UI_MOOD_IDLE);
    ui_pose_t open = ui_timeline_pose(0, UI_MOOD_IDLE);
    assert(closed.eye_height == 2 && open.eye_height == 14);
    assert(ui_timeline_pose(5520, UI_MOOD_IDLE).eye_height == 14);
    assert(ui_timeline_pose(11400, UI_MOOD_IDLE).eye_height == closed.eye_height);
    assert(ui_timeline_pose(9000, UI_MOOD_IDLE).float_y == 0);
    assert(ui_timeline_pose(9200, UI_MOOD_IDLE).float_y == -4);
    assert(ui_timeline_pose(9480, UI_MOOD_IDLE).float_y == 0);
    assert(ui_timeline_pose(12000, UI_MOOD_IDLE).float_y == open.float_y);
    /* Real business states choose poses, never manufacture audio levels. */
    assert(ui_timeline_pose(0, UI_MOOD_LISTEN).eye_height == 18);
    assert(ui_timeline_pose(0, UI_MOOD_THINK).eye_height == 6);
    assert(ui_timeline_pose(0, UI_MOOD_SPEAK).mouth_height == 6);
    assert(ui_timeline_pose(240, UI_MOOD_SPEAK).mouth_height == 14);
    assert(ui_timeline_pose(480, UI_MOOD_SPEAK).mouth_height == 6);
    for (unsigned t = 0; t < 24000; ++t) {
        ui_pose_t pose = ui_timeline_pose(t, UI_MOOD_SPEAK);
        assert(pose.float_y >= -4 && pose.float_y <= 0);
        if (t % 12000 < 9000 || t % 12000 >= 9480) assert(pose.float_y == 0);
        assert(pose.eye_height >= 2 && pose.eye_height <= 14);
        assert(pose.mouth_height >= 6 && pose.mouth_height <= 14);
        assert(pose.glow_opa == 90);
    }
    puts("PASS: frame-independent timing, blink, state poses, loop boundaries and bounded geometry");
    return 0;
}
