// SPDX-License-Identifier: MIT
#import <Cocoa/Cocoa.h>
#include "include/cef_application_mac.h"
#include "include/wrapper/cef_library_loader.h"

int RunSuiteCutBrowser(int argc, char** argv);

@interface SuiteCutApplication : NSApplication <CefAppProtocol> {
  BOOL handling_;
}
@end
@implementation SuiteCutApplication
- (BOOL)isHandlingSendEvent { return handling_; }
- (void)setHandlingSendEvent:(BOOL)value { handling_ = value; }
- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sending;
  [super sendEvent:event];
}
@end

int main(int argc, char** argv) {
  @autoreleasepool {
    CefScopedLibraryLoader loader;
    if (!loader.LoadInMain()) return 1;
    [SuiteCutApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    return RunSuiteCutBrowser(argc, argv);
  }
}
