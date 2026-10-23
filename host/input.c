#include "hle.h"
#include "input.h"
#include "vibration.h"
#include <pthread.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "equipment_wheel.h"

#include "camera_controller.inc"
#include "native_ui.inc"

_Static_assert(sizeof(ZeldaPadState) == 0x28, "SDK Npad state ABI");
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t sampling, latched_buttons;
static uint64_t controller_buttons, controller_latched;
static uint64_t touch_buttons;
static int32_t controller_axes[4], touch_axes[4];
static uint64_t virtual_buttons, virtual_latched;
static EquipmentWheel equipmentWheel;
static _Atomic int lastInputDevice = INPUT_DEVICE_KEYBOARD_MOUSE;
void inputNoteDevice(InputDevice device) { atomic_store_explicit(&lastInputDevice, device, memory_order_relaxed); }
InputDevice inputGetDevice(void) { return atomic_load_explicit(&lastInputDevice, memory_order_relaxed); }

void inputScrollEquipment(int direction) {
    (void)direction; // Equipment is selected by holding its key and moving the right stick.
}
void inputCancelEquipmentScroll(void) {
    pthread_mutex_lock(&input_lock);
    equipmentWheelReset(&equipmentWheel);
    pthread_mutex_unlock(&input_lock);
}

static unsigned char keys[128];
static uint32_t styles = 1, player_enabled = 1;
static const struct { unsigned key; uint64_t button; } mapping[] = {
    // 现代 PC 动作游戏标准键盘映射
    {14, 1u << 0}, // E 键: A (交互/拾取/对话/骑马)
    {56, 1u << 1}, // Left Shift: B (疾跑/冲刺/闪避)
    {60, 1u << 1}, // Right Shift: B (备用疾跑)
    {49, 1u << 2}, // Space 空格: X (跳跃/滑翔伞)
    {3,  1u << 3}, // F 键: Y (近战普攻备用键)
    {12, 1u << 6}, // Q 键: L (究极手/余料建造技能)
    {15, 1u << 7}, // R 键: R (投掷武器/准备投掷)
    {8,  1u << 4}, // C 键: L3 (潜行/下蹲)
    {59, 1u << 4}, // Left Ctrl: L3 (备用下蹲)
    {9,  1u << 5}, // V 键: R3 (望远镜/标记点)
    {48, 1u << 11},// Tab 键: Minus (地图/任务)
    {46, 1u << 11},// M 键: Minus (备用地图)
    {36, 1u << 10},// Return/Enter: Plus (背包/暂停)
    {35, 1u << 10},// P 键: Plus (备用背包)
    {53, 1u << 10},// ESC 键: Plus (备用背包/暂停)
    // 数字键快速呼出轮盘
    {18, 1u << 14},// 1 键: D-Pad Right (快速武器轮盘)
    {19, 1u << 12},// 2 键: D-Pad Left (快速盾牌轮盘)
    {20, 1u << 13},// 3 键: D-Pad Up (快速材料轮盘)
    {21, 1u << 15},// 4 键: D-Pad Down (吹口哨)
    // 原版兼容键位别名
    {7,  1u << 0}, // X 键 -> A
    {6,  1u << 1}, // Z 键 -> B
    // 物理方向键
    {123, 1u << 12}, {126, 1u << 13}, {124, 1u << 14}, {125, 1u << 15},
};

static void inputUpdateStickModeLocked(void) {
    uint64_t held = touch_buttons | virtual_buttons | controller_buttons;
    for (unsigned i = 0; i < sizeof mapping / sizeof *mapping; i++)
        if (keys[mapping[i].key]) held |= mapping[i].button;
    cameraSetRightStickMode((held & ((1u << 7) | (1u << 12) | (1u << 13) | (1u << 14))) != 0);
}

void input_key(unsigned code, int pressed) {
    if (code >= sizeof keys) return;
    if (pressed) inputNoteDevice(INPUT_DEVICE_KEYBOARD_MOUSE);
    pthread_mutex_lock(&input_lock);
    if (pressed && !keys[code]) for (unsigned i = 0; i < sizeof mapping / sizeof *mapping; i++)
        if (mapping[i].key == code) {
            latched_buttons |= mapping[i].button;
            equipmentWheelReset(&equipmentWheel);
        }
    keys[code] = !!pressed;
    inputUpdateStickModeLocked();
    pthread_mutex_unlock(&input_lock);
}

void input_clear(void) {
    pthread_mutex_lock(&input_lock); memset(keys, 0, sizeof keys); latched_buttons = 0;
    controller_buttons = controller_latched = 0; touch_buttons = virtual_buttons = virtual_latched = 0; memset(controller_axes, 0, sizeof controller_axes);
    memset(touch_axes, 0, sizeof touch_axes);
    equipmentWheelReset(&equipmentWheel);
    cameraReset();
    cameraSetRightStickMode(false);
    pthread_mutex_unlock(&input_lock);
}

void input_controller_clear(void) {
    pthread_mutex_lock(&input_lock);
    controller_buttons = controller_latched = 0; memset(controller_axes, 0, sizeof controller_axes);
    inputUpdateStickModeLocked();
    pthread_mutex_unlock(&input_lock);
    if (inputGetDevice() == INPUT_DEVICE_GAMEPAD) inputNoteDevice(INPUT_DEVICE_KEYBOARD_MOUSE);
}

static void input_button(uint64_t buttonMask, bool pressed, InputDevice device) {
    if (pressed) inputNoteDevice(device);
    pthread_mutex_lock(&input_lock);
    if (pressed) {
        equipmentWheelReset(&equipmentWheel);
        touch_buttons |= buttonMask;
        latched_buttons |= buttonMask;
    } else {
        touch_buttons &= ~buttonMask;
    }
    inputUpdateStickModeLocked();
    pthread_mutex_unlock(&input_lock);
}

void inputTouchButton(uint64_t mask, bool pressed) { input_button(mask, pressed, INPUT_DEVICE_TOUCH); }
void inputPointerButton(uint64_t mask, bool pressed) { input_button(mask, pressed, INPUT_DEVICE_KEYBOARD_MOUSE); }
void inputTouchStick(float x, float y) {
    if (!isfinite(x) || !isfinite(y) || hypotf(x, y) < .12f) x = y = 0;
    if (hypotf(x, y) > .20f) inputNoteDevice(INPUT_DEVICE_TOUCH);
    pthread_mutex_lock(&input_lock);
    touch_axes[0] = (int32_t)lroundf(fmaxf(-1, fminf(1, x)) * 32767);
    touch_axes[1] = (int32_t)lroundf(fmaxf(-1, fminf(1, y)) * 32767);
    pthread_mutex_unlock(&input_lock);
}

void inputTouchController(uint64_t buttons, float lx, float ly, float rx, float ry) {
    float axes[4] = {lx, ly, rx, ry};
    bool deliberate = (buttons & 0xffff) != 0;
    for (unsigned i = 0; i < 4; i++) if (isfinite(axes[i]) && fabsf(axes[i]) > .20f) deliberate = true;
    if (deliberate) { inputNoteDevice(INPUT_DEVICE_TOUCH); inputCancelEquipmentScroll(); }
    pthread_mutex_lock(&input_lock);
    buttons &= 0xffff;
    virtual_latched |= buttons & ~virtual_buttons;
    virtual_buttons = buttons;
    inputUpdateStickModeLocked();
    for (unsigned i = 0; i < 2; i++) {
        float x = axes[i * 2], y = axes[i * 2 + 1];
        if (!isfinite(x) || !isfinite(y) || hypotf(x, y) < .12f) x = y = 0;
        touch_axes[i * 2] = (int32_t)lroundf(fmaxf(-1, fminf(1, x)) * 32767);
        touch_axes[i * 2 + 1] = (int32_t)lroundf(fmaxf(-1, fminf(1, y)) * 32767);
    }
    pthread_mutex_unlock(&input_lock);
}
void inputTouchControllerClear(void) {
    pthread_mutex_lock(&input_lock);
    virtual_buttons = virtual_latched = 0;
    inputUpdateStickModeLocked();
    memset(touch_axes, 0, sizeof touch_axes);
    pthread_mutex_unlock(&input_lock);
}

void input_controller(uint64_t buttons, float lx, float ly, float rx, float ry) {
    float axes[4] = {lx, ly, rx, ry};
    if (buttons != 0 || fabsf(lx) > 0.15f || fabsf(ly) > 0.15f || fabsf(rx) > 0.15f || fabsf(ry) > 0.15f) {
        inputNoteDevice(INPUT_DEVICE_GAMEPAD);
        inputCancelEquipmentScroll();
        cameraNoteGamepadActivity();
    }
    pthread_mutex_lock(&input_lock);
    buttons &= 0xffff;
    controller_latched |= buttons & ~controller_buttons; controller_buttons = buttons;
    inputUpdateStickModeLocked();
    for (unsigned s = 0; s < 2; s++) {
        float x = axes[s * 2], y = axes[s * 2 + 1];
        if (!isfinite(x) || !isfinite(y) || hypotf(x, y) < .12f) x = y = 0;
        controller_axes[s * 2] = (int32_t)lroundf(fmaxf(-1, fminf(1, x)) * 32767);
        controller_axes[s * 2 + 1] = (int32_t)lroundf(fmaxf(-1, fminf(1, y)) * 32767);
    }
    pthread_mutex_unlock(&input_lock);
}

int input_states(ZeldaPadState *out, int count, uint32_t id, uint32_t style) {
    if (!out || count <= 0) return 0;
    pthread_mutex_lock(&input_lock);
    *out = (ZeldaPadState){.sampling = ++sampling};
    if (id == 0 && player_enabled && (styles & style & 1)) {
        out->buttons = latched_buttons | controller_buttons | controller_latched | touch_buttons | virtual_buttons | virtual_latched;
        latched_buttons = controller_latched = virtual_latched = 0;
        for (unsigned i = 0; i < sizeof mapping / sizeof *mapping; i++) if (keys[mapping[i].key]) out->buttons |= mapping[i].button;
        
        // 键盘基础摇杆映射
        out->lx = (keys[2] - keys[0]) * 32767; out->ly = (keys[13] - keys[1]) * 32767;
        out->rx = (keys[37] - keys[38]) * 32767; out->ry = (keys[34] - keys[40]) * 32767;

        // 实体手柄优先机制：若手柄右摇杆正在推向非零，完全采用手柄，避免鼠标与手柄打架
        bool gamepadRightStickActive = (controller_axes[2] != 0 || controller_axes[3] != 0);
        if (touch_axes[0] || touch_axes[1]) { out->lx = touch_axes[0]; out->ly = touch_axes[1]; }
        if (controller_axes[0] || controller_axes[1]) { out->lx = controller_axes[0]; out->ly = controller_axes[1]; }
        if (gamepadRightStickActive) {
            out->rx = controller_axes[2];
            out->ry = controller_axes[3];
        } else if (touch_axes[2] || touch_axes[3]) {
            out->rx = touch_axes[2]; out->ry = touch_axes[3];
        } else {
            // 实体手柄右摇杆静止时，接管鼠标视角动力学
            int32_t camRx = 0, camRy = 0;
            if (cameraPoll(&camRx, &camRy)) {
                out->rx = camRx;
                out->ry = camRy;
            }
        }

        // 智能锁敌系统：自动注入 ZL 注视状态与切怪脉冲
        out->buttons |= cameraGetTargetLockButtons();
        out->attributes = 3; // Connected and wired.
    }
    pthread_mutex_unlock(&input_lock);
    return 1;
}

#define WHY "Host keyboard and GameController supply one FullKey controller"
HLE_FORCE("_ZN2nn3hid14InitializeNpadEv", WHY, void, h_pad_init, void) { input_clear(); cameraInit(); }
HLE_FORCE("_ZN2nn3hid22SetSupportedNpadIdTypeEPKjm", WHY, void, h_pad_ids, const uint32_t *ids, uint64_t n) {
    if (n > 10 || (!ids && n)) die("invalid supported controller IDs");
    pthread_mutex_lock(&input_lock); player_enabled = 0;
    for (uint64_t i = 0; i < n; i++) if (ids[i] == 0) player_enabled = 1;
    pthread_mutex_unlock(&input_lock);
}
HLE_FORCE("_ZN2nn3hid24SetSupportedNpadStyleSetENS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", WHY, void, h_pad_styles, uint32_t value) {
    pthread_mutex_lock(&input_lock); styles = value; pthread_mutex_unlock(&input_lock);
}
HLE_FORCE("_ZN2nn3hid15GetNpadStyleSetERKj", WHY, uint32_t, h_pad_style, const uint32_t *id) {
    pthread_mutex_lock(&input_lock); uint32_t result = *id == 0 && player_enabled ? styles & 1 : 0; pthread_mutex_unlock(&input_lock); return result;
}
#define STATES(symbol, name, style) HLE_FORCE(symbol, WHY, int, name, ZeldaPadState *out, int n, const uint32_t *id) { return input_states(out, n, *id, style); }
STATES("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj", h_pad_full, 1)
STATES("_ZN2nn3hid13GetNpadStatesEPNS0_17NpadHandheldStateEiRKj", h_pad_handheld, 2)
STATES("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyDualStateEiRKj", h_pad_dual, 4)
STATES("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyLeftStateEiRKj", h_pad_left, 8)
STATES("_ZN2nn3hid13GetNpadStatesEPNS0_17NpadJoyRightStateEiRKj", h_pad_right, 16)
HLE_FORCE("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE", WHY, void, h_pad_hold, int type) { (void)type; }
HLE_FORCE("_ZN2nn3hid14DisconnectNpadERKj", WHY, void, h_pad_disconnect, const uint32_t *id) {
    pthread_mutex_lock(&input_lock); if (*id == 0) player_enabled = 0; pthread_mutex_unlock(&input_lock);
}
HLE_FORCE("_ZN2nn3hid23GetSixAxisSensorHandlesEPNS0_19SixAxisSensorHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", WHY,
          int, h_pad_sensors, void *out, int n, const uint32_t *id, uint32_t style) { (void)out; (void)n; (void)id; (void)style; return 0; }
HLE_FORCE("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", WHY,
          int, h_pad_vibration, uint32_t *out, int n, const uint32_t *id, uint32_t style) {
    pthread_mutex_lock(&input_lock);
    int count = id && player_enabled ? vibration_handles(out, n, *id, styles & style) : 0;
    pthread_mutex_unlock(&input_lock); return count;
}
HLE_FORCE("_ZN2nn3hid15InitializeMouseEv", WHY, void, h_mouse_init, void) {}
HLE_FORCE("_ZN2nn3hid18InitializeKeyboardEv", WHY, void, h_keyboard_init, void) {}
HLE_FORCE("_ZN2nn3hid14GetMouseStatesEPNS0_10MouseStateEi", WHY, int, h_mouse_states, void *out, int n) {
    if (n <= 0) return 0; memset(out, 0, 0x28); return 1;
}
HLE_FORCE("_ZN2nn3hid16GetKeyboardStateEPNS0_13KeyboardStateE", WHY, void, h_keyboard_state, void *out) { memset(out, 0, 0x30); }
