#include "action_layout.h"
#include "camera_controller.h"
#include <stddef.h>
#include <math.h>

typedef struct {
    char magic[8];
    uint32_t version, reserved;
    uintptr_t callback;
} NativeUiBridge;
typedef struct {
    const uint16_t *text;
    int32_t length, padding;
    int64_t metadata;
} NativeUiTextSpan;
typedef struct { uintptr_t object; uint32_t index; uint8_t flags, padding[3]; } NativeUiBlackboardRef;
_Static_assert(sizeof(NativeUiBridge) == 24 && offsetof(NativeUiBridge, callback) == 16, "native UI bridge ABI");
_Static_assert(sizeof(NativeUiTextSpan) == 24 && offsetof(NativeUiTextSpan, metadata) == 16, "native UTF-16 span ABI");
_Static_assert(sizeof(NativeUiBlackboardRef) == 16, "native blackboard handle ABI");

static struct {
    uintptr_t base;
    void **blackboardSlot;
    uint32_t (*getBoard)(void *, NativeUiBlackboardRef *, uint32_t);
    uint32_t (*getEnum)(void *, uint32_t *, const NativeUiBlackboardRef *, uint32_t);
    uint32_t (*getString)(void *, const char **, const NativeUiBlackboardRef *, uint32_t);
    void (*rebuildGuide)(void *, uint32_t);
    uint32_t (*getMessage)(NativeUiTextSpan *, const char **, const char **, uint32_t);
} nativeUiApi;
static pthread_mutex_t nativeUiLock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    uint64_t guides;
    double guideTick, promptTick, drawRootTick;
    bool interact;
    uintptr_t guideScreen;
    bool keyboard;
    InputInteractionPrompt prompt;
    uintptr_t promptRoot; // Identity only; only walk parents of a live Draw argument.
    uint64_t frames, commandFrames, draws, scopes;
} nativeUiState;
static struct {
    bool enabled, announced;
    double frontendTick, diagnosticTick;
    InputKeyboardPrompt keys[INPUT_KEYBOARD_PROMPT_CAPACITY];
    double ticks[INPUT_KEYBOARD_PROMPT_CAPACITY];
} nativeUiOverlay;
static bool nativeUiTouchEnabled;
void inputSetTouchInteractionEnabled(bool enabled) {
    pthread_mutex_lock(&nativeUiLock); nativeUiTouchEnabled = enabled; pthread_mutex_unlock(&nativeUiLock);
}
void inputSetKeyboardPromptOverlayEnabled(bool enabled) {
    pthread_mutex_lock(&nativeUiLock);
    memset(&nativeUiOverlay, 0, sizeof nativeUiOverlay);
    nativeUiOverlay.enabled = enabled;
    pthread_mutex_unlock(&nativeUiLock);
}
void inputGetKeyboardPrompts(InputKeyboardPrompts *out) {
    if (!out) return;
    *out = (InputKeyboardPrompts){0};
    double now = cameraNow();
    bool active = cameraIsGameplayActive() && inputGetDevice() == INPUT_DEVICE_KEYBOARD_MOUSE;
    pthread_mutex_lock(&nativeUiLock);
    nativeUiOverlay.frontendTick = now;
    if (active && nativeUiOverlay.enabled && nativeUiState.drawRootTick > 0 && now - nativeUiState.drawRootTick <= .15)
        for (unsigned i = 0; i < INPUT_KEYBOARD_PROMPT_CAPACITY; i++)
            if (nativeUiOverlay.ticks[i] > 0 && now - nativeUiOverlay.ticks[i] <= .075)
                out->keys[out->count++] = nativeUiOverlay.keys[i];
    if (!active) memset(nativeUiOverlay.ticks, 0, sizeof nativeUiOverlay.ticks);
#ifdef LOG
    if (nativeUiOverlay.enabled && now - nativeUiOverlay.diagnosticTick >= 5) {
        nativeUiOverlay.diagnosticTick = now;
        LOG("UI_PROMPTS status gameplay=%d device=%d frame=%llu command_frame=%llu draw=%llu scope=%llu action=%llu labels=%u",
            cameraIsGameplayActive(), inputGetDevice(), (unsigned long long)nativeUiState.frames,
            (unsigned long long)nativeUiState.commandFrames, (unsigned long long)nativeUiState.draws,
            (unsigned long long)nativeUiState.scopes, (unsigned long long)nativeUiState.prompt.button, out->count);
    }
#endif
    pthread_mutex_unlock(&nativeUiLock);
}

static uint64_t nativeUiLoad64(const void *p, size_t offset) {
    uint64_t value; memcpy(&value, (const char *)p + offset, sizeof value); return value;
}

// Native CtrlGuideSet::GuideType indices, verified against the v1.4.3
// enumeration table. These are live engine guide requests, never key presses.
static uint32_t nativeUiEvents(uint64_t guides, bool interact) {
    const uint64_t air = (1ull << 13) | (0x7full << 18) | (1ull << 43) | (1ull << 45);
    const uint64_t climbing = 7ull << 8;
    const uint64_t powers = (0x7full << 27) | (0xfull << 35);
    uint32_t events = 0;
    if (guides & air) events |= EVENT_MASK_IN_AIR;
    if (guides & climbing) events |= EVENT_MASK_CLIMBING;
    if (guides & (1ull << 11)) events |= EVENT_MASK_SWIMMING;
    if (guides & ((1ull << 2) | (1ull << 12))) events |= EVENT_MASK_COMBAT;
    if (guides & powers) events |= EVENT_MASK_ULTRAHAND;
    if (interact) events |= EVENT_MASK_INTERACT;
    return events;
}

static const uint16_t *nativeUiKeyboardLabel(uint32_t key) {
    // The native option-swapped B/X cases reach this hook as effective B or X.
    // Labels follow the actual host bindings; controller/touch use native icons.
    static const uint16_t *const labels[] = {
        NULL, u"E", u"Shift", u"Space", u"LMB", u"Q", u"MMB", u"R", u"RMB", u"Enter", u"Tab",
        u"3", u"4", u"2", u"1", u"3/4", u"2/1", u"1-4", u"WASD", u"W", u"S", u"A", u"D",
        u"W/S", u"A/D", u"Ctrl", u"Mouse", u"Mouse", u"Mouse", u"Mouse", u"Mouse", u"Mouse", u"Mouse", u"V", u"Mouse",
    };
    return key < sizeof labels / sizeof *labels ? labels[key] : NULL;
}

static InputInteractionPrompt nativeUiReadPrompt(void) {
    InputInteractionPrompt out = {0};
    // The same DoCommand blackboard and typed getters used in 0x2148790.
    // Calls stay on the game's UI thread. No actor or UI pointer is read later
    // from the host render/input threads.
    if (!nativeUiApi.blackboardSlot || !*nativeUiApi.blackboardSlot) return out;
    void *manager = *nativeUiApi.blackboardSlot;
    NativeUiBlackboardRef ref = {0};
    uint32_t key = 0;
    const char *message = NULL;
    if (!(nativeUiApi.getBoard(manager, &ref, 0xca62c947) & 1) ||
        !(nativeUiApi.getEnum(manager, &key, &ref, 0x3ffc8e66) & 1) ||
        !(nativeUiApi.getString(manager, &message, &ref, 0xd7e9ac3a) & 1) ||
        !message || !message[0] || !strcmp(message, "Empty")) return out;
    switch (key) {
        case 0x54dcf7ce: out.button = PAD_BTN_A; break;
        case 0xcca66a8a: out.button = PAD_BTN_B; break;
        case 0xdd29f651: out.button = PAD_BTN_X; break;
        case 0x2814d70c: out.button = PAD_BTN_Y; break;
        case 0xf3397f3f: out.button = PAD_BTN_L; break;
        default: return out;
    }
    NativeUiTextSpan span = {0};
    const char *layout = (const char *)(nativeUiApi.base + 0x2c924e1);
    if (nativeUiApi.getMessage && (nativeUiApi.getMessage(&span, &layout, &message, 1) & 1) &&
        span.text && span.length > 0 && span.length <= 4096) {
        // MSBT control parameters are binary; copy visible UTF-16 only.
        for (int32_t i = 0; i < span.length && out.length < 95;) {
            uint16_t code = span.text[i++];
            if (!code) break;
            if (code == 14) {
                if (span.length - i < 3) break;
                uint16_t bytes = span.text[i + 2];
                if ((bytes & 1) || 3 + bytes / 2 > span.length - i) break;
                i += 3 + bytes / 2; continue;
            }
            if (code == 15) { if (span.length - i < 2) break; i += 2; continue; }
            if (code >= 0xe000 && code <= 0xf8ff) continue;
            out.text[out.length++] = code;
        }
        if (out.length && out.text[out.length - 1] >= 0xd800 && out.text[out.length - 1] <= 0xdbff) out.length--;
    }
    if (!out.length) { out.text[0] = 0x64cd; out.text[1] = 0x4f5c; out.length = 2; } // 操作
    out.text[out.length] = 0;
    return out;
}

static bool nativeUiCommandScope(const void *pane) {
    if (strncmp((const char *)pane + 0xb0, "N_InOut_00", 24)) return false;
    const void *root = (void *)nativeUiLoad64(pane, 0x18);
    if (!root || memcmp((const char *)root + 0xb0, "RootPane\0", 9)) return false;
    // This direct-child signature occurs only in DoCommand in the verified
    // Common archive. N_InOut_00 alone is shared by many other screens.
    uintptr_t end = (uintptr_t)pane + 0x20, link = nativeUiLoad64(pane, 0x28);
    unsigned found = 0;
    for (unsigned i = 0; link && link != end && i < 16; i++) {
        const void *child = (void *)(link - 8);
        const char *name = (const char *)child + 0xb0;
        if (!strncmp(name, "N_MainButton_00", 24)) found |= 1;
        if (!strncmp(name, "N_SubButton_00", 24)) found |= 2;
        link = nativeUiLoad64(child, 0x10);
    }
    return found == 3;
}
static uint64_t nativeUiDrawCommand(void *pane) {
    double now = cameraNow();
    const unsigned char *bytes = pane;
    bool visible = cameraIsGameplayActive() && (bytes[0x58] & 0x81) == 0x81 && bytes[0x5a];
    InputInteractionPrompt prompt = visible ? nativeUiReadPrompt() : (InputInteractionPrompt){0};
    uintptr_t root = nativeUiLoad64(pane, 0x18);
    pthread_mutex_lock(&nativeUiLock);
    bool first = !nativeUiState.scopes++;
    if (nativeUiState.promptRoot != root || !visible)
        memset(nativeUiOverlay.ticks, 0, sizeof nativeUiOverlay.ticks);
    nativeUiState.promptRoot = root;
    nativeUiState.drawRootTick = visible ? now : 0;
    nativeUiState.prompt = prompt;
    nativeUiState.promptTick = now;
    nativeUiState.interact = prompt.button == PAD_BTN_A;
    bool touch = nativeUiTouchEnabled;
    pthread_mutex_unlock(&nativeUiLock);
#ifdef LOG
    if (first) LOG("UI_PROMPTS DoCommand draw scope matched; live native hierarchy");
#else
    (void)first;
#endif
    // Hiding the draw group leaves native transforms/animations running and
    // needs no saved visibility bit to restore on a controller/device switch.
    return visible && touch && inputGetDevice() == INPUT_DEVICE_TOUCH;
}

static uint64_t nativeUiDrawKey(void *pane) {
    if (!cameraIsGameplayActive() || inputGetDevice() != INPUT_DEVICE_KEYBOARD_MOUSE) return 0;
    double now = cameraNow();
    pthread_mutex_lock(&nativeUiLock);
    uintptr_t root = nativeUiState.promptRoot;
    bool ready = root && nativeUiState.drawRootTick > 0 && nativeUiOverlay.enabled &&
        now - nativeUiState.drawRootTick <= .15 && now - nativeUiOverlay.frontendTick <= .15;
    pthread_mutex_unlock(&nativeUiLock);
    if (!ready) return 0;
    static const struct { char name[24]; uint8_t key; } panes[] = {
        {"P_CaptureKeyA_00",1},{"T_B_00",2},{"T_X_00",3},{"T_Y_00",4},
        {"P_KeyA_00",1},{"P_KeyA_02",1},{"T_B_01",2},{"T_X_02",3},{"T_Y_03",4},{"P_KeyA_01",1},
        {"P_CaptureKeyB_00",2},{"T_A_01",1},{"T_X_01",3},{"T_Y_01",4},{"P_KeyB_00",2},
        {"P_CaptureKeyX_00",3},{"T_A_02",1},{"T_B_02",2},{"T_Y_02",4},{"P_KeyX_00",3},
        {"P_CaptureKeyY_00",4},{"T_A_03",1},{"T_B_03",2},{"T_X_03",3},{"P_Key0_00",4},
        {"P_CaptureKeyL_00",5},{"P_KeyL_00",5},{"P_LStick_00",18},{"P_LStickUp_00",19},
        {"P_ZL_00",6},{"T_Plus_00",9},
    };
    _Static_assert(sizeof panes / sizeof *panes <= INPUT_KEYBOARD_PROMPT_CAPACITY, "prompt slots");
    unsigned slot;
    for (slot = 0; slot < sizeof panes / sizeof *panes; slot++)
        if (!strncmp((char *)pane + 0xb0, panes[slot].name, 24)) break;
    if (slot == sizeof panes / sizeof *panes) return 0;
    // Match Pane::Draw's native visibility/calculated-matrix gate. Only leaf
    // artwork can be replaced; original captions, capture panes and hold gauges draw normally.
    const unsigned char *bytes = pane;
    if ((bytes[0x58] & 0x81) != 0x81 || !bytes[0x5a] ||
        nativeUiLoad64(pane, 0x28) != (uintptr_t)pane + 0x20) return 0;
    const void *ancestor = pane;
    for (unsigned depth = 0; depth < 16 && (uintptr_t)ancestor != root; depth++) {
        ancestor = (void *)nativeUiLoad64(ancestor, 0x18);
        if (!ancestor) return 0;
    }
    if ((uintptr_t)ancestor != root || memcmp((char *)ancestor + 0xb0, "RootPane\0", 9)) return 0;
    float matrix[12], size[2];
    memcpy(matrix, bytes + 0x70, sizeof matrix);
    memcpy(size, (char *)ancestor + 0x50, sizeof size);
    float scale = hypotf(matrix[0], matrix[4]);
    if (!isfinite(size[0]) || !isfinite(size[1]) || size[0] < 1 || size[1] < 1 ||
        !isfinite(matrix[3]) || !isfinite(matrix[7]) || !isfinite(scale) || scale < .01f || scale > 10) return 0;
    InputKeyboardPrompt key = {.x=.5f + matrix[3]/size[0], .y=.5f - matrix[7]/size[1],
        .height=32*scale/size[1], .alpha=bytes[0x5a]/255.f};
    if (key.x < 0 || key.x > 1 || key.y < 0 || key.y > 1) return 0;
    const uint16_t *label = nativeUiKeyboardLabel(panes[slot].key);
    for (unsigned i = 0; i < 7 && label[i]; i++) key.label[i] = label[i];
    pthread_mutex_lock(&nativeUiLock);
    nativeUiOverlay.keys[slot] = key;
    nativeUiOverlay.ticks[slot] = now;
    bool first = !nativeUiOverlay.announced;
    nativeUiOverlay.announced = true;
    pthread_mutex_unlock(&nativeUiLock);
#ifdef LOG
    if (first) LOG("UI_PROMPTS keyboard overlay active; live DoCommand draw, original layout");
#else
    (void)first;
#endif
    return 1;
}

// The UI owns a DrawInfo projection independently of scene cameras. Layout::Draw
// supplies it before native SetLayout invalidates material/draw state. Fit the
// observed, centered 1920x1080 design canvas without touching pane transforms.
static bool nativeUiFitProjection(void *drawInfo) {
    if (nativeUiLoad64(drawInfo, 0) != nativeUiApi.base + 0x391ae68) return false;
    float width, height;
    cameraGetDisplaySize(&width, &height);
    if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) return false;
    float aspect = width / height;
    if (fabsf(aspect - 16.f/9) < .01f) return false;
    float matrix[16];
    memcpy(matrix, (char *)drawInfo+0x10, sizeof matrix);
    for (unsigned i=0; i<16; i++) if (!isfinite(matrix[i])) return false;
    const unsigned zero[] = {1,2,3,4,6,7,8,9,12,13,14};
    for (unsigned i=0; i<sizeof zero/sizeof *zero; i++)
        if (fabsf(matrix[zero[i]]) > .000001f) return false;
    if (fabsf(matrix[15]-1) > .000001f) return false;
    float x=1.f/960, y=1.f/540;
    if (aspect > 16.f/9) x=y/aspect; else y=x*aspect;
    bool original = fabsf(matrix[0]*960-1)<.0001f && fabsf(matrix[5]*540-1)<.0001f;
    bool fitted = fabsf(matrix[0]/x-1)<.0001f && fabsf(matrix[5]/y-1)<.0001f;
    if (!original || fitted) return false;
    memcpy((char *)drawInfo+0x10, &x, sizeof x);
    memcpy((char *)drawInfo+0x24, &y, sizeof y);
#ifdef LOG
    static bool reported;
    pthread_mutex_lock(&nativeUiLock);
    if (!reported) {
        reported=true;
        LOG("UI_PROJECTION centered 1920x1080 canvas fitted to %.0fx%.0f; pixel_scale=%.6f",width,height,x*width/(y*height));
    }
    pthread_mutex_unlock(&nativeUiLock);
#endif
    return true;
}

static uint64_t nativeUiCallback(uint32_t kind, void *object, uint64_t value) {
    if (!object) return 0;
    if (kind == 4) { nativeUiFitProjection(object); return 0; }
    if (kind == 3) {
        pthread_mutex_lock(&nativeUiLock); nativeUiState.draws++; pthread_mutex_unlock(&nativeUiLock);
        if (!cameraIsGameplayActive()) return 0;
        if (nativeUiCommandScope(object)) return nativeUiDrawCommand(object);
        return nativeUiDrawKey(object);
    }
    if (kind == 1) {
        const uint16_t *label = nativeUiKeyboardLabel((uint32_t)value);
        if (inputGetDevice() != INPUT_DEVICE_KEYBOARD_MOUSE || !label) return 0;
        int32_t length = 0;
        while (label[length]) length++;
        NativeUiTextSpan span = {.text = label, .length = length, .metadata = -1};
        memcpy(object, &span, sizeof span);
        return 1;
    }
    if (kind != 2) return 0;
    pthread_mutex_lock(&nativeUiLock); nativeUiState.frames++; pthread_mutex_unlock(&nativeUiLock);
    uintptr_t vtable = nativeUiLoad64(object, 0);
    double now = cameraNow();
    if (vtable == nativeUiApi.base + 0x3726538) {
        uint64_t guides = nativeUiLoad64(object, 0x1b8);
        bool keyboard = inputGetDevice() == INPUT_DEVICE_KEYBOARD_MOUSE;
        pthread_mutex_lock(&nativeUiLock);
        bool refresh = nativeUiState.guideScreen != (uintptr_t)object || nativeUiState.keyboard != keyboard;
        nativeUiState.guideScreen = (uintptr_t)object; // identity only; never dereferenced asynchronously
        nativeUiState.keyboard = keyboard;
        nativeUiState.guides = guides;
        nativeUiState.guideTick = now;
        pthread_mutex_unlock(&nativeUiLock);
        if (refresh && guides && nativeUiLoad64(object, 0x1c0)) {
            // Rebuild the currently highest-priority native guide, including
            // already-visible icons. Reuse the engine's own layout/text path.
            // Native 0x27c7be4(mask,1) selects the least-significant active bit.
            uint32_t guide = (uint32_t)__builtin_ctzll(guides);
            if (guide <= 48) nativeUiApi.rebuildGuide(object, guide);
        }
    } else if (vtable == nativeUiApi.base + 0x3726bc0) {
        bool visible = !((const unsigned char *)object)[0x1ef];
        InputInteractionPrompt prompt = visible && cameraIsGameplayActive() ? nativeUiReadPrompt() : (InputInteractionPrompt){0};
        pthread_mutex_lock(&nativeUiLock);
        nativeUiState.commandFrames++;
        // Draw-scoped state is authoritative once observed. Frame callbacks
        // are still useful for guide events and before the first visible draw.
        if (nativeUiState.scopes) { pthread_mutex_unlock(&nativeUiLock); return 0; }
        nativeUiState.interact = prompt.button == PAD_BTN_A;
        void *layout = (void *)nativeUiLoad64(object, 0x28);
        uintptr_t root = layout ? nativeUiLoad64(layout, 0x18) : 0;
        if (nativeUiState.promptRoot != root || !prompt.button)
            memset(nativeUiOverlay.ticks, 0, sizeof nativeUiOverlay.ticks);
        nativeUiState.promptRoot = root;
        nativeUiState.prompt = prompt;
        nativeUiState.promptTick = now;
        pthread_mutex_unlock(&nativeUiLock);
    }
    return 0;
}

bool inputGetInteractionPrompt(InputInteractionPrompt *out) {
    if (!out) return false;
    *out = (InputInteractionPrompt){0};
    if (!cameraIsGameplayActive()) return false;
    double now = cameraNow();
    pthread_mutex_lock(&nativeUiLock);
    if (now - nativeUiState.promptTick <= .3) *out = nativeUiState.prompt;
    pthread_mutex_unlock(&nativeUiLock);
    return out->button != 0;
}

uint32_t inputGetActionEvents(void) {
    if (!cameraIsGameplayActive()) return 0;
    double now = cameraNow();
    pthread_mutex_lock(&nativeUiLock);
    uint64_t guides = now - nativeUiState.guideTick <= .3 ? nativeUiState.guides : 0;
    bool interact = now - nativeUiState.promptTick <= .3 && nativeUiState.interact;
    pthread_mutex_unlock(&nativeUiLock);
    return nativeUiEvents(guides, interact);
}

bool inputBindNativeUi(void *shared, void *mainBase) {
    NativeUiBridge *bridge = shared;
    if (!bridge || !mainBase || memcmp(bridge->magic, "ZUI143\0\0", 8) || bridge->version != 4 || bridge->reserved)
        return false;
    uintptr_t base = (uintptr_t)mainBase;
    uintptr_t slot = nativeUiLoad64(mainBase, 0x395dff0);
    // The relocated GOT entry must refer to this module's static blackboard slot.
    if (slot != base + 0x3ad66b0) return false;
    nativeUiApi.base = base;
    nativeUiApi.blackboardSlot = (void **)slot;
    nativeUiApi.getBoard = (void *)(base + 0x3cffe8);
    nativeUiApi.getEnum = (void *)(base + 0x980288);
    nativeUiApi.getString = (void *)(base + 0x2aab9b4);
    nativeUiApi.rebuildGuide = (void *)(base + 0x2147df0);
    nativeUiApi.getMessage = (void *)(base + 0xba42c0);
    bridge->callback = (uintptr_t)nativeUiCallback;
#ifdef LOG
    LOG("UI_PROMPTS bridge=4; draw-scoped discovery; host keyboard labels + touch interaction");
#endif
    return true;
}
