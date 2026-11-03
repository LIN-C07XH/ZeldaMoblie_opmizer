#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/// 屏幕画面宽高比适配模式
typedef enum {
    ASPECT_MODE_FIT = 0,     // 等比适应（保留黑边，完整显示原画面）
    ASPECT_MODE_FILL = 1,    // Equal-scale fill; excess image is cropped, not a wider game FOV.
    ASPECT_MODE_STRETCH = 2, // 拉伸充满
    ASPECT_MODE_NATIVE_WIDE = 3 // native render target and camera projection match the display
} ScreenAspectMode;

/// 视口参数计算结果
typedef struct {
    float originX;
    float originY;
    float width;
    float height;
    float scaleX;
    float scaleY;
} ScreenViewport;

/// 设备屏幕比例分类信息
typedef enum {
    DEVICE_ASPECT_IPAD_4_3 = 0,     // iPad 传统与 Pro 12.9/13 寸 (4:3, 1.333)
    DEVICE_ASPECT_IPAD_MODERN = 1,  // iPad 10 代 / Air / Pro 11 寸 (~1.43)
    DEVICE_ASPECT_STANDARD_16_9 = 2,// Switch / 标准电视显示器 (16:9, 1.778)
    DEVICE_ASPECT_IPHONE_TALL = 3,  // 全面屏 iPhone (19.5:9, 2.167 ~ 2.174，包括 iPhone 15/16/17 Pro/PM)
    DEVICE_ASPECT_ULTRAWIDE = 4     // 超宽带鱼屏 (21:9+)
} DeviceAspectClass;

/// 识别当前屏幕宽高比分类
DeviceAspectClass screenClassifyAspect(float screenWidth, float screenHeight);

/// 根据屏幕尺寸与游戏原分辨率计算最佳出图视口
ScreenViewport screenCalculateViewport(float screenWidth, float screenHeight,
                                       float gameWidth, float gameHeight,
                                       ScreenAspectMode mode);

// Shared by the Metal present encoder and frontend overlays. Display dimensions
// refer to the whole drawable; safe areas belong to controls, never the image.
static inline ScreenViewport screenAspectViewport(float sw, float sh, float gw, float gh, ScreenAspectMode mode) {
    ScreenViewport vp = {0, 0, 0, 0, 1, 1};
    if (!isfinite(sw) || !isfinite(sh) || !isfinite(gw) || !isfinite(gh) || sw<=0 || sh<=0 || gw<=0 || gh<=0) return vp;
    float sx=sw/gw, sy=sh/gh;
    // A native wide camera requires a matching wide render target. Never hide
    // missing native negotiation by stretching a 16:9 image. Equal-scale fill
    // only removes the few pixels introduced by the native 8-pixel alignment.
    if (mode==ASPECT_MODE_NATIVE_WIDE && fabsf((sw/sh)/(gw/gh)-1)<.01f)
        mode=ASPECT_MODE_FILL;
    if (mode==ASPECT_MODE_STRETCH) return (ScreenViewport){0,0,sw,sh,sx,sy};
    float scale=mode==ASPECT_MODE_FILL ? fmaxf(sx,sy) : fminf(sx,sy);
    vp.width=roundf(gw*scale); vp.height=roundf(gh*scale);
    vp.originX=floorf((sw-vp.width)*.5f); vp.originY=floorf((sh-vp.height)*.5f);
    vp.scaleX=vp.scaleY=scale;
    return vp;
}
