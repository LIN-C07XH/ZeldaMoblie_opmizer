#include "screen_aspect.h"
#include <math.h>

#define ASPECT_EPSILON 0.05f

DeviceAspectClass screenClassifyAspect(float screenWidth, float screenHeight) {
    if (screenHeight <= 0.0f) return DEVICE_ASPECT_STANDARD_16_9;
    float ratio = screenWidth / screenHeight;

    if (ratio < 1.38f) {
        // 4:3 比例 (1.333)，例如 iPad 9.7", 10.2", 12.9", 13"
        return DEVICE_ASPECT_IPAD_4_3;
    } else if (ratio < 1.60f) {
        // 现代 iPad 比例 (~1.43)，例如 iPad 10 代, iPad Pro 11"
        return DEVICE_ASPECT_IPAD_MODERN;
    } else if (ratio < 1.95f) {
        // 标准 16:9 比例 (1.778)
        return DEVICE_ASPECT_STANDARD_16_9;
    } else if (ratio < 2.25f) {
        // 现代 iPhone 全面屏 (19.5:9, 2.167 ~ 2.174)，例如 iPhone 15/16/17 Pro / Pro Max
        return DEVICE_ASPECT_IPHONE_TALL;
    } else {
        // 21:9 或更宽
        return DEVICE_ASPECT_ULTRAWIDE;
    }
}

ScreenViewport screenCalculateViewport(float screenWidth, float screenHeight,
                                       float gameWidth, float gameHeight,
                                       ScreenAspectMode mode) {
    ScreenViewport vp = {0.0f, 0.0f, screenWidth, screenHeight, 1.0f, 1.0f};
    if (screenWidth <= 0.0f || screenHeight <= 0.0f || gameWidth <= 0.0f || gameHeight <= 0.0f) {
        return vp;
    }

    float scaleX = screenWidth / gameWidth;
    float scaleY = screenHeight / gameHeight;

    switch (mode) {
        case ASPECT_MODE_FILL: {
            // 等比全屏充满：取较大缩放倍数，消除 iPad 4:3 上下黑边或 iPhone 19.5:9 左右黑边
            float scale = fmaxf(scaleX, scaleY);
            float scaledW = roundf(gameWidth * scale);
            float scaledH = roundf(gameHeight * scale);
            vp.originX = floorf((screenWidth - scaledW) * 0.5f);
            vp.originY = floorf((screenHeight - scaledH) * 0.5f);
            vp.width = scaledW;
            vp.height = scaledH;
            vp.scaleX = scale;
            vp.scaleY = scale;
            break;
        }
        case ASPECT_MODE_STRETCH: {
            // 强制拉伸充满全屏
            vp.originX = 0.0f;
            vp.originY = 0.0f;
            vp.width = screenWidth;
            vp.height = screenHeight;
            vp.scaleX = scaleX;
            vp.scaleY = scaleY;
            break;
        }
        case ASPECT_MODE_FIT:
        default: {
            // 传统等比居中适应：保留黑边
            float scale = fminf(scaleX, scaleY);
            float scaledW = roundf(gameWidth * scale);
            float scaledH = roundf(gameHeight * scale);
            vp.originX = floorf((screenWidth - scaledW) * 0.5f);
            vp.originY = floorf((screenHeight - scaledH) * 0.5f);
            vp.width = scaledW;
            vp.height = scaledH;
            vp.scaleX = scale;
            vp.scaleY = scale;
            break;
        }
    }

    return vp;
}
