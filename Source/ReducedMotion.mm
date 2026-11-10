#import <AppKit/AppKit.h>

bool semitoneShouldReduceMotion();

bool semitoneShouldReduceMotion()
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}
