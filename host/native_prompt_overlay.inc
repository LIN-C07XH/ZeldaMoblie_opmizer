// Host text never enters the game's layout, font buffers or animation groups.
#import <TargetConditionals.h>
#include <string.h>
#include "screen_aspect.h"
#if TARGET_OS_IPHONE
extern int front_screen_aspect_mode(void);
#endif
#if TARGET_OS_IPHONE
#define ZeldaPromptViewBase UIView
#define ZeldaPromptColor UIColor
#define ZeldaPromptFont UIFont
#else
#define ZeldaPromptViewBase NSView
#define ZeldaPromptColor NSColor
#define ZeldaPromptFont NSFont
#endif

@interface ZeldaNativePromptView : ZeldaPromptViewBase {
    InputKeyboardPrompts _prompts;
}
- (void)refreshNativePrompts;
@end
@implementation ZeldaNativePromptView
- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
#if TARGET_OS_IPHONE
        self.opaque = NO;
        self.backgroundColor = UIColor.clearColor;
        self.userInteractionEnabled = NO;
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
#else
        self.wantsLayer = YES;
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
#endif
        inputSetKeyboardPromptOverlayEnabled(true);
    }
    return self;
}
- (void)dealloc { inputSetKeyboardPromptOverlayEnabled(false); }
#if !TARGET_OS_IPHONE
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (NSView *)hitTest:(NSPoint)point { (void)point; return nil; }
#endif
- (void)refreshNativePrompts {
    InputKeyboardPrompts next;
    inputGetKeyboardPrompts(&next);
    if (memcmp(&_prompts, &next, sizeof next)) {
        _prompts = next;
#if TARGET_OS_IPHONE
        [self setNeedsDisplay];
#else
        self.needsDisplay = YES;
#endif
    }
}
- (void)drawRect:(CGRect)dirty {
    (void)dirty;
#if !TARGET_OS_IPHONE
    [[NSColor clearColor] setFill];
    NSRectFillUsingOperation(self.bounds, NSCompositingOperationCopy);
#endif
    CGFloat width = CGRectGetWidth(self.bounds), height = CGRectGetHeight(self.bounds);
#if TARGET_OS_IPHONE
    ScreenAspectMode mode = (ScreenAspectMode)front_screen_aspect_mode();
#else
    ScreenAspectMode mode = ASPECT_MODE_FIT;
#endif
    float imageWidth, imageHeight;
    cameraGetDisplaySize(&imageWidth, &imageHeight);
    ScreenViewport viewport = screenAspectViewport(width, height, imageWidth, imageHeight, mode);
    ScreenViewport canvas = screenAspectViewport(viewport.width, viewport.height, 1920, 1080, ASPECT_MODE_FIT);
    canvas.originX += viewport.originX; canvas.originY += viewport.originY;
    viewport = canvas;
    CGFloat gameWidth = viewport.width, gameHeight = viewport.height;
    CGFloat left = viewport.originX, top = viewport.originY;
    for (uint32_t i = 0; i < _prompts.count; i++) {
        const InputKeyboardPrompt *key = &_prompts.keys[i];
        NSUInteger length = 0; while (length < 7 && key->label[length]) length++;
        if (!length || key->alpha <= 0) continue;
        NSString *text = [[NSString alloc] initWithCharacters:key->label length:length];
        CGFloat capHeight = MAX(10., key->height * gameHeight);
        ZeldaPromptFont *font = [ZeldaPromptFont monospacedSystemFontOfSize:capHeight * .63 weight:
#if TARGET_OS_IPHONE
            UIFontWeightSemibold
#else
            NSFontWeightSemibold
#endif
        ];
        NSDictionary *attributes = @{NSFontAttributeName:font,
            NSForegroundColorAttributeName:[ZeldaPromptColor colorWithWhite:1 alpha:key->alpha]};
        CGSize textSize = [text sizeWithAttributes:attributes];
        CGFloat capWidth = MAX(capHeight, textSize.width + capHeight * .4);
        CGRect rect = CGRectMake(left + key->x * gameWidth - capWidth * .5,
            top + key->y * gameHeight - capHeight * .5, capWidth, capHeight);
#if TARGET_OS_IPHONE
        UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:capHeight * .2];
#else
        NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:capHeight * .2 yRadius:capHeight * .2];
#endif
        [[ZeldaPromptColor colorWithWhite:.04 alpha:.78 * key->alpha] setFill]; [path fill];
        [[ZeldaPromptColor colorWithWhite:1 alpha:.75 * key->alpha] setStroke];
        path.lineWidth = MAX(.5, capHeight / 32.); [path stroke];
        [text drawAtPoint:CGPointMake(CGRectGetMidX(rect) - textSize.width * .5,
            CGRectGetMidY(rect) - textSize.height * .5) withAttributes:attributes];
    }
}
@end
#undef ZeldaPromptViewBase
#undef ZeldaPromptColor
#undef ZeldaPromptFont
