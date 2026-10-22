#include "action_layout.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>

static pthread_mutex_t layoutLock = PTHREAD_MUTEX_INITIALIZER;
static LayoutConfig currentLayout;
static uint32_t currentActiveEvents = EVENT_MASK_ALWAYS;

static UiAnchor parseAnchorName(const char *name) {
    if (!name) return ANCHOR_BOTTOM_RIGHT;
    if (strcmp(name, "topLeft") == 0) return ANCHOR_TOP_LEFT;
    if (strcmp(name, "topCenter") == 0) return ANCHOR_TOP_CENTER;
    if (strcmp(name, "topRight") == 0) return ANCHOR_TOP_RIGHT;
    if (strcmp(name, "middleLeft") == 0) return ANCHOR_MIDDLE_LEFT;
    if (strcmp(name, "center") == 0) return ANCHOR_CENTER;
    if (strcmp(name, "middleRight") == 0) return ANCHOR_MIDDLE_RIGHT;
    if (strcmp(name, "bottomLeft") == 0) return ANCHOR_BOTTOM_LEFT;
    if (strcmp(name, "bottomCenter") == 0) return ANCHOR_BOTTOM_CENTER;
    if (strcmp(name, "bottomRight") == 0) return ANCHOR_BOTTOM_RIGHT;
    return ANCHOR_BOTTOM_RIGHT;
}

static uint64_t parsePadButtonName(const char *name) {
    if (!name) return PAD_BTN_NONE;
    if (strcmp(name, "A") == 0) return PAD_BTN_A;
    if (strcmp(name, "B") == 0) return PAD_BTN_B;
    if (strcmp(name, "X") == 0) return PAD_BTN_X;
    if (strcmp(name, "Y") == 0) return PAD_BTN_Y;
    if (strcmp(name, "L") == 0) return PAD_BTN_L;
    if (strcmp(name, "R") == 0) return PAD_BTN_R;
    if (strcmp(name, "ZL") == 0) return PAD_BTN_ZL;
    if (strcmp(name, "ZR") == 0) return PAD_BTN_ZR;
    if (strcmp(name, "L3") == 0) return PAD_BTN_L3;
    if (strcmp(name, "R3") == 0) return PAD_BTN_R3;
    if (strcmp(name, "PLUS") == 0) return PAD_BTN_PLUS;
    if (strcmp(name, "MINUS") == 0) return PAD_BTN_MINUS;
    if (strcmp(name, "SWITCH") == 0) return PAD_BTN_SPECIAL_SWITCH;
    return PAD_BTN_NONE;
}

static uint32_t parseEventMask(const char *name) {
    if (!name) return EVENT_MASK_ALWAYS;
    if (strcmp(name, "always") == 0) return EVENT_MASK_ALWAYS;
    if (strcmp(name, "in_air") == 0) return EVENT_MASK_IN_AIR;
    if (strcmp(name, "interact") == 0) return EVENT_MASK_INTERACT;
    if (strcmp(name, "climbing") == 0) return EVENT_MASK_CLIMBING;
    if (strcmp(name, "swimming") == 0) return EVENT_MASK_SWIMMING;
    if (strcmp(name, "combat") == 0) return EVENT_MASK_COMBAT;
    if (strcmp(name, "ultrahand") == 0) return EVENT_MASK_ULTRAHAND;
    return EVENT_MASK_ALWAYS;
}

void actionLayoutSetDefaultMobile(void) {
    pthread_mutex_lock(&layoutLock);
    memset(&currentLayout, 0, sizeof(currentLayout));

    // 1. 普攻键 Y (王者平A / 吃鸡开火位置)：大圆按键 (72pt)
    LayoutButtonDef btnAttack = {
        .id = "attack_y",
        .title = "⚔️ 普攻",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -95.0f,
        .offsetY = -95.0f,
        .width = 72.0f,
        .height = 72.0f,
        .padButton = PAD_BTN_Y,
        .visibleEvents = EVENT_MASK_ALWAYS,
        .highlightEvents = EVENT_MASK_COMBAT,
        .isSpecialAction = false
    };

    // 2. 疾跑 / 闪避 / 收刀 B (原神闪避冲刺位)：(56pt)
    LayoutButtonDef btnDash = {
        .id = "dash_b",
        .title = "💨 闪避",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -185.0f,
        .offsetY = -50.0f,
        .width = 56.0f,
        .height = 56.0f,
        .padButton = PAD_BTN_B,
        .visibleEvents = EVENT_MASK_ALWAYS,
        .highlightEvents = EVENT_MASK_NONE,
        .isSpecialAction = false
    };

    // 3. 跳跃 X (吃鸡跳跃翻墙位)：(52pt)
    LayoutButtonDef btnJump = {
        .id = "jump_x",
        .title = "🦘 跳跃",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -45.0f,
        .offsetY = -190.0f,
        .width = 52.0f,
        .height = 52.0f,
        .padButton = PAD_BTN_X,
        .visibleEvents = EVENT_MASK_ALWAYS,
        .highlightEvents = EVENT_MASK_NONE,
        .isSpecialAction = false
    };

    // 4. 拾取 / 互动 A (靠近交互时高亮放大)：(50pt -> 60pt)
    LayoutButtonDef btnInteract = {
        .id = "interact_a",
        .title = "✋ 拾取",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -170.0f,
        .offsetY = -145.0f,
        .width = 50.0f,
        .height = 50.0f,
        .padButton = PAD_BTN_A,
        .visibleEvents = EVENT_MASK_ALWAYS,
        .highlightEvents = EVENT_MASK_INTERACT,
        .isSpecialAction = false
    };

    // 5. 射箭 ZR (开镜开弓位)：(48pt)
    LayoutButtonDef btnBow = {
        .id = "bow_zr",
        .title = "🏹 射箭",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -115.0f,
        .offsetY = -225.0f,
        .width = 48.0f,
        .height = 48.0f,
        .padButton = PAD_BTN_ZR,
        .visibleEvents = EVENT_MASK_ALWAYS,
        .highlightEvents = EVENT_MASK_NONE,
        .isSpecialAction = false
    };

    // 6. 锁敌 ZL (智能注视 Toggle)：(46pt)
    LayoutButtonDef btnLock = {
        .id = "target_lock_zl",
        .title = "⌖ 锁敌",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -245.0f,
        .offsetY = -120.0f,
        .width = 46.0f,
        .height = 46.0f,
        .padButton = PAD_BTN_ZL,
        .visibleEvents = EVENT_MASK_ALWAYS,
        .highlightEvents = EVENT_MASK_COMBAT,
        .isSpecialAction = false
    };

    // 7. 快速切怪 (微脉冲切怪)：(38pt)
    LayoutButtonDef btnSwitch = {
        .id = "switch_target",
        .title = "⇄ 切怪",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -245.0f,
        .offsetY = -175.0f,
        .width = 38.0f,
        .height = 38.0f,
        .padButton = PAD_BTN_SPECIAL_SWITCH,
        .visibleEvents = EVENT_MASK_ALWAYS,
        .highlightEvents = EVENT_MASK_NONE,
        .isSpecialAction = true
    };

    // 8. 动态滑翔伞 (滞空 / 下落时动态浮现并高亮闪烁)：(58pt)
    LayoutButtonDef btnGlider = {
        .id = "glider_x",
        .title = "🪂 滑翔",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -45.0f,
        .offsetY = -260.0f,
        .width = 58.0f,
        .height = 58.0f,
        .padButton = PAD_BTN_X,
        .visibleEvents = EVENT_MASK_IN_AIR,
        .highlightEvents = EVENT_MASK_IN_AIR,
        .isSpecialAction = false
    };

    // 9. 究极手 / 余料建造 L：(46pt)
    LayoutButtonDef btnSkill = {
        .id = "skill_l",
        .title = "🖐 究极",
        .anchor = ANCHOR_BOTTOM_RIGHT,
        .offsetX = -180.0f,
        .offsetY = -225.0f,
        .width = 46.0f,
        .height = 46.0f,
        .padButton = PAD_BTN_L,
        .visibleEvents = EVENT_MASK_ALWAYS,
        .highlightEvents = EVENT_MASK_ULTRAHAND,
        .isSpecialAction = false
    };

    currentLayout.buttons[0] = btnAttack;
    currentLayout.buttons[1] = btnDash;
    currentLayout.buttons[2] = btnJump;
    currentLayout.buttons[3] = btnInteract;
    currentLayout.buttons[4] = btnBow;
    currentLayout.buttons[5] = btnLock;
    currentLayout.buttons[6] = btnSwitch;
    currentLayout.buttons[7] = btnGlider;
    currentLayout.buttons[8] = btnSkill;
    currentLayout.buttonCount = 9;

    pthread_mutex_unlock(&layoutLock);
}

void actionLayoutInit(void) {
    actionLayoutSetDefaultMobile();
}

void actionDispatchEvents(uint32_t activeEventMask) {
    pthread_mutex_lock(&layoutLock);
    // 强制保留常驻事件
    currentActiveEvents = activeEventMask | EVENT_MASK_ALWAYS;
    pthread_mutex_unlock(&layoutLock);
}

uint32_t actionGetActiveEvents(void) {
    pthread_mutex_lock(&layoutLock);
    uint32_t ev = currentActiveEvents;
    pthread_mutex_unlock(&layoutLock);
    return ev;
}

LayoutConfig actionGetLayoutConfig(void) {
    pthread_mutex_lock(&layoutLock);
    LayoutConfig cfg = currentLayout;
    pthread_mutex_unlock(&layoutLock);
    return cfg;
}

static void extractJsonString(const char *block, const char *key, char *out, size_t maxLen) {
    out[0] = '\0';
    const char *pos = strstr(block, key);
    if (!pos) return;
    pos = strchr(pos, ':');
    if (!pos) return;
    pos = strchr(pos, '"');
    if (!pos) return;
    pos++;
    const char *end = strchr(pos, '"');
    if (!end) return;
    size_t len = (size_t)(end - pos);
    if (len >= maxLen) len = maxLen - 1;
    strncpy(out, pos, len);
    out[len] = '\0';
}

static float extractJsonFloat(const char *block, const char *key, float defaultVal) {
    const char *pos = strstr(block, key);
    if (!pos) return defaultVal;
    pos = strchr(pos, ':');
    if (!pos) return defaultVal;
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    char *end = NULL;
    float v = strtof(pos, &end);
    if (end == pos) return defaultVal;
    return v;
}

bool actionLayoutLoadFromJson(const char *jsonString) {
    if (!jsonString || !jsonString[0]) return false;

    pthread_mutex_lock(&layoutLock);
    LayoutConfig newConfig;
    memset(&newConfig, 0, sizeof(newConfig));

    const char *btnStart = strstr(jsonString, "\"buttons\"");
    if (!btnStart) {
        pthread_mutex_unlock(&layoutLock);
        return false;
    }

    const char *curr = strchr(btnStart, '[');
    if (!curr) {
        pthread_mutex_unlock(&layoutLock);
        return false;
    }

    int count = 0;
    while ((curr = strchr(curr, '{')) != NULL && count < MAX_LAYOUT_BUTTONS) {
        const char *objEnd = strchr(curr, '}');
        if (!objEnd) break;

        size_t blockLen = (size_t)(objEnd - curr + 1);
        char *block = (char *)malloc(blockLen + 1);
        if (!block) break;
        strncpy(block, curr, blockLen);
        block[blockLen] = '\0';

        LayoutButtonDef btn;
        memset(&btn, 0, sizeof(btn));

        extractJsonString(block, "\"id\"", btn.id, sizeof(btn.id));
        extractJsonString(block, "\"title\"", btn.title, sizeof(btn.title));

        char anchorStr[32] = {0};
        extractJsonString(block, "\"anchor\"", anchorStr, sizeof(anchorStr));
        btn.anchor = parseAnchorName(anchorStr);

        btn.offsetX = extractJsonFloat(block, "\"offset_x\"", 0.0f);
        btn.offsetY = extractJsonFloat(block, "\"offset_y\"", 0.0f);
        btn.width = extractJsonFloat(block, "\"width\"", 48.0f);
        btn.height = extractJsonFloat(block, "\"height\"", 48.0f);

        char btnStr[32] = {0};
        extractJsonString(block, "\"mapped_button\"", btnStr, sizeof(btnStr));
        btn.padButton = parsePadButtonName(btnStr);

        char eventStr[32] = {0};
        extractJsonString(block, "\"visible_event\"", eventStr, sizeof(eventStr));
        btn.visibleEvents = parseEventMask(eventStr);

        char highlightStr[32] = {0};
        extractJsonString(block, "\"highlight_event\"", highlightStr, sizeof(highlightStr));
        btn.highlightEvents = parseEventMask(highlightStr);

        if (btn.padButton == PAD_BTN_SPECIAL_SWITCH) {
            btn.isSpecialAction = true;
        }

        if (btn.id[0] != '\0') {
            newConfig.buttons[count++] = btn;
        }

        free(block);
        curr = objEnd + 1;
    }

    if (count > 0) {
        newConfig.buttonCount = count;
        currentLayout = newConfig;
        pthread_mutex_unlock(&layoutLock);
        return true;
    }

    pthread_mutex_unlock(&layoutLock);
    return false;
}

ComputedButtonRect actionComputeButtonRect(const LayoutButtonDef *button,
                                           float screenWidth, float screenHeight,
                                           const UiSafeAreaInsets *insets) {
    ComputedButtonRect rect = {0.0f, 0.0f, 0.0f, 0.0f, false, false};
    if (!button || screenWidth <= 0.0f || screenHeight <= 0.0f) return rect;

    UiSafeAreaInsets safe = {0.0f, 0.0f, 0.0f, 0.0f};
    if (insets) safe = *insets;

    float safeW = screenWidth - safe.left - safe.right;
    float safeH = screenHeight - safe.top - safe.bottom;
    float baseX = safe.left;
    float baseY = safe.top;

    switch (button->anchor) {
        case ANCHOR_TOP_LEFT:
            baseX += 0.0f;
            baseY += 0.0f;
            break;
        case ANCHOR_TOP_CENTER:
            baseX += safeW * 0.5f;
            baseY += 0.0f;
            break;
        case ANCHOR_TOP_RIGHT:
            baseX += safeW;
            baseY += 0.0f;
            break;
        case ANCHOR_MIDDLE_LEFT:
            baseX += 0.0f;
            baseY += safeH * 0.5f;
            break;
        case ANCHOR_CENTER:
            baseX += safeW * 0.5f;
            baseY += safeH * 0.5f;
            break;
        case ANCHOR_MIDDLE_RIGHT:
            baseX += safeW;
            baseY += safeH * 0.5f;
            break;
        case ANCHOR_BOTTOM_LEFT:
            baseX += 0.0f;
            baseY += safeH;
            break;
        case ANCHOR_BOTTOM_CENTER:
            baseX += safeW * 0.5f;
            baseY += safeH;
            break;
        case ANCHOR_BOTTOM_RIGHT:
        default:
            baseX += safeW;
            baseY += safeH;
            break;
    }

    uint32_t active = actionGetActiveEvents();
    // 动态事件可见性检查（当 visibleEvents 匹配或为常驻时可见）
    bool visible = (button->visibleEvents & active) != 0;
    // 动态事件高亮检查
    bool highlight = (button->highlightEvents != EVENT_MASK_NONE) &&
                     ((button->highlightEvents & active) != 0);

    float w = button->width;
    float h = button->height;
    if (highlight) {
        // 高亮时放大 1.15 倍，更有手游互动手感
        w *= 1.15f;
        h *= 1.15f;
    }

    rect.width = roundf(w);
    rect.height = roundf(h);
    rect.x = roundf(baseX + button->offsetX - rect.width * 0.5f);
    rect.y = roundf(baseY + button->offsetY - rect.height * 0.5f);
    rect.isVisible = visible;
    rect.isHighlighted = highlight;

    return rect;
}
