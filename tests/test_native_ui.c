#include "input.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static double testTime = 1;
static bool gameplay = true;
static InputDevice device = INPUT_DEVICE_KEYBOARD_MOUSE;
static double cameraNow(void) { return testTime; }
bool cameraIsGameplayActive(void) { return gameplay; }
InputDevice inputGetDevice(void) { return device; }
#include "native_ui.inc"

static int rebuilds, boardCalls;
static uint32_t lastGuide, promptKey = 0x54dcf7ce;
static const char *prompt = "Take";
static bool boardAvailable = true;
static const uint16_t *caption = u"交谈";
static int32_t captionLength = -1;
static uint32_t message(NativeUiTextSpan *out, const char **layout, const char **id, uint32_t flag) {
    assert(*layout == (const char *)(nativeUiApi.base + 0x2c924e1) && *id == prompt && flag == 1);
    int32_t length = 0; while (caption[length]) length++;
    if (captionLength >= 0) length = captionLength;
    *out = (NativeUiTextSpan){.text=caption,.length=length,.metadata=-1}; return 1;
}
static void rebuild(void *screen, uint32_t guide) { assert(screen); lastGuide = guide; rebuilds++; }
static uint32_t board(void *manager, NativeUiBlackboardRef *out, uint32_t key) {
    assert(manager && key == 0xca62c947); boardCalls++; *out = (NativeUiBlackboardRef){.object=1}; return boardAvailable;
}
static uint32_t enumeration(void *manager, uint32_t *out, const NativeUiBlackboardRef *ref, uint32_t key) {
    assert(manager && ref->object == 1 && key == 0x3ffc8e66); *out = promptKey; return 1;
}
static uint32_t string(void *manager, const char **out, const NativeUiBlackboardRef *ref, uint32_t key) {
    assert(manager && ref->object == 1 && key == 0xd7e9ac3a); *out = prompt; return 1;
}
static void store64(void *p, size_t at, uint64_t value) { memcpy((char *)p + at, &value, 8); }
static void labels(void) {
    NativeUiTextSpan span;
    memset(&span, 0x55, sizeof span);
    NativeUiTextSpan original = span;
    device = INPUT_DEVICE_GAMEPAD;
    assert(!nativeUiCallback(1, &span, 1) && !memcmp(&span, &original, sizeof span));
    device = INPUT_DEVICE_TOUCH;
    assert(!nativeUiCallback(1, &span, 1) && !memcmp(&span, &original, sizeof span));
    device = INPUT_DEVICE_KEYBOARD_MOUSE;
    assert(!nativeUiCallback(1, &span, 0));
    assert(!nativeUiCallback(1, &span, 999));
    assert(nativeUiCallback(1, &span, 3)); // effective X, including native B/X options
    assert(span.length == 5 && span.metadata == -1 && !memcmp(span.text, u"Space", 12));
    assert(nativeUiCallback(1, &span, 4));
    assert(span.length == 3 && !memcmp(span.text, u"LMB", 8));
    assert(nativeUiCallback(1, &span, 13));
    assert(span.length == 1 && span.text[0] == '2');
}
static void events(void) {
    const uint64_t base = 0x10000000;
    unsigned char ctrl[0x200] = {0}, action[0x200] = {0};
    void *manager = (void *)1;
    nativeUiApi.base = base; nativeUiApi.rebuildGuide = rebuild;
    nativeUiApi.blackboardSlot = &manager; nativeUiApi.getBoard = board;
    nativeUiApi.getEnum = enumeration; nativeUiApi.getString = string;
    store64(ctrl, 0, base + 0x3726538); store64(ctrl, 0x1c0, 1);
    store64(action, 0, base + 0x3726bc0);
    store64(ctrl, 0x1b8, (1ull << 18) | (1ull << 2)); // parasail + native battle guide
    nativeUiCallback(2, ctrl, 0);
    assert(lastGuide == 2 && rebuilds == 1);
    assert(inputGetActionEvents() == (EVENT_MASK_IN_AIR | EVENT_MASK_COMBAT));
    nativeUiCallback(2, ctrl, 0); assert(rebuilds == 1);
    device = INPUT_DEVICE_GAMEPAD;
    nativeUiCallback(2, ctrl, 0); assert(rebuilds == 2); // existing icons refreshed
    device = INPUT_DEVICE_TOUCH;
    nativeUiCallback(2, ctrl, 0); assert(rebuilds == 2); // both modes retain native icons
    for (unsigned bit = 8; bit <= 10; bit++) assert(nativeUiEvents(1ull << bit, false) == EVENT_MASK_CLIMBING);
    assert(nativeUiEvents(1ull << 11, false) == EVENT_MASK_SWIMMING);
    for (unsigned bit = 18; bit <= 24; bit++) assert(nativeUiEvents(1ull << bit, false) == EVENT_MASK_IN_AIR);
    assert(nativeUiEvents(1ull << 45, false) == EVENT_MASK_IN_AIR);
    assert(nativeUiEvents(1ull << 29, false) == EVENT_MASK_ULTRAHAND);
    assert(nativeUiEvents(1ull << 1, false) == 0);
    nativeUiCallback(2, action, 0); assert(inputGetActionEvents() & EVENT_MASK_INTERACT);
    prompt = "Empty"; nativeUiCallback(2, action, 0); assert(!(inputGetActionEvents() & EVENT_MASK_INTERACT));
    prompt = "Take"; promptKey = 123; nativeUiCallback(2, action, 0); assert(!(inputGetActionEvents() & EVENT_MASK_INTERACT));
    promptKey = 0x54dcf7ce; boardAvailable = false;
    nativeUiCallback(2, action, 0); assert(!(inputGetActionEvents() & EVENT_MASK_INTERACT));
    boardAvailable = true;
    nativeUiCallback(2, action, 0); assert(inputGetActionEvents() & EVENT_MASK_INTERACT);
    action[0x1ef] = 1; nativeUiCallback(2, action, 0); assert(!(inputGetActionEvents() & EVENT_MASK_INTERACT));
    gameplay = false; int calls = boardCalls;
    action[0x1ef] = 0; nativeUiCallback(2, action, 0);
    assert(!inputGetActionEvents() && boardCalls == calls);
    gameplay = true; testTime += .31; assert(!inputGetActionEvents());
    store64(ctrl, 0x1b8, 0); nativeUiCallback(2, ctrl, 0); assert(!inputGetActionEvents());
}
static void binding(void) {
    NativeUiBridge bridge = {.magic="ZUI143", .version=1};
    unsigned char *base = calloc(1, 0x395dff8); assert(base);
    assert(!inputBindNativeUi(&bridge, base) && !bridge.callback);
    store64(base, 0x395dff0, (uintptr_t)base + 0x3ad66b0);
    bridge.version = 1; assert(!inputBindNativeUi(&bridge, base));
    bridge.version = 2; assert(!inputBindNativeUi(&bridge, base));
    bridge.version = 3; assert(!inputBindNativeUi(&bridge, base));
    bridge.version = 4; assert(inputBindNativeUi(&bridge, base) && bridge.callback == (uintptr_t)nativeUiCallback);
    free(base);
}
static void contextual(void) {
    void *manager = (void *)1; nativeUiApi.blackboardSlot = &manager;
    unsigned char screen[0x300] = {0}, layout[0x40] = {0}, root[0xe0] = {0};
    store64(screen, 0, nativeUiApi.base + 0x3726bc0);
    store64(screen, 0x28, (uintptr_t)layout); store64(layout, 0x18, (uintptr_t)root);
    memcpy(root + 0xb0, "RootPane", 9); root[0x58] = 0xa5;
    nativeUiApi.getMessage = message;
    const uint32_t keys[] = {0x54dcf7ce,0xcca66a8a,0xdd29f651,0x2814d70c,0xf3397f3f};
    const uint64_t buttons[] = {PAD_BTN_A,PAD_BTN_B,PAD_BTN_X,PAD_BTN_Y,PAD_BTN_L};
    InputInteractionPrompt out;
    device = INPUT_DEVICE_TOUCH; inputSetTouchInteractionEnabled(true);
    for (unsigned i=0; i<5; i++) {
        promptKey=keys[i]; nativeUiCallback(2,screen,0);
        assert(inputGetInteractionPrompt(&out) && out.button == buttons[i]);
        assert(out.length == 2 && !memcmp(out.text,u"交谈",6));
        assert(root[0x58] == 0xa5); // frame sampling never changes pane visibility
    }
    device = INPUT_DEVICE_GAMEPAD; nativeUiCallback(2,screen,0); assert(root[0x58] == 0xa5);
    root[0x58] = 0xa4;
    device = INPUT_DEVICE_TOUCH; nativeUiCallback(2,screen,0);
    inputSetTouchInteractionEnabled(false); nativeUiCallback(2,screen,0); assert(root[0x58] == 0xa4); // don't unhide a native-hidden root
    const uint16_t tagged[] = u"\x000e\x0000\x0003\x0002\x0001打开\xe040滑翔伞\x000f\x0000\x0003";
    caption = tagged; captionLength = sizeof tagged / sizeof *tagged - 1;
    nativeUiCallback(2,screen,0); assert(inputGetInteractionPrompt(&out));
    assert(out.length == 5 && !memcmp(out.text,u"打开滑翔伞",12));
    promptKey=123; nativeUiCallback(2,screen,0); assert(!inputGetInteractionPrompt(&out));
    promptKey=0x54dcf7ce; nativeUiCallback(2,screen,0); testTime += .31; assert(!inputGetInteractionPrompt(&out));
    nativeUiCallback(2,screen,0); gameplay=false; assert(!inputGetInteractionPrompt(&out)); gameplay=true;

}
static void drawKeys(void) {
    unsigned char root[0xe0]={0}, group[0xe0]={0}, main[0xe0]={0}, sub[0xe0]={0}, key[0xe0]={0}, other[0xe0]={0};
    void *manager=(void *)1; nativeUiApi.blackboardSlot=&manager;
    caption=u"交谈"; captionLength=-1; promptKey=0x54dcf7ce; prompt="Take";
    nativeUiState.promptRoot=0; nativeUiState.drawRootTick=0;
    inputSetTouchInteractionEnabled(false);
    memcpy(root+0xb0,"RootPane",9); root[0x58]=0x81;
    memcpy(group+0xb0,"N_InOut_00",11); group[0x58]=0x81; group[0x5a]=255;
    memcpy(main+0xb0,"N_MainButton_00",16); memcpy(sub+0xb0,"N_SubButton_00",15);
    store64(group,0x18,(uintptr_t)root); store64(group,0x28,(uintptr_t)main+8);
    store64(main,0x18,(uintptr_t)group); store64(main,0x10,(uintptr_t)sub+8);
    store64(sub,0x18,(uintptr_t)group); store64(sub,0x10,(uintptr_t)group+0x20);
    float size[]={1920,1080}, matrix[]={1,0,0,117,0,1,0,-330,0,0,1,0};
    memcpy(root+0x50,size,sizeof size); memcpy(key+0x70,matrix,sizeof matrix);
    memcpy(key+0xb0,"P_KeyA_00",10); key[0x58]=0x81; key[0x5a]=128;
    store64(key,0x28,(uintptr_t)key+0x20); store64(key,0x18,(uintptr_t)main);
    device=INPUT_DEVICE_KEYBOARD_MOUSE;
    InputKeyboardPrompts out;
    assert(!nativeUiCallback(3,key,0)); // no prior screen/frame registration
    inputSetKeyboardPromptOverlayEnabled(true);
    inputGetKeyboardPrompts(&out); assert(!out.count);
    uint64_t frames=nativeUiState.frames;
    assert(!nativeUiCallback(3,group,0)); // discovers scope from real draw, not screen vtable
    assert(nativeUiState.frames==frames);
    unsigned char original[sizeof key]; memcpy(original,key,sizeof key);
    assert(nativeUiCallback(3,key,0));
    assert(!memcmp(key,original,sizeof key));
    inputGetKeyboardPrompts(&out); assert(out.count==1 && out.keys[0].label[0]=='E');
    assert(fabsf(out.keys[0].x-(.5f+117.f/1920))<1e-6);
    assert(fabsf(out.keys[0].y-(.5f+330.f/1080))<1e-6);
    assert(fabsf(out.keys[0].alpha-128.f/255)<1e-6);
    store64(key,0x18,(uintptr_t)other); assert(!nativeUiCallback(3,key,0));
    store64(key,0x18,(uintptr_t)key); assert(!nativeUiCallback(3,key,0));
    store64(key,0x18,(uintptr_t)main);
    key[0x58]=0; assert(!nativeUiCallback(3,key,0)); key[0x58]=0x81;
    store64(key,0x28,(uintptr_t)other+8); assert(!nativeUiCallback(3,key,0));
    store64(key,0x28,(uintptr_t)key+0x20);
    float bad=NAN; memcpy(key+0x7c,&bad,4); assert(!nativeUiCallback(3,key,0));
    memcpy(key+0x70,matrix,sizeof matrix);
    device=INPUT_DEVICE_GAMEPAD; assert(!nativeUiCallback(3,key,0));
    inputGetKeyboardPrompts(&out); assert(!out.count);
    assert(!nativeUiCallback(3,group,0));
    device=INPUT_DEVICE_KEYBOARD_MOUSE; inputGetKeyboardPrompts(&out); assert(!out.count);
    gameplay=false; assert(!nativeUiCallback(3,(void *)1,0)); gameplay=true;
    nativeUiCallback(3,group,0); nativeUiCallback(3,key,0);
    testTime+=.08; inputGetKeyboardPrompts(&out); assert(!out.count);
    prompt="Empty"; nativeUiCallback(3,group,0);
    // Visible native key artwork is sufficient for keyboard replacement even
    // when the primary blackboard action has not been published yet.
    assert(nativeUiCallback(3,key,0));
    InputInteractionPrompt action; assert(!inputGetInteractionPrompt(&action));
    prompt="Take"; nativeUiCallback(3,group,0);
    testTime+=.16; nativeUiCallback(3,group,0); assert(!nativeUiCallback(3,key,0));
    inputGetKeyboardPrompts(&out); assert(nativeUiCallback(3,key,0));
    group[0x58]=0x80; nativeUiCallback(3,group,0); assert(!nativeUiCallback(3,key,0));
    inputGetKeyboardPrompts(&out); assert(!out.count);
    group[0x58]=0x81;
    device=INPUT_DEVICE_TOUCH; inputSetTouchInteractionEnabled(true);
    const uint32_t keys[]={0x54dcf7ce,0xcca66a8a,0xdd29f651,0x2814d70c,0xf3397f3f};
    const uint64_t buttons[]={PAD_BTN_A,PAD_BTN_B,PAD_BTN_X,PAD_BTN_Y,PAD_BTN_L};
    for(unsigned i=0;i<5;i++) {
        promptKey=keys[i]; assert(nativeUiCallback(3,group,0));
        assert(group[0x58]==0x81); // no native flag mutations; hide this draw only
        assert(inputGetInteractionPrompt(&action) && action.button==buttons[i]);
    }
    inputSetTouchInteractionEnabled(false); assert(!nativeUiCallback(3,group,0));
    device=INPUT_DEVICE_GAMEPAD; inputSetTouchInteractionEnabled(true);
    assert(!nativeUiCallback(3,group,0)); // immediate native controller restoration
    memcpy(sub+0xb0,"N_SomethingElse",16);
    assert(!nativeUiCommandScope(group)); // shared N_InOut name in another screen is not enough
    inputSetTouchInteractionEnabled(false); inputSetKeyboardPromptOverlayEnabled(false);
}

int main(void) {
    labels(); events(); contextual(); drawKeys(); binding();
    puts("Native UI: borrowed UTF-16 spans, device restoration, native guide events, typed interaction query, expiry and binding passed");
}
