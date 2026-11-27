// SPDX-License-Identifier: MIT
#import <CoreLocation/CoreLocation.h>
#include <cstdlib>
#include "context/context_geolocation_fallback.h"
namespace suitecut {
// CLAuthorizationStatusDenied also covers Location Services being off
// system-wide, so the authorization status alone answers both cases without
// the main-thread locationServicesEnabled query.
NativeSystemLocation NativeSystemLocationStatus() {
  if (const auto forced =
          NativeSystemLocationOverride(std::getenv("SUITECUT_GEOLOCATION_SYSTEM_STATUS")))
    return *forced;
  static CLLocationManager* manager = [[CLLocationManager alloc] init];
  switch (manager.authorizationStatus) {
    case kCLAuthorizationStatusNotDetermined:
      return NativeSystemLocation::kNotDetermined;
    case kCLAuthorizationStatusRestricted:
    case kCLAuthorizationStatusDenied:
      return NativeSystemLocation::kDenied;
    default:
      return NativeSystemLocation::kAllowed;
  }
}
}  // namespace suitecut
