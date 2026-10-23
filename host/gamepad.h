#pragma once
#include <stdbool.h>
void gamepad_start(void);
void gamepad_active(bool active);
void gamepad_stop(void);
// iOS registers its on-screen left stick so it is not selected as a physical pad.
void gamepad_set_virtual_controller(void *controller);
void gamepad_vibration_mode(int mode);   // ZELDA_VIBRATION_*
bool gamepad_test_vibration(void);       // short pulse on the active controller or supported iPhone motor
void gamepad_describe(char *name, unsigned size, bool *haptics);   // empty name when no controller is connected
