#include "platform/FileDialog.hpp"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface GalaxySaveFormat : NSObject {
    NSSavePanel* panel_;
    NSArray<NSString*>* extensions_;
}
- (void)setPanel:(NSSavePanel*)panel;
- (void)setExtensions:(NSArray<NSString*>*)extensions;
- (void)changed:(id)sender;
@end

@implementation GalaxySaveFormat

- (void)dealloc {
    [extensions_ release];
    [super dealloc];
}

- (void)setPanel:(NSSavePanel*)panel {
    panel_ = panel;
}

- (void)setExtensions:(NSArray<NSString*>*)extensions {
    [extensions_ release];
    extensions_ = [extensions retain];
}

- (void)changed:(id)sender {
    NSPopUpButton* popup = sender;
    const NSInteger index = popup.indexOfSelectedItem;
    if (panel_ == nil || index < 0 || index >= static_cast<NSInteger>(extensions_.count)) {
        return;
    }
    NSString* extension = extensions_[static_cast<NSUInteger>(index)];
    NSString* name = panel_.nameFieldStringValue;
    if (name.length == 0) {
        name = @"map";
    }
    NSString* base = name.stringByDeletingPathExtension;
    if (base.length == 0) {
        base = name;
    }
    panel_.nameFieldStringValue = [base stringByAppendingPathExtension:extension];
}

@end

namespace galaxy {

std::optional<std::filesystem::path> openMapDialog(const std::filesystem::path& initialDirectory) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.title = @"Open Total Annihilation map";
        panel.prompt = @"Open";

        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (NSString* extension in @[ @"ufo", @"hpi", @"tnt" ]) {
            UTType* type = [UTType typeWithFilenameExtension:extension];
            if (type != nil) {
                [types addObject:type];
            }
        }
        panel.allowedContentTypes = types;

        if (!initialDirectory.empty()) {
            NSString* directory = [NSString stringWithUTF8String:initialDirectory.string().c_str()];
            if (directory != nil) {
                panel.directoryURL = [NSURL fileURLWithPath:directory];
            }
        }

        if ([panel runModal] != NSModalResponseOK || panel.URL.path == nil) {
            return std::nullopt;
        }
        return std::filesystem::path(panel.URL.path.UTF8String);
    }
}

std::optional<std::filesystem::path> saveMapDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.canCreateDirectories = YES;
        panel.title = @"Save Total Annihilation map";
        panel.prompt = @"Save";
        panel.allowsOtherFileTypes = YES;
        if (UTType* data = [UTType typeWithIdentifier:@"public.data"]) {
            panel.allowedContentTypes = @[ data ];
        }

        NSArray<NSString*>* extensions = @[ @"ufo", @"hpi", @"tnt" ];
        NSArray<NSString*>* titles = @[ @"UFO map (.ufo)", @"HPI archive (.hpi)", @"TNT and OTA (.tnt)" ];
        NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(64, 0, 250, 25) pullsDown:NO];
        [popup addItemsWithTitles:titles];
        NSString* suggestedExtension = @"ufo";
        if (!suggestedName.empty()) {
            const std::string extension = suggestedName.extension().string();
            if (extension.size() > 1) {
                suggestedExtension = [NSString stringWithUTF8String:extension.c_str() + 1];
            }
        }
        const NSUInteger selected = [extensions indexOfObject:suggestedExtension.lowercaseString];
        [popup selectItemAtIndex:selected == NSNotFound ? 0 : static_cast<NSInteger>(selected)];

        GalaxySaveFormat* format = [[GalaxySaveFormat alloc] init];
        [format setPanel:panel];
        [format setExtensions:extensions];
        popup.target = format;
        popup.action = @selector(changed:);

        NSTextField* label = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 4, 58, 18)];
        label.stringValue = @"Format:";
        label.editable = NO;
        label.bezeled = NO;
        label.drawsBackground = NO;
        label.alignment = NSTextAlignmentRight;
        NSView* accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 28)];
        [accessory addSubview:label];
        [accessory addSubview:popup];
        [label release];
        [popup release];
        panel.accessoryView = accessory;
        [accessory release];

        if (!suggestedName.empty()) {
            NSString* name = [NSString stringWithUTF8String:suggestedName.filename().string().c_str()];
            if (name != nil) {
                panel.nameFieldStringValue = name;
            }
        }
        if (!initialDirectory.empty()) {
            NSString* directory = [NSString stringWithUTF8String:initialDirectory.string().c_str()];
            if (directory != nil) {
                panel.directoryURL = [NSURL fileURLWithPath:directory];
            }
        }

        const NSInteger response = [panel runModal];
        NSString* chosenExtension = @"ufo";
        if (popup.indexOfSelectedItem >= 0 && popup.indexOfSelectedItem < static_cast<NSInteger>(extensions.count)) {
            chosenExtension = extensions[static_cast<NSUInteger>(popup.indexOfSelectedItem)];
        }
        NSString* chosenPath = panel.URL.path;
        [format release];
        if (response != NSModalResponseOK || chosenPath == nil) {
            return std::nullopt;
        }
        std::filesystem::path result(chosenPath.UTF8String);
        result.replace_extension(chosenExtension.UTF8String);
        return result;
    }
}

std::optional<std::filesystem::path> chooseFolderDialog(
    const std::string& title,
    const std::filesystem::path& initialDirectory) {
    @autoreleasepool {
        // The dialog can appear before any window exists, so bring the app forward first.
        [NSApp activateIgnoringOtherApps:YES];

        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.canCreateDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.prompt = @"Choose";
        if (NSString* text = [NSString stringWithUTF8String:title.c_str()]) {
            panel.title = text;
            panel.message = text;
        }
        if (!initialDirectory.empty()) {
            NSString* directory = [NSString stringWithUTF8String:initialDirectory.string().c_str()];
            if (directory != nil) {
                panel.directoryURL = [NSURL fileURLWithPath:directory];
            }
        }
        if ([panel runModal] != NSModalResponseOK || panel.URL.path == nil) {
            return std::nullopt;
        }
        return std::filesystem::path(panel.URL.path.UTF8String);
    }
}

std::optional<std::filesystem::path> openArchiveDialog(const std::filesystem::path& initialDirectory) {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.title = @"Open archive";
        panel.prompt = @"Open";

        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (NSString* extension in @[ @"hpi", @"ufo", @"ccx", @"gp3", @"gpf" ]) {
            UTType* type = [UTType typeWithFilenameExtension:extension];
            if (type != nil) {
                [types addObject:type];
            }
        }
        panel.allowedContentTypes = types;

        if (!initialDirectory.empty()) {
            NSString* directory = [NSString stringWithUTF8String:initialDirectory.string().c_str()];
            if (directory != nil) {
                panel.directoryURL = [NSURL fileURLWithPath:directory];
            }
        }
        if ([panel runModal] != NSModalResponseOK || panel.URL.path == nil) {
            return std::nullopt;
        }
        return std::filesystem::path(panel.URL.path.UTF8String);
    }
}

std::optional<std::filesystem::path> saveArchiveDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName) {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.canCreateDirectories = YES;
        panel.title = @"Save archive";
        panel.prompt = @"Save";
        panel.allowsOtherFileTypes = YES;
        if (!suggestedName.empty()) {
            NSString* name = [NSString stringWithUTF8String:suggestedName.filename().string().c_str()];
            if (name != nil) {
                panel.nameFieldStringValue = name;
            }
        }
        if (!initialDirectory.empty()) {
            NSString* directory = [NSString stringWithUTF8String:initialDirectory.string().c_str()];
            if (directory != nil) {
                panel.directoryURL = [NSURL fileURLWithPath:directory];
            }
        }
        if ([panel runModal] != NSModalResponseOK || panel.URL.path == nil) {
            return std::nullopt;
        }
        return std::filesystem::path(panel.URL.path.UTF8String);
    }
}

std::vector<std::filesystem::path> chooseFilesDialog(
    const std::string& title,
    const std::filesystem::path& initialDirectory) {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = YES;
        panel.prompt = @"Import";
        if (NSString* text = [NSString stringWithUTF8String:title.c_str()]) {
            panel.title = text;
            panel.message = text;
        }
        if (!initialDirectory.empty()) {
            NSString* directory = [NSString stringWithUTF8String:initialDirectory.string().c_str()];
            if (directory != nil) {
                panel.directoryURL = [NSURL fileURLWithPath:directory];
            }
        }
        std::vector<std::filesystem::path> result;
        if ([panel runModal] != NSModalResponseOK) {
            return result;
        }
        for (NSURL* url in panel.URLs) {
            if (url.path != nil) {
                result.emplace_back(url.path.UTF8String);
            }
        }
        return result;
    }
}

bool alertDialog(
    const std::string& message,
    const std::string& detail,
    const std::string& cancelLabel,
    const std::string& acceptLabel) {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        NSAlert* alert = [[NSAlert alloc] init];
        alert.alertStyle = NSAlertStyleInformational;
        alert.messageText = [NSString stringWithUTF8String:message.c_str()];
        alert.informativeText = [NSString stringWithUTF8String:detail.c_str()];
        // The first button is the default one, on the right. A button named Cancel answers to Esc.
        [alert addButtonWithTitle:[NSString stringWithUTF8String:acceptLabel.c_str()]];
        [alert addButtonWithTitle:[NSString stringWithUTF8String:cancelLabel.c_str()]];
        const NSInteger response = [alert runModal];
        [alert release];
        return response == NSAlertFirstButtonReturn;
    }
}

}  // namespace galaxy
