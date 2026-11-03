// Run as a simulator app. Dispatch through UIControl's SDK selectors: misspelled
// tracking overrides otherwise compile successfully but receive no real touches.
#import <UIKit/UIKit.h>
#include <assert.h>
#include <math.h>
#include <objc/runtime.h>
#include "touch_controls.inc"

@interface TouchPoint : NSObject
@property CGPoint point;
@end
@implementation TouchPoint
- (CGPoint)locationInView:(UIView *)view { (void)view; return self.point; }
@end
@interface StickProbe : NSObject
@property unsigned changes;
- (void)changed:(UIControl *)sender;
@end
@implementation StickProbe
- (void)changed:(UIControl *)sender { (void)sender; self.changes++; }
@end

static void testTrackingContract(void) {
    SEL callbacks[] = {@selector(beginTrackingWithTouch:withEvent:),
        @selector(continueTrackingWithTouch:withEvent:), @selector(endTrackingWithTouch:withEvent:),
        @selector(cancelTrackingWithEvent:)};
    for (unsigned i=0;i<sizeof callbacks/sizeof callbacks[0];i++)
        assert(class_getMethodImplementation(ZeldaTouchStick.class,callbacks[i]) !=
               class_getMethodImplementation(UIControl.class,callbacks[i]));
    ZeldaTouchStick *stick=[[ZeldaTouchStick alloc] initWithFrame:CGRectMake(0,0,128,128)];
    StickProbe *probe=[StickProbe new];
    [stick addTarget:probe action:@selector(changed:) forControlEvents:UIControlEventValueChanged];
    UIControl *control=stick;
    TouchPoint *touch=[TouchPoint new]; touch.point=CGPointMake(100,64);
    assert([control beginTrackingWithTouch:(id)touch withEvent:nil]);
    assert(stick.axes.x>.9 && stick.axes.y==0 && probe.changes==1);
    touch.point=CGPointMake(256,-128);
    assert([control continueTrackingWithTouch:(id)touch withEvent:nil]);
    assert(fabs(hypot(stick.axes.x,stick.axes.y)-1)<.00001 && probe.changes==2);
    [control endTrackingWithTouch:(id)touch withEvent:nil];
    assert(CGPointEqualToPoint(stick.axes,CGPointZero) && probe.changes==3);
    [control beginTrackingWithTouch:(id)touch withEvent:nil];
    [control cancelTrackingWithEvent:nil];
    assert(CGPointEqualToPoint(stick.axes,CGPointZero) && probe.changes==5);
    fprintf(stderr,"PASS: UIKit tracking selectors, value delivery, radial clamp, release and cancellation\n");
}
@interface TrackingTestDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation TrackingTestDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; testTrackingContract(); exit(0);
}
@end
int main(int argc,char **argv) {
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(TrackingTestDelegate.class)); }
}
