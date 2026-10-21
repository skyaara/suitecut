// SPDX-License-Identifier: MIT
#import <Foundation/Foundation.h>
#include "media_activity.h"
#include <pthread/qos.h>
#include <pthread.h>

namespace suitecut {
namespace {
pthread_t media_main_thread = nullptr;
}
void RegisterMediaMainThread() { media_main_thread = pthread_self(); }
MediaActivity::MediaActivity() {
  @autoreleasepool {
    const auto options = NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical;
    id token = [[NSProcessInfo processInfo] beginActivityWithOptions:options
        reason:@"SuiteCut active browser recording"];
    token_ = [token retain];
    // The media worker depends on the CEF UI thread delivering the next paint.
    // Express that dependency while capture is active, rather than letting a
    // headless app's UI thread compete at inherited batch priority under load.
    if (media_main_thread && !pthread_equal(media_main_thread, pthread_self()))
      ui_override_ = pthread_override_qos_class_start_np(media_main_thread, QOS_CLASS_USER_INTERACTIVE, 0);
  }
}
MediaActivity::~MediaActivity() {
  if (ui_override_) pthread_override_qos_class_end_np(static_cast<pthread_override_t>(ui_override_));
  @autoreleasepool {
    if (token_) {
      [[NSProcessInfo processInfo] endActivity:(id)token_];
      [(id)token_ release];
    }
  }
}
}  // namespace suitecut
