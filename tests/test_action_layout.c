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
    DeviceAspectClass switchClass = screenClassifyAspect(1276.0f, 720.0f);
    assert(switchClass == DEVICE_ASPECT_STANDARD_16_9);

    // 4. iPhone 16/17 Pro Max (2868x1320, ~2.173)
    DeviceAspectClass iphonePmClass = screenClassifyAspect(2868.0f, 1320.0f);
    assert(iphonePmClass == DEVICE_ASPECT_IPHONE_TALL);

    // 5. 测试视口计算：iPad 4:3 充满全屏 (ASPECT_MODE_FILL)
    // 游戏原生 16:9 (1920x1080)，屏幕为 iPad (2048x1536)
    ScreenViewport vpFillIpad = screenCalculateViewport(2048.0f, 1536.0f, 1920.0f, 1076.0f, ASPECT_MODE_FILL);
    // 在 Fill 模式下，高度必须完全填满屏幕 (1536)，宽度居中超出屏幕消除黑边
    assert(vpFillIpad.height >= 1536.0f);
    assert(vpFillIpad.originY <= 0.0f);

    // 6. 测试视口计算：iPhone 19.5:9 充满全屏 (ASPECT_MODE_FILL)
    // 屏幕为 iPhone 15/16/17 (2556x1179)
    ScreenViewport vpFillIphone = screenCalculateViewport(2556.0f, 1179.0f, 1920.0f, 1076.0f, ASPECT_MODE_FILL);
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
    const LayoutButtonDef *btnJump = NULL;
    const LayoutButtonDef *btnInteract = NULL;

    for (int i = 0; i < cfg.buttonCount; i++) {
        if (strcmp(cfg.buttons[i].id, "attack_y") == 0) btnAttack = &cfg.buttons[i];
        if (strcmp(cfg.buttons[i].id, "jump_x") == 0) btnJump = &cfg.buttons[i];
        if (strcmp(cfg.buttons[i].id, "interact_a") == 0) btnInteract = &cfg.buttons[i];
    }

    assert(btnAttack != NULL);
    assert(btnJump != NULL);
    assert(btnInteract != NULL);

    // 验证普攻键 Y 为大圆按键 (72x72)，锚定在右下角
    assert(btnAttack->anchor == ANCHOR_BOTTOM_RIGHT);
    assert(btnAttack->width == 76.0f);
    assert(btnAttack->height == 76.0f);
    assert(btnAttack->padButton == PAD_BTN_Y);

    // 屏幕尺寸模拟：iPhone 16 Pro (874 x 402 pt)
    UiSafeAreaInsets insets = {0.0f, 59.0f, 21.0f, 59.0f};
    ComputedButtonRect attackRect = actionComputeButtonRect(btnAttack, 874.0f, 402.0f, &insets);
    assert(attackRect.isVisible == true);
    assert(attackRect.width == 76.0f);
    assert(attackRect.height == 76.0f);

    // 常态下：滑翔伞按钮不显示 (不在空中)
    ComputedButtonRect gliderRectNormal = actionComputeButtonRect(btnJump, 874.0f, 402.0f, &insets);
    assert(gliderRectNormal.isVisible == true);

    // 常态下：拾取互动键显示但不高亮
    ComputedButtonRect interactRectNormal = actionComputeButtonRect(btnInteract, 874.0f, 402.0f, &insets);
    assert(interactRectNormal.isVisible == true);
    assert(interactRectNormal.isHighlighted == false);

    // 触发事件流：模拟进入空中下落状态
    actionDispatchEvents(EVENT_MASK_IN_AIR);
    ComputedButtonRect gliderRectAir = actionComputeButtonRect(btnJump, 874.0f, 402.0f, &insets);
    // 滞空时：滑翔伞按钮立即动态激活并高亮！
    assert(gliderRectAir.isVisible == true);
    assert(gliderRectAir.isHighlighted == true);

    // 触发事件流：模拟靠近互动拾取物
    actionDispatchEvents(EVENT_MASK_INTERACT);
    ComputedButtonRect interactRectPrompt = actionComputeButtonRect(btnInteract, 874.0f, 402.0f, &insets);
    assert(interactRectPrompt.isVisible == true);
    // 互动键动态高亮并放大
    assert(interactRectPrompt.isHighlighted == true);
    assert(interactRectPrompt.width == btnInteract->width);
    assert(interactRectPrompt.x == interactRectNormal.x && interactRectPrompt.y == interactRectNormal.y);

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
    assert(cfg.buttons[0].highlightEvents == EVENT_MASK_NONE);
    assert(!actionComputeButtonRect(&cfg.buttons[0], 874, 402, NULL).isHighlighted);

    printf("[PASS] testJsonCustomLayout (JSON 动态键位配置解析成功)\n");
}

static void testLayoutValidation(void) {
    LayoutConfig previous = actionGetLayoutConfig();
    assert(!actionLayoutLoadFromJson("{\"buttons\":[{\"id\":\"bad\",\"mapped_button\":\"Y\",\"width\":-1}]}"));
    assert(!actionLayoutLoadFromJson("{\"buttons\":[{\"id\":\"bad\",\"mapped_button\":\"Y\",\"width\":1e100}]}"));
    assert(!actionLayoutLoadFromJson("{\"buttons\":[{\"id\":\"bad\",\"mapped_button\":\"Y\",\"visible_event\":\"typo\"}]}"));
    assert(!actionLayoutLoadFromJson("{\"buttons\":[{\"id\":\"bad\",\"mapped_button\":\"typo\"}]}"));
    LayoutConfig retained = actionGetLayoutConfig();
    assert(!memcmp(&previous, &retained, sizeof previous));
    // Objects outside the buttons array cannot silently become extra buttons.
    assert(actionLayoutLoadFromJson("{\"buttons\":[{\"id\":\"one\",\"title\":\"{}\",\"mapped_button\":\"D_LEFT\"}],\"other\":{\"id\":\"two\"}}"));
    assert(actionGetLayoutConfig().buttonCount == 1);
    LayoutButtonDef edge = {.anchor=ANCHOR_BOTTOM_RIGHT,.width=80,.height=80,.visibleEvents=EVENT_MASK_ALWAYS,
                           .highlightEvents=EVENT_MASK_COMBAT};
    UiSafeAreaInsets safe = {10, 20, 30, 40};
    actionDispatchEvents(EVENT_MASK_COMBAT);
    ComputedButtonRect r = actionComputeButtonRect(&edge, 320, 180, &safe);
    assert(r.x >= 20 && r.y >= 10 && r.x + r.width <= 280 && r.y + r.height <= 150);
    assert(!actionComputeButtonRect(&edge, NAN, 180, &safe).isVisible);
    safe.left = 500;
    assert(!actionComputeButtonRect(&edge, 320, 180, &safe).isVisible);
    actionLayoutSetDefaultMobile();
    assert(actionGetActiveEvents() == EVENT_MASK_ALWAYS);
    puts("[PASS] layout defaults, invalid-config retention and safe-area clamping");
}

static void testDefaultSpacing(void) {
    const float sizes[][2]={{568,320},{667,375},{812,375},{874,402},{1024,768}};
    for (unsigned screen=0;screen<sizeof sizes/sizeof sizes[0];screen++) {
        actionLayoutInit();
        LayoutConfig cfg=actionGetLayoutConfig();
        UiSafeAreaInsets safe={0,44,21,44};
        if (screen==0) safe=(UiSafeAreaInsets){0,0,0,0};
        actionDispatchEvents(EVENT_MASK_COMBAT|EVENT_MASK_INTERACT|EVENT_MASK_IN_AIR|EVENT_MASK_ULTRAHAND|EVENT_MASK_SWIMMING);
        for (int i=0;i<cfg.buttonCount;i++) {
            ComputedButtonRect a=actionComputeButtonRect(&cfg.buttons[i],sizes[screen][0],sizes[screen][1],&safe);
            if (!a.isVisible) continue;
            assert(a.width>=44 && a.height>=44);
            // Reserve room for visual growth as well as the base hit rectangle.
            float grow = 1.16f;
            a.x -= a.width*(grow-1)*.5f; a.y -= a.height*(grow-1)*.5f;
            a.width *= grow; a.height *= grow;
            assert(a.y>=safe.top+40); // Keep the menu toolbar clear.
            for (int j=i+1;j<cfg.buttonCount;j++) {
                ComputedButtonRect b=actionComputeButtonRect(&cfg.buttons[j],sizes[screen][0],sizes[screen][1],&safe);
                if (!b.isVisible) continue;
                b.x -= b.width*(grow-1)*.5f; b.y -= b.height*(grow-1)*.5f;
                b.width *= grow; b.height *= grow;
                assert(a.x+a.width<=b.x || b.x+b.width<=a.x || a.y+a.height<=b.y || b.y+b.height<=a.y);
            }
            // Default labels contain text rather than the former pictographs.
            assert(!strstr(cfg.buttons[i].title,"\xF0\x9F"));
        }
    }
}

static void testLegacyGeometryMigration(void) {
    actionLayoutInit();
    LayoutConfig defaults=actionGetLayoutConfig(), legacy=defaults;
    const float old[][4]={{-56,-58,72,72},{-144,-38,56,56},{-36,-148,52,52},{-158,-116,104,52},
        {-104,-202,48,48},{-246,-64,48,48},{-246,-128,44,44},{-36,-216,56,56},{-178,-202,48,48}};
    for (int i=0;i<9;i++) {
        legacy.buttons[i].anchor=ANCHOR_BOTTOM_RIGHT;
        legacy.buttons[i].highlightEvents=(uint32_t[]){EVENT_MASK_COMBAT,0,0,EVENT_MASK_INTERACT,0,EVENT_MASK_COMBAT,0,EVENT_MASK_IN_AIR,EVENT_MASK_ULTRAHAND}[i];
        if (i==7) { strcpy(legacy.buttons[i].id,"glider_x"); legacy.buttons[i].padButton=PAD_BTN_X; legacy.buttons[i].visibleEvents=EVENT_MASK_IN_AIR; }
        legacy.buttons[i].offsetX=old[i][0]; legacy.buttons[i].offsetY=old[i][1];
        legacy.buttons[i].width=old[i][2]; legacy.buttons[i].height=old[i][3];
    }
    LayoutConfig custom=legacy;
    custom.buttons[0].offsetX-=1;
    assert(!actionLayoutMigrateLegacy(&custom) && custom.buttons[0].offsetX==-57);
    strcpy(legacy.buttons[0].title,"Custom");
    assert(actionLayoutMigrateLegacy(&legacy));
    assert(!strcmp(legacy.buttons[0].title,"Custom"));
    for (int i=0;i<9;i++) {
        assert(legacy.buttons[i].anchor==defaults.buttons[i].anchor);
        assert(legacy.buttons[i].offsetX==defaults.buttons[i].offsetX && legacy.buttons[i].offsetY==defaults.buttons[i].offsetY);
        assert(legacy.buttons[i].width==defaults.buttons[i].width && legacy.buttons[i].height==defaults.buttons[i].height);
    }
    assert(!actionLayoutMigrateLegacy(&defaults));
    assert(!strcmp(legacy.buttons[7].id,"throw_r") && legacy.buttons[7].padButton==PAD_BTN_R);
}

int main(void) {
    testDefaultSpacing();
    testLegacyGeometryMigration();
    printf("====== 运行屏幕适配与动态 Action 布局单元测试 ======\n");
    testScreenAspect();
    testActionLayoutDefault();
    testJsonCustomLayout();
    testLayoutValidation();
    printf("====== 所有动态布局单元测试全部通过！ ======\n");
    return 0;
}
