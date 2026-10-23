#include "input.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
bool zelda_is_movie(void) { return 0; }
int vibration_handles(uint32_t *out, int count, uint32_t player, uint32_t style) {
    (void)out; (void)count; (void)player; (void)style; return 0;
}
static ZeldaPadState sample(void) {
    ZeldaPadState result; assert(input_states(&result, 1, 0, 1)==1); return result;
}
int main(void) {
    input_clear();
    // A remains usable on the title screen without any camera or native UI.
    assert(!cameraIsGameplayActive());
    inputTouchButton(1, true); inputTouchButton(1, false);
    assert(sample().buttons & 1); assert(!(sample().buttons & 1));
    inputTouchController(1u<<9, .5f, -.5f, -.75f, .25f);
    ZeldaPadState state=sample();
    assert(inputGetDevice()==INPUT_DEVICE_TOUCH && (state.buttons&(1u<<9)));
    assert(state.lx>16000 && state.ly< -16000 && state.rx< -24000 && state.ry>8000);
    // Independent sources must not release one another's identical buttons.
    inputTouchButton(1u<<9, true); inputTouchController(0,0,0,0,0);
    assert(sample().buttons&(1u<<9)); inputTouchButton(1u<<9, false);
    assert(!(sample().buttons&(1u<<9)));
    // A short virtual controller pulse is consumed exactly once.
    inputTouchController(1u<<2, 0,0,0,0); inputTouchController(0,0,0,0,0);
    assert(sample().buttons&(1u<<2)); assert(!(sample().buttons&(1u<<2)));
    inputTouchController(0, NAN,INFINITY,.01f,.01f); state=sample();
    assert(!state.lx && !state.ly && !state.rx && !state.ry);
    inputTouchController(0xffff, 1,1,1,1); inputTouchControllerClear(); state=sample();
    assert(!state.buttons && !state.lx && !state.ly && !state.rx && !state.ry);
    // A real controller retains axis priority; clearing touch doesn't erase it.
    input_controller(0, .4f,.4f,.4f,.4f); inputTouchController(0,-1,-1,-1,-1);
    state=sample(); assert(state.lx>0 && state.ly>0 && state.rx>0 && state.ry>0);
    inputTouchControllerClear(); assert(sample().rx>0);
    input_clear(); state=sample(); assert(!state.buttons && !state.rx);
    puts("Touch input: title confirmation, four axes, pulses, independent sources, clearing and controller priority passed");
}
