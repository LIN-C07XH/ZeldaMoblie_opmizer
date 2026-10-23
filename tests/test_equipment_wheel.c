#include "equipment_wheel.h"
#include <assert.h>
#include <stdio.h>

static void sample(EquipmentWheel *wheel, double now, bool active, uint64_t buttons, int32_t rx) {
    uint64_t actualButtons; int32_t actualRx;
    assert(equipmentWheelPoll(wheel, now, &actualButtons, &actualRx) == active);
    assert(actualButtons == buttons && actualRx == rx);
}
int main(void) {
    EquipmentWheel w = {0};
    const uint64_t shield = 1ull << 12, weapon = 1ull << 14;
    assert(!equipmentWheelScroll(&w, 1, 0, false)); // title/cutscene cannot open
    sample(&w, 0, false, 0, 0);
    assert(equipmentWheelScroll(&w, 1, 1, true));
    sample(&w, 1, true, shield, 0);
    sample(&w, 1.19, true, shield, 0);
    sample(&w, 1.201, true, shield, -32767);
    // Repeated GetNpadStates calls must not consume the next queued notch.
    sample(&w, 1.201, true, shield, -32767);
    sample(&w, 1.262, true, shield, 0);
    // Reverse scrolling navigates the same category while its menu owns input.
    assert(equipmentWheelScroll(&w, -1, 1.28, false));
    sample(&w, 1.323, true, shield, 32767);
    sample(&w, 1.384, true, shield, 0);
    sample(&w, 1.61, true, 0, 0); // release to equip after idle
    assert(!equipmentWheelScroll(&w, -1, 1.62, true));
    sample(&w, 1.70, true, 0, 0);
    sample(&w, 1.71, false, 0, 0);
    assert(equipmentWheelScroll(&w, -1, 2, true));
    sample(&w, 2, true, weapon, 0);
    sample(&w, 2.201, true, weapon, 32767);
    equipmentWheelReset(&w); // ESC/focus loss/another input releases immediately
    sample(&w, 2.21, false, 0, 0);
    // Both 30 Hz and 120 Hz polling drain one notch with a neutral interval.
    for (int hz = 30; hz <= 120; hz += 90) {
        assert(equipmentWheelScroll(&w, -1, 3, true));
        int edges = 0; int32_t previous = 0;
        for (int n = 0; n < hz; n++) {
            uint64_t buttons; int32_t rx;
            equipmentWheelPoll(&w, 3 + (double)n / hz, &buttons, &rx);
            if (rx && !previous) edges++;
            previous = rx;
        }
        assert(edges == 1 && w.phase == WHEEL_IDLE);
    }
    puts("Equipment wheel: category latch, reversal, gating, timing and cancellation passed");
}
