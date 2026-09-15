#include <assert.h>
#include <string.h>
#include "did_tibo_rest.h"
int main(void) {
    tibo_rest_t rest; tibo_rest_snapshot_t snapshot; tibo_rest_init(&rest, 1, 30000);
    assert(!tibo_rest_apply_push(&rest, "{\"type\":\"other\"}", 0));
    assert(tibo_rest_apply_push(&rest, "{\"type\":\"tibo.push\",\"title\":\"Review\",\"body\":\"Approve changes\"}", 100));
    tibo_rest_snapshot(&rest, &snapshot); assert(snapshot.state == TIBO_REST_PENDING && tibo_rest_take_beep(&rest));
    assert(strcmp(snapshot.title, "Review") == 0); tibo_rest_tick(&rest, 30100); assert(tibo_rest_take_beep(&rest));
    tibo_rest_mute(&rest); tibo_rest_tick(&rest, 90000); assert(!tibo_rest_take_beep(&rest));
    tibo_rest_cycle_level(&rest); tibo_rest_snapshot(&rest, &snapshot); assert(snapshot.level == 2); return 0;
}
