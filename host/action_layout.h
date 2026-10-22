#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_LAYOUT_BUTTONS 24
#define MAX_BUTTON_ID_LEN 32
#define MAX_BUTTON_TITLE_LEN 32

/// Unity 风格 UI 锚点
typedef enum {
    ANCHOR_TOP_LEFT = 0,
    ANCHOR_TOP_CENTER = 1,
    ANCHOR_TOP_RIGHT = 2,
    ANCHOR_MIDDLE_LEFT = 3,
    ANCHOR_CENTER = 4,
    ANCHOR_MIDDLE_RIGHT = 5,
    ANCHOR_BOTTOM_LEFT = 6,
    ANCHOR_BOTTOM_CENTER = 7,
    ANCHOR_BOTTOM_RIGHT = 8
} UiAnchor;

/// 游戏动作事件掩码
typedef enum {
    EVENT_MASK_NONE = 0,
    EVENT_MASK_ALWAYS = (1u << 0),       // 常驻激活
    EVENT_MASK_IN_AIR = (1u << 1),       // 滞空/高空下落 (滑翔伞、下落斩)
    EVENT_MASK_INTERACT = (1u << 2),     // 互动就绪 (拾取、开宝箱、对话、骑马)
    EVENT_MASK_CLIMBING = (1u << 3),     // 攀爬状态
    EVENT_MASK_SWIMMING = (1u << 4),     // 游泳状态
    EVENT_MASK_COMBAT = (1u << 5),       // 战斗/索敌状态
    EVENT_MASK_ULTRAHAND = (1u << 6),    // 究极手/余料建造激活
} ActionEventMask;

/// 手柄映射按键位掩码
typedef enum {
    PAD_BTN_NONE = 0,
    PAD_BTN_A = (1ull << 0),
    PAD_BTN_B = (1ull << 1),
    PAD_BTN_X = (1ull << 2),
    PAD_BTN_Y = (1ull << 3),
    PAD_BTN_L3 = (1ull << 4),
    PAD_BTN_R3 = (1ull << 5),
    PAD_BTN_L = (1ull << 6),
    PAD_BTN_R = (1ull << 7),
    PAD_BTN_ZL = (1ull << 8),
    PAD_BTN_ZR = (1ull << 9),
    PAD_BTN_PLUS = (1ull << 10),
    PAD_BTN_MINUS = (1ull << 11),
    PAD_BTN_D_LEFT = (1ull << 12),
    PAD_BTN_D_UP = (1ull << 13),
    PAD_BTN_D_RIGHT = (1ull << 14),
    PAD_BTN_D_DOWN = (1ull << 15),
    PAD_BTN_SPECIAL_SWITCH = (1ull << 30) // 特殊行为：切怪微脉冲
} PadButtonMask;

/// 单个动态按钮配置
typedef struct {
    char id[MAX_BUTTON_ID_LEN];
    char title[MAX_BUTTON_TITLE_LEN];
    UiAnchor anchor;
    float offsetX;
    float offsetY;
    float width;
    float height;
    uint64_t padButton;
    uint32_t visibleEvents;    // 激活并显示该按钮的事件掩码
    uint32_t highlightEvents;  // 触发高亮放大的事件掩码
    bool isSpecialAction;      // 是否为非直通手柄的特殊事件
} LayoutButtonDef;

/// 屏幕绝对矩形区域
typedef struct {
    float x;
    float y;
    float width;
    float height;
    bool isVisible;
    bool isHighlighted;
} ComputedButtonRect;

/// 安全区边距
typedef struct {
    float top;
    float left;
    float bottom;
    float right;
} UiSafeAreaInsets;

/// 完整的布局描述结构
typedef struct {
    int buttonCount;
    LayoutButtonDef buttons[MAX_LAYOUT_BUTTONS];
} LayoutConfig;

/// 初始化动态布局子系统
void actionLayoutInit(void);

/// 重置为手游经典王者/吃鸡黄金右手布局（Fallback 预设）
void actionLayoutSetDefaultMobile(void);

/// 从 JSON 字符串解析加载键位配置
bool actionLayoutLoadFromJson(const char *jsonString);

/// 触发更新当前游戏事件掩码（事件驱动流）
void actionDispatchEvents(uint32_t activeEventMask);

/// 查询当前游戏事件掩码
uint32_t actionGetActiveEvents(void);

/// 获取当前布局配置
LayoutConfig actionGetLayoutConfig(void);

/// 根据屏幕尺寸与安全区，计算指定按钮的绝对屏幕渲染矩形
ComputedButtonRect actionComputeButtonRect(const LayoutButtonDef *button,
                                           float screenWidth, float screenHeight,
                                           const UiSafeAreaInsets *insets);
