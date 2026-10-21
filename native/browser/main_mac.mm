// SPDX-License-Identifier: MIT
#import <Cocoa/Cocoa.h>
#include "include/cef_application_mac.h"
#include "include/wrapper/cef_library_loader.h"
#include "media_pipeline.h"
#include "media_activity.h"

#include "process/main.h"

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
  // The supervised encoder is a native process helper, not a browser. Dispatch
  // it before mapping CEF or creating NSApplication so the first paint does not
  // wait behind browser startup and framework/sandbox work a second time.
  const int encoder = suitecut::MediaEncoderHelperMain(argc, argv);
  if (encoder >= 0) return encoder;
  suitecut::RegisterMediaMainThread();
  @autoreleasepool {
    CefScopedLibraryLoader loader;
    if (!loader.LoadInMain()) return 1;
    [SuiteCutApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    return RunSuiteCutBrowser(argc, argv);
  }
}
