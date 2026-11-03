#pragma once
#include "action_layout.h"
#include <math.h>
#include <string.h>

typedef struct {
    float scale;
    float stickSize;
    float leftStickX;
    float rightStickX;
    float stickY;
    float rightStickY;
    float auxiliaryX;
    float auxiliaryY;
    float auxiliarySize;
    float auxiliaryGap;
    float lookScaleX;
    float lookScaleY;
} TabletTouchLayout;

static inline TabletTouchLayout tabletTouchLayout(float width, float height, const UiSafeAreaInsets *safe) {
    TabletTouchLayout layout = {0};
    if (!safe || !isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) return layout;
    float usableWidth = width - safe->left - safe->right;
    float usableHeight = height - safe->top - safe->bottom;
    if (usableWidth <= 0 || usableHeight <= 0) return layout;
    layout.scale = fminf(1.10f, fmaxf(.92f, usableHeight / 814.0f));
    float scale = layout.scale;
    layout.stickSize = 168 * scale;
    layout.leftStickX = safe->left + 30 * scale;
    layout.rightStickX = width - safe->right - 198 * scale;
    layout.stickY = height - safe->bottom - 274 * scale;
    layout.rightStickY = height - safe->bottom - 224 * scale;
    layout.auxiliaryX = safe->left + 24 * scale;
    layout.auxiliaryY = height - safe->bottom - 468 * scale;
    layout.auxiliarySize = 54 * scale;
    layout.auxiliaryGap = 10 * scale;
    // A quarter-width swipe turns about 215 degrees with the default camera
    // settings. Keep the same angular travel per screen fraction on iPad sizes.
    layout.lookScaleX = 1.8f * 1194.0f / usableWidth;
    layout.lookScaleY = layout.lookScaleX * .82f;
    return layout;
}

static inline LayoutButtonDef tabletTouchButton(LayoutButtonDef button, float scale, bool dualStick) {
    static const struct {
        const char *id;
        float x, y, width, height;
        bool left;
    } BUTTONS[] = {
        {"attack_y", -118, -190, 88, 88, false},
        {"dash_b", -50, -284, 64, 64, false},
        {"jump_x", -50, -103, 64, 64, false},
        {"interact_a", -172, -90, 110, 52, false},
        {"bow_zr", -148, -330, 64, 64, false},
        {"target_lock_zl", 242, -240, 62, 62, true},
        {"switch_target", 242, -148, 58, 58, true},
        {"throw_r", -238, -322, 56, 56, false},
        {"skill_l", -232, -220, 64, 64, false}
    };
    for (unsigned i = 0; i < sizeof BUTTONS / sizeof BUTTONS[0]; i++) {
        if (strcmp(button.id, BUTTONS[i].id)) continue;
        button.anchor = BUTTONS[i].left ? ANCHOR_BOTTOM_LEFT : ANCHOR_BOTTOM_RIGHT;
        button.offsetX = BUTTONS[i].x * scale;
        button.offsetY = (BUTTONS[i].y - (dualStick && !BUTTONS[i].left ? 190 : 0)) * scale;
        button.width = BUTTONS[i].width * scale;
        button.height = BUTTONS[i].height * scale;
        break;
    }
    return button;
}
