// UIKit controls deliberately have no feedback generator or haptic engine.
// Game rumble belongs exclusively to the phone/controller output bridge.
@interface ZeldaTouchButton : UIButton
@property (nonatomic) BOOL contextActive;
@end
@implementation ZeldaTouchButton
- (void)refreshAppearance {
    CGFloat scale = self.highlighted ? 1.16 : self.contextActive ? 1.10 : 1;
    UIColor *color = self.highlighted ? [UIColor colorWithRed:.35 green:.85 blue:1 alpha:.95] :
        self.contextActive ? [UIColor colorWithRed:.15 green:.65 blue:.85 alpha:.85] :
        [UIColor colorWithWhite:.05 alpha:.48];
    [UIView animateWithDuration:.12 delay:0 options:UIViewAnimationOptionBeginFromCurrentState|UIViewAnimationOptionAllowUserInteraction animations:^{
        self.transform = CGAffineTransformMakeScale(scale, scale);
        self.backgroundColor = color;
        self.layer.borderColor = [UIColor colorWithWhite:1 alpha:self.contextActive ? .9 : .4].CGColor;
    } completion:nil];
}
- (void)setContextActive:(BOOL)value {
    if (_contextActive == value) return;
    _contextActive = value;
    [self refreshAppearance];
}
- (void)setHighlighted:(BOOL)value {
    if (self.highlighted == value) return;
    [super setHighlighted:value];
    [self refreshAppearance];
}
@end

@interface ZeldaTouchStick : UIControl
@property (nonatomic, readonly) CGPoint axes;
@property (nonatomic, strong) UIView *knob;
- (void)reset;
@end
@implementation ZeldaTouchStick
- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor colorWithWhite:.05 alpha:.25];
        self.layer.borderWidth = 1.5;
        self.layer.borderColor = [UIColor colorWithWhite:1 alpha:.35].CGColor;
        self.knob = [[UIView alloc] initWithFrame:CGRectMake(0,0,52,52)];
        self.knob.userInteractionEnabled = NO;
        self.knob.backgroundColor = [UIColor colorWithWhite:1 alpha:.45];
        self.knob.layer.cornerRadius = 26;
        [self addSubview:self.knob];
        self.accessibilityLabel = @"Move stick";
    }
    return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    self.layer.cornerRadius = MIN(self.bounds.size.width,self.bounds.size.height)*.5;
    CGFloat knobSize = MIN(72, MAX(52, MIN(self.bounds.size.width,self.bounds.size.height)*.4));
    self.knob.bounds = CGRectMake(0, 0, knobSize, knobSize);
    self.knob.layer.cornerRadius = knobSize * .5;
    CGFloat travel = MIN(self.bounds.size.width,self.bounds.size.height)*.30;
    self.knob.center = CGPointMake(CGRectGetMidX(self.bounds)+_axes.x*travel,CGRectGetMidY(self.bounds)-_axes.y*travel);
}
- (void)updateTouch:(UITouch *)touch {
    CGPoint point = [touch locationInView:self];
    CGFloat travel = MAX(1,MIN(self.bounds.size.width,self.bounds.size.height)*.30);
    CGFloat x = (point.x-CGRectGetMidX(self.bounds))/travel, y = (CGRectGetMidY(self.bounds)-point.y)/travel;
    CGFloat length = MAX(1,hypot(x,y));
    _axes = CGPointMake(x/length,y/length);
    [self setNeedsLayout];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}
- (BOOL)beginTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    (void)event;
    [self updateTouch:touch];
    return YES;
}
- (BOOL)continueTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    (void)event;
    [self updateTouch:touch];
    return YES;
}
- (void)reset {
    _axes = CGPointZero;
    [self setNeedsLayout];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}
- (void)endTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event { (void)touch; (void)event; [self reset]; }
- (void)cancelTrackingWithEvent:(UIEvent *)event { (void)event; [self reset]; }
@end
