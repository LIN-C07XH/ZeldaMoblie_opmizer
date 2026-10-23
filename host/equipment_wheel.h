#pragma once
#include <stdbool.h>
#include <stdint.h>

// One gesture owns one native quick-select menu. All timing is in seconds,
// independent of the number of GetNpadStates calls or the configured FPS.
typedef enum { WHEEL_IDLE, WHEEL_OPEN, WHEEL_STEP, WHEEL_NEUTRAL, WHEEL_RELEASE } EquipmentWheelPhase;
typedef struct {
    EquipmentWheelPhase phase;
    uint64_t button;
    int queued, direction;
    double phaseAt, lastScroll;
} EquipmentWheel;

static inline void equipmentWheelReset(EquipmentWheel *wheel) { *wheel = (EquipmentWheel){0}; }

static inline bool equipmentWheelScroll(EquipmentWheel *wheel, int direction, double now, bool mayOpen) {
    if (!direction) return false;
    if (wheel->phase == WHEEL_RELEASE) return false; // ensure a released sample between gestures
    if (wheel->phase == WHEEL_IDLE) {
        if (!mayOpen) return false;
        wheel->phase = WHEEL_OPEN;
        wheel->phaseAt = now;
        // Up opens shields (D-left), down opens weapons (D-right).
        wheel->button = 1ull << (direction > 0 ? 12 : 14);
    }
    int step = direction > 0 ? -1 : 1;
    if (wheel->queued + step >= -24 && wheel->queued + step <= 24) wheel->queued += step;
    wheel->lastScroll = now;
    return true;
}

static inline bool equipmentWheelPoll(EquipmentWheel *wheel, double now, uint64_t *buttons, int32_t *rx) {
    *buttons = 0; *rx = 0;
    if (wheel->phase == WHEEL_IDLE) return false;
    if (wheel->phase == WHEEL_RELEASE) {
        if (now - wheel->phaseAt >= 0.08) equipmentWheelReset(wheel);
        return true;
    }
    *buttons = wheel->button;
    if (wheel->phase == WHEEL_OPEN && now - wheel->phaseAt < 0.20) return true;
    if (wheel->phase == WHEEL_STEP) {
        if (now - wheel->phaseAt < 0.06) { *rx = wheel->direction * 32767; return true; }
        wheel->phase = WHEEL_NEUTRAL; wheel->phaseAt = now;
        return true;
    }
    if (wheel->phase == WHEEL_NEUTRAL && now - wheel->phaseAt < 0.06) return true;
    if (wheel->queued) {
        wheel->direction = wheel->queued > 0 ? 1 : -1;
        wheel->queued -= wheel->direction;
        wheel->phase = WHEEL_STEP; wheel->phaseAt = now;
        *rx = wheel->direction * 32767;
    } else if (now - wheel->lastScroll >= 0.32) {
        wheel->phase = WHEEL_RELEASE; wheel->phaseAt = now;
        *buttons = 0;
    } else {
        wheel->phase = WHEEL_NEUTRAL; wheel->phaseAt = now;
    }
    return true;
}
