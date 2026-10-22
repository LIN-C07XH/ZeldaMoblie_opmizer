#include "action_layout.h"
#include "action_layout.inc"
#include "screen_aspect.h"
#include "screen_aspect.inc"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static void testScreenAspect(void) {
    // 1. iPad 12.9" / 13" (2732x2048, 4:3 = 1.333)
    DeviceAspectClass ipadClass = screenClassifyAspect(2732.0f, 2048.0f);
    assert(ipadClass == DEVICE_ASPECT_IPAD_4_3);

    // 2. iPad Pro 11" (2388x1668, ~1.431)
    DeviceAspectClass ipadPro11Class = screenClassifyAspect(2388.0f, 1668.0f);
    assert(ipadPro11Class == DEVICE_ASPECT_IPAD_MODERN);

    // 3. Switch 原生 (1280x720, 16:9 = 1.778)
    DeviceAspectClass switchClass = screenClassifyAspect(1280.0f, 720.0f);
    assert(switchClass == DEVICE_ASPECT_STANDARD_16_9);

    // 4. iPhone 16/17 Pro Max (2868x1320, ~2.173)
    DeviceAspectClass iphonePmClass = screenClassifyAspect(2868.0f, 1320.0f);
    assert(iphonePmClass == DEVICE_ASPECT_IPHONE_TALL);

    // 5. 测试视口计算：iPad 4:3 充满全屏 (ASPECT_MODE_FILL)
    // 游戏原生 16:9 (1920x1080)，屏幕为 iPad (2048x1536)
    ScreenViewport vpFillIpad = screenCalculateViewport(2048.0f, 1536.0f, 1920.0f, 1080.0f, ASPECT_MODE_FILL);
    // 在 Fill 模式下，高度必须完全填满屏幕 (1536)，宽度居中超出屏幕消除黑边
    assert(vpFillIpad.height >= 1536.0f);
    assert(vpFillIpad.originY <= 0.0f);

    // 6. 测试视口计算：iPhone 19.5:9 充满全屏 (ASPECT_MODE_FILL)
    // 屏幕为 iPhone 15/16/17 (2556x1179)
    ScreenViewport vpFillIphone = screenCalculateViewport(2556.0f, 1179.0f, 1920.0f, 1080.0f, ASPECT_MODE_FILL);
    // 在 Fill 模式下，宽度必须完全填满屏幕 (2556)，上下居中消除左右黑边
    assert(vpFillIphone.width >= 2556.0f);
    assert(vpFillIphone.originX <= 0.0f);

    printf("[PASS] testScreenAspect (全机型 iPad 4:3 与 iPhone 19.5:9 适配计算成功)\n");
}

static void testActionLayoutDefault(void) {
    actionLayoutInit();
    LayoutConfig cfg = actionGetLayoutConfig();
    assert(cfg.buttonCount >= 8);

    // 寻找攻击键 Y
    const LayoutButtonDef *btnAttack = NULL;
    const LayoutButtonDef *btnGlider = NULL;
    const LayoutButtonDef *btnInteract = NULL;

    for (int i = 0; i < cfg.buttonCount; i++) {
        if (strcmp(cfg.buttons[i].id, "attack_y") == 0) btnAttack = &cfg.buttons[i];
        if (strcmp(cfg.buttons[i].id, "glider_x") == 0) btnGlider = &cfg.buttons[i];
        if (strcmp(cfg.buttons[i].id, "interact_a") == 0) btnInteract = &cfg.buttons[i];
    }

    assert(btnAttack != NULL);
    assert(btnGlider != NULL);
    assert(btnInteract != NULL);

    // 验证普攻键 Y 为大圆按键 (72x72)，锚定在右下角
    assert(btnAttack->anchor == ANCHOR_BOTTOM_RIGHT);
    assert(btnAttack->width == 72.0f);
    assert(btnAttack->height == 72.0f);
    assert(btnAttack->padButton == PAD_BTN_Y);

    // 屏幕尺寸模拟：iPhone 16 Pro (874 x 402 pt)
    UiSafeAreaInsets insets = {0.0f, 59.0f, 21.0f, 59.0f};
    ComputedButtonRect attackRect = actionComputeButtonRect(btnAttack, 874.0f, 402.0f, &insets);
    assert(attackRect.isVisible == true);
    assert(attackRect.width == 72.0f);
    assert(attackRect.height == 72.0f);

    // 常态下：滑翔伞按钮不显示 (不在空中)
    ComputedButtonRect gliderRectNormal = actionComputeButtonRect(btnGlider, 874.0f, 402.0f, &insets);
    assert(gliderRectNormal.isVisible == false);

    // 常态下：拾取互动键显示但不高亮
    ComputedButtonRect interactRectNormal = actionComputeButtonRect(btnInteract, 874.0f, 402.0f, &insets);
    assert(interactRectNormal.isVisible == true);
    assert(interactRectNormal.isHighlighted == false);

    // 触发事件流：模拟进入空中下落状态
    actionDispatchEvents(EVENT_MASK_IN_AIR);
    ComputedButtonRect gliderRectAir = actionComputeButtonRect(btnGlider, 874.0f, 402.0f, &insets);
    // 滞空时：滑翔伞按钮立即动态激活并高亮！
    assert(gliderRectAir.isVisible == true);
    assert(gliderRectAir.isHighlighted == true);

    // 触发事件流：模拟靠近互动拾取物
    actionDispatchEvents(EVENT_MASK_INTERACT);
    ComputedButtonRect interactRectPrompt = actionComputeButtonRect(btnInteract, 874.0f, 402.0f, &insets);
    assert(interactRectPrompt.isVisible == true);
    // 互动键动态高亮并放大
    assert(interactRectPrompt.isHighlighted == true);
    assert(interactRectPrompt.width > btnInteract->width);

    printf("[PASS] testActionLayoutDefault (Unity 锚点定位与动态事件流驱动验证成功)\n");
}

static void testJsonCustomLayout(void) {
    const char *customJson = "{\n"
                             "  \"buttons\": [\n"
                             "    {\n"
                             "      \"id\": \"custom_attack\",\n"
                             "      \"title\": \"斩\",\n"
                             "      \"anchor\": \"bottomRight\",\n"
                             "      \"offset_x\": -110.0,\n"
                             "      \"offset_y\": -110.0,\n"
                             "      \"width\": 80.0,\n"
                             "      \"height\": 80.0,\n"
                             "      \"mapped_button\": \"Y\",\n"
                             "      \"visible_event\": \"always\"\n"
                             "    }\n"
                             "  ]\n"
                             "}";

    bool loaded = actionLayoutLoadFromJson(customJson);
    assert(loaded == true);

    LayoutConfig cfg = actionGetLayoutConfig();
    assert(cfg.buttonCount == 1);
    assert(strcmp(cfg.buttons[0].id, "custom_attack") == 0);
    assert(cfg.buttons[0].width == 80.0f);
    assert(cfg.buttons[0].padButton == PAD_BTN_Y);

    printf("[PASS] testJsonCustomLayout (JSON 动态键位配置解析成功)\n");
}

int main(void) {
    printf("====== 运行屏幕适配与动态 Action 布局单元测试 ======\n");
    testScreenAspect();
    testActionLayoutDefault();
    testJsonCustomLayout();
    printf("====== 所有动态布局单元测试全部通过！ ======\n");
    return 0;
}
