#include "platform/AppIcon.hpp"

#include "ui/EmbeddedData.hpp"

#import <Cocoa/Cocoa.h>

namespace galaxy {

// A program started from a terminal or an IDE has no bundle, so macOS shows the
// generic executable icon. Setting the icon on the application replaces it in
// the Dock and in the Cmd-Tab switcher. The picture already contains the
// template margin and shadow, so it is used as it is.
void applyApplicationIcon(GLFWwindow*) {
    if (kDockIconSize == 0) {
        return;
    }
    @autoreleasepool {
        NSData* data = [NSData dataWithBytes:kDockIconData length:kDockIconSize];
        NSImage* image = [[NSImage alloc] initWithData:data];
        if (image != nil) {
            [NSApp setApplicationIconImage:image];
        }
    }
}

}  // namespace galaxy
