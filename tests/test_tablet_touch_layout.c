#include "action_layout.h"
#include "action_layout.inc"
#include "tablet_touch_layout.h"
#include <assert.h>
#include <stdio.h>

static bool overlap(ComputedButtonRect a, ComputedButtonRect b) {
    return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
}

static void testTablet(float width, float height, UiSafeAreaInsets safe, bool dualStick) {
    actionLayoutInit();
    LayoutConfig config = actionGetLayoutConfig();
    assert(actionLayoutUsesMobileDefaults());
    TabletTouchLayout layout = tabletTouchLayout(width, height, &safe);
    ComputedButtonRect leftStick = {layout.leftStickX, layout.stickY, layout.stickSize, layout.stickSize, true, false};
    ComputedButtonRect rightStick = {layout.rightStickX, layout.rightStickY, layout.stickSize, layout.stickSize, true, false};
    ComputedButtonRect rectangles[MAX_LAYOUT_BUTTONS];
    unsigned left = 0, right = 0;
    for (int i = 0; i < config.buttonCount; i++) {
        LayoutButtonDef button = tabletTouchButton(config.buttons[i], layout.scale, dualStick);
        rectangles[i] = actionComputeButtonRect(&button, width, height, &safe);
        ComputedButtonRect rect = rectangles[i];
        assert(rect.isVisible && rect.width >= 50 && rect.height >= 47);
        assert(rect.x >= safe.left && rect.x + rect.width <= width - safe.right);
        assert(rect.y >= safe.top + 48 && rect.y + rect.height <= height - safe.bottom - 48);
        assert(!overlap(rect, leftStick));
        if (dualStick) assert(!overlap(rect, rightStick));
        if (rect.x < width / 2) left++; else right++;
        for (int j = 0; j < i; j++) assert(!overlap(rect, rectangles[j]));
    }
    assert(left == 2 && right == 7);
    assert(layout.auxiliaryY > safe.top + 70);
    assert(layout.auxiliaryY + 3 * layout.auxiliarySize + 2 * layout.auxiliaryGap < layout.stickY);
    assert(layout.stickY + layout.stickSize < height - safe.bottom - 64);
    // Equal screen fractions have equal angular input, independent of event rate.
    float travel = width - safe.left - safe.right;
    assert(fabsf(.25f * travel * layout.lookScaleX - 537.3f) < .001f);
    assert(layout.lookScaleY < layout.lookScaleX && layout.lookScaleY > 0);
    float split = 0;
    for (unsigned i = 0; i < 10; i++) split += .025f * travel * layout.lookScaleX;
    assert(fabsf(split - .25f * travel * layout.lookScaleX) < .001f);
}

int main(void) {
    const float SIZES[][2] = {{1194, 834}, {1366, 1024}, {1024, 768}, {900, 600}};
    for (unsigned i = 0; i < sizeof SIZES / sizeof SIZES[0]; i++)
        for (unsigned dual = 0; dual < 2; dual++)
            testTablet(SIZES[i][0], SIZES[i][1], (UiSafeAreaInsets){0, 0, 20, 0}, dual != 0);
    testTablet(1194, 834, (UiSafeAreaInsets){24, 24, 20, 24}, false);
    actionLayoutInit();
    assert(actionLayoutUsesMobileDefaults());
    assert(actionLayoutLoadFromJson("{\"buttons\":[{\"id\":\"attack_y\",\"title\":\"Custom\",\"mapped_button\":\"Y\",\"offset_x\":-80,\"width\":90,\"height\":90}]}"));
    assert(!actionLayoutUsesMobileDefaults());
    puts("Tablet controls: thumb zones, safe areas, non-overlapping sticks/buttons, custom layout preservation and normalized camera travel passed");
}
