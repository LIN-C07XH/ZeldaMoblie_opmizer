#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "camera_controller.h"

typedef struct {
    int64_t sampling;
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    uint32_t attributes, reserved;
} ZeldaPadState;

void input_key(unsigned code, int pressed);
void input_clear(void);
void input_controller(uint64_t buttons, float lx, float ly, float rx, float ry);
void input_controller_clear(void);
/// 触屏虚拟按键状态设置 (pressed: true 为按下, false 为松开)
void inputTouchButton(uint64_t buttonMask, bool pressed);
int input_states(ZeldaPadState *out, int count, uint32_t id, uint32_t style);

/// Last deliberate user input; synthesized HID samples never change this.
typedef enum { INPUT_DEVICE_KEYBOARD_MOUSE, INPUT_DEVICE_GAMEPAD, INPUT_DEVICE_TOUCH } InputDevice;
void inputNoteDevice(InputDevice device);
InputDevice inputGetDevice(void);
void inputPointerButton(uint64_t buttonMask, bool pressed);
void inputTouchStick(float x, float y);
void inputScrollEquipment(int direction);
void inputCancelEquipmentScroll(void);
/// Live native CtrlGuide/DoCommand state; zero outside active gameplay or after a stale UI frame.
uint32_t inputGetActionEvents(void);
typedef struct {
    uint64_t button;
    uint16_t text[96];
    uint32_t length;
} InputInteractionPrompt;
/// Copied native primary action and localized caption; no guest pointers escape.
bool inputGetInteractionPrompt(InputInteractionPrompt *out);
void inputSetTouchInteractionEnabled(bool enabled);
enum { INPUT_KEYBOARD_PROMPT_CAPACITY = 32 };
typedef struct {
    float x, y, height, alpha; // Normalized game image coordinates; origin at top left.
    uint16_t label[8];
} InputKeyboardPrompt;
typedef struct {
    uint32_t count;
    InputKeyboardPrompt keys[INPUT_KEYBOARD_PROMPT_CAPACITY];
} InputKeyboardPrompts;
void inputSetKeyboardPromptOverlayEnabled(bool enabled);
/// Copies current draw data and renews the frontend's short rendering lease.
void inputGetKeyboardPrompts(InputKeyboardPrompts *out);
bool inputBindNativeUi(void *shared, void *mainBase);
