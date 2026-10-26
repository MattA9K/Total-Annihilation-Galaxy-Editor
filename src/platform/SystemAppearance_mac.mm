#include "platform/SystemAppearance.hpp"

#import <Cocoa/Cocoa.h>

namespace galaxy {

bool systemPrefersDarkMode() {
    // macOS stores "Dark" in the global preferences when the system look is dark,
    // including when it switches between light and dark by itself with the sun. The
    // key is read here instead of asking the application, because the application's
    // own look may have been set by the user and would hide the system's choice.
    @autoreleasepool {
        NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
        return style != nil && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
    }
}

void applyNativeAppearance(int mode) {
    @autoreleasepool {
        if (mode == 1) {
            NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        } else if (mode == 2) {
            NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        } else {
            NSApp.appearance = nil;
        }
    }
}

}  // namespace galaxy
