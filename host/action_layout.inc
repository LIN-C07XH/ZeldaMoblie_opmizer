#include "action_layout.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>

static pthread_mutex_t layoutLock = PTHREAD_MUTEX_INITIALIZER;
static LayoutConfig currentLayout, mobileDefaults;
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
    if (strcmp(name, "D_LEFT") == 0) return PAD_BTN_D_LEFT;
    if (strcmp(name, "D_UP") == 0) return PAD_BTN_D_UP;
    if (strcmp(name, "D_RIGHT") == 0) return PAD_BTN_D_RIGHT;
    if (strcmp(name, "D_DOWN") == 0) return PAD_BTN_D_DOWN;
    if (strcmp(name, "SWITCH") == 0) return PAD_BTN_SPECIAL_SWITCH;
    return PAD_BTN_NONE;
}

static uint32_t parseEventMask(const char *name) {
    if (!name) return EVENT_MASK_NONE;
    if (strcmp(name, "always") == 0) return EVENT_MASK_ALWAYS;
    if (strcmp(name, "in_air") == 0) return EVENT_MASK_IN_AIR;
    if (strcmp(name, "interact") == 0) return EVENT_MASK_INTERACT;
    if (strcmp(name, "climbing") == 0) return EVENT_MASK_CLIMBING;
    if (strcmp(name, "swimming") == 0) return EVENT_MASK_SWIMMING;
    if (strcmp(name, "combat") == 0) return EVENT_MASK_COMBAT;
    if (strcmp(name, "ultrahand") == 0) return EVENT_MASK_ULTRAHAND;
    return EVENT_MASK_NONE;
}

void actionLayoutSetDefaultMobile(void) {
    pthread_mutex_lock(&layoutLock);
    memset(&currentLayout, 0, sizeof(currentLayout));
    currentActiveEvents = EVENT_MASK_ALWAYS;

    // Stable thumb positions. Events change emphasis, never availability.
    LayoutButtonDef buttons[] = {
        {.id="attack_y", .title="攻击", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-114, .offsetY=-98, .width=76, .height=76, .padButton=PAD_BTN_Y,
         .visibleEvents=EVENT_MASK_ALWAYS, .highlightEvents=EVENT_MASK_COMBAT},
        {.id="dash_b", .title="疾跑", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-36, .offsetY=-40, .width=52, .height=52, .padButton=PAD_BTN_B,
         .visibleEvents=EVENT_MASK_ALWAYS, .highlightEvents=EVENT_MASK_CLIMBING|EVENT_MASK_SWIMMING},
        {.id="jump_x", .title="跳跃", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-36, .offsetY=-166, .width=54, .height=54, .padButton=PAD_BTN_X,
         .visibleEvents=EVENT_MASK_ALWAYS, .highlightEvents=EVENT_MASK_IN_AIR},
        {.id="interact_a", .title="确定", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-102, .offsetY=-238, .width=104, .height=48, .padButton=PAD_BTN_A,
         .visibleEvents=EVENT_MASK_ALWAYS, .highlightEvents=EVENT_MASK_INTERACT},
        {.id="bow_zr", .title="射箭", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-194, .offsetY=-132, .width=48, .height=48, .padButton=PAD_BTN_ZR,
         .visibleEvents=EVENT_MASK_ALWAYS},
        {.id="target_lock_zl", .title="锁定", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-194, .offsetY=-54, .width=48, .height=48, .padButton=PAD_BTN_ZL,
         .visibleEvents=EVENT_MASK_ALWAYS, .highlightEvents=EVENT_MASK_COMBAT},
        {.id="switch_target", .title="切换", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-258, .offsetY=-54, .width=44, .height=44, .padButton=PAD_BTN_SPECIAL_SWITCH,
         .visibleEvents=EVENT_MASK_ALWAYS, .isSpecialAction=true},
        {.id="throw_r", .title="投掷", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-258, .offsetY=-132, .width=48, .height=48, .padButton=PAD_BTN_R,
         .visibleEvents=EVENT_MASK_ALWAYS},
        {.id="skill_l", .title="能力", .anchor=ANCHOR_BOTTOM_RIGHT,
         .offsetX=-258, .offsetY=-210, .width=52, .height=52, .padButton=PAD_BTN_L,
         .visibleEvents=EVENT_MASK_ALWAYS, .highlightEvents=EVENT_MASK_ULTRAHAND}
    };
    currentLayout.buttonCount = sizeof buttons / sizeof buttons[0];
    memcpy(currentLayout.buttons, buttons, sizeof buttons);
    mobileDefaults = currentLayout;

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

bool actionLayoutUsesMobileDefaults(void) {
    pthread_mutex_lock(&layoutLock);
    bool matches = currentLayout.buttonCount == mobileDefaults.buttonCount;
    for (int i = 0; matches && i < currentLayout.buttonCount; i++) {
        const LayoutButtonDef *button = &currentLayout.buttons[i];
        const LayoutButtonDef *original = NULL;
        for (int j = 0; j < mobileDefaults.buttonCount; j++)
            if (!strcmp(button->id, mobileDefaults.buttons[j].id)) original = &mobileDefaults.buttons[j];
        matches = original && button->anchor == original->anchor && button->offsetX == original->offsetX &&
            button->offsetY == original->offsetY && button->width == original->width &&
            button->height == original->height && button->padButton == original->padButton;
    }
    pthread_mutex_unlock(&layoutLock);
    return matches;
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
    if (end == pos) return NAN;
    return v;
}

// Locate structural delimiters without mistaking a title's braces for JSON.
static const char *layoutDelimiter(const char *p, char delimiter) {
    bool quoted = false, escaped = false;
    for (; *p; p++) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            else if (*p == '"') quoted = false;
        } else if (*p == '"') quoted = true;
        else if (*p == delimiter) return p;
    }
    return NULL;
}

// Migrate only the exact geometry and mappings of our three shipped presets.
// Real custom layouts and labels are retained; no configuration file is written.
static bool actionLayoutMigrateLegacy(LayoutConfig *cfg) {
    static const float old[3][9][4] = {
        {{-95,-95,72,72},{-185,-50,56,56},{-45,-190,52,52},{-170,-145,104,54},
         {-115,-225,48,48},{-245,-120,46,46},{-245,-175,38,38},{-45,-260,58,58},{-180,-225,46,46}},
        {{-56,-58,72,72},{-144,-38,56,56},{-36,-148,52,52},{-158,-116,104,52},
         {-104,-202,48,48},{-246,-64,48,48},{-246,-128,44,44},{-36,-216,56,56},{-178,-202,48,48}},
        {{-126,-106,80,80},{-48,-62,52,52},{-44,-132,56,56},{-104,-54,120,52},
         {-202,-154,48,48},{-218,-72,48,48},{-280,-72,44,44},{-104,-54,120,52},{-268,-148,52,52}}
    };
    if (cfg->buttonCount != 9 || mobileDefaults.buttonCount != 9) return false;
    int slots[9];
    for (int version=0;version<3;version++) {
        bool matched=true;
        for (int i=0;i<9;i++) {
            const LayoutButtonDef *def=&mobileDefaults.buttons[i];
            slots[i]=-1;
            for (int j=0;j<9;j++) if (!strcmp(cfg->buttons[j].id, i == 7 ? "glider_x" : def->id)) slots[i]=j;
            if (slots[i]<0) { matched=false; break; }
            const LayoutButtonDef *b=&cfg->buttons[slots[i]];
            UiAnchor anchor = version == 2 && (i == 3 || i == 7) ? ANCHOR_MIDDLE_RIGHT : ANCHOR_BOTTOM_RIGHT;
            uint32_t highlights[9] = {EVENT_MASK_COMBAT,0,0,EVENT_MASK_INTERACT,0,EVENT_MASK_COMBAT,0,EVENT_MASK_IN_AIR,EVENT_MASK_ULTRAHAND};
            if (b->anchor!=anchor || b->padButton!=(i == 7 ? PAD_BTN_X : def->padButton) ||
                b->visibleEvents!=(i == 7 ? EVENT_MASK_IN_AIR : EVENT_MASK_ALWAYS) || b->highlightEvents!=highlights[i] ||
                b->offsetX!=old[version][i][0] || b->offsetY!=old[version][i][1] ||
                b->width!=old[version][i][2] || b->height!=old[version][i][3]) { matched=false; break; }
        }
        if (!matched) continue;
        for (int i=0;i<9;i++) {
            LayoutButtonDef *b=&cfg->buttons[slots[i]];
            const LayoutButtonDef *def=&mobileDefaults.buttons[i];
            b->anchor=def->anchor; b->offsetX=def->offsetX; b->offsetY=def->offsetY;
            b->width=def->width; b->height=def->height;
            b->visibleEvents=def->visibleEvents; b->highlightEvents=def->highlightEvents;
            if (i == 7) *b = *def;
        }
        return true;
    }
    return false;
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
    const char *arrayEnd = layoutDelimiter(curr + 1, ']');
    if (!arrayEnd) { pthread_mutex_unlock(&layoutLock); return false; }
    bool valid = true;
    while ((curr = layoutDelimiter(curr + 1, '{')) != NULL && curr < arrayEnd) {
        const char *objEnd = layoutDelimiter(curr + 1, '}');
        if (count == MAX_LAYOUT_BUTTONS || !objEnd || objEnd > arrayEnd) { valid = false; break; }

        size_t blockLen = (size_t)(objEnd - curr + 1);
        char *block = (char *)malloc(blockLen + 1);
        if (!block) { valid = false; break; }
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
        btn.visibleEvents = eventStr[0] ? parseEventMask(eventStr) : EVENT_MASK_ALWAYS;

        char highlightStr[32] = {0};
        extractJsonString(block, "\"highlight_event\"", highlightStr, sizeof(highlightStr));
        btn.highlightEvents = parseEventMask(highlightStr);

        if (btn.padButton == PAD_BTN_SPECIAL_SWITCH) {
            btn.isSpecialAction = true;
        }

        valid = btn.id[0] && btn.padButton != PAD_BTN_NONE && btn.visibleEvents != EVENT_MASK_NONE &&
            (!highlightStr[0] || strcmp(highlightStr, "none") == 0 || btn.highlightEvents != EVENT_MASK_NONE) &&
            isfinite(btn.offsetX) && isfinite(btn.offsetY) && isfinite(btn.width) && isfinite(btn.height) &&
            btn.width > 0 && btn.height > 0;
        for (int i = 0; i < count; i++) if (!strcmp(newConfig.buttons[i].id, btn.id)) valid = false;
        if (valid) newConfig.buttons[count++] = btn;
        free(block);
        if (!valid) break;
        curr = objEnd;
    }

    if (valid && count > 0) {
        newConfig.buttonCount = count;
        if (actionLayoutMigrateLegacy(&newConfig)) fprintf(stderr, "TOUCH_LAYOUT migrated shipped preset geometry\n");
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
    if (!button || !isfinite(screenWidth) || !isfinite(screenHeight) || screenWidth <= 0.0f || screenHeight <= 0.0f ||
        !isfinite(button->offsetX) || !isfinite(button->offsetY) || !isfinite(button->width) || !isfinite(button->height) ||
        button->width <= 0 || button->height <= 0) return rect;

    UiSafeAreaInsets safe = {0.0f, 0.0f, 0.0f, 0.0f};
    if (insets) safe = *insets;
    if (!isfinite(safe.left) || !isfinite(safe.right) || !isfinite(safe.top) || !isfinite(safe.bottom) ||
        safe.left < 0 || safe.right < 0 || safe.top < 0 || safe.bottom < 0) return rect;

    float safeW = screenWidth - safe.left - safe.right;
    float safeH = screenHeight - safe.top - safe.bottom;
    if (safeW < 1 || safeH < 1) return rect;
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

    rect.width = fminf(safeW, fmaxf(1, roundf(w)));
    rect.height = fminf(safeH, fmaxf(1, roundf(h)));
    rect.x = fmaxf(safe.left, fminf(safe.left + safeW - rect.width, roundf(baseX + button->offsetX - rect.width * 0.5f)));
    rect.y = fmaxf(safe.top, fminf(safe.top + safeH - rect.height, roundf(baseY + button->offsetY - rect.height * 0.5f)));
    rect.isVisible = visible;
    rect.isHighlighted = highlight;

    return rect;
}
