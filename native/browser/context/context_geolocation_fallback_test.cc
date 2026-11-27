// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "context/context_geolocation_fallback.h"
#include <cassert>
#include <cstdio>
#include <string>
using suitecut::NativeGeolocationFallbackCoordinates;
using suitecut::NativeGeolocationFallbackDecide;
using suitecut::NativeGeolocationFallbackNeeded;
using suitecut::NativeSystemLocation;

static void OnlySystemDenialNeedsTheFallback() {
  for (const auto system : {NativeSystemLocation::kAllowed, NativeSystemLocation::kNotDetermined,
                            NativeSystemLocation::kDenied, NativeSystemLocation::kServicesOff}) {
    const bool denied =
        system == NativeSystemLocation::kDenied || system == NativeSystemLocation::kServicesOff;
    assert(NativeGeolocationFallbackNeeded(system, true, true) == denied);
    // Without a grant the page is denied natively too; without an emulated
    // position there is nothing to answer with.
    assert(!NativeGeolocationFallbackNeeded(system, false, true));
    assert(!NativeGeolocationFallbackNeeded(system, true, false));
    assert(!NativeGeolocationFallbackNeeded(system, false, false));
  }
}

static void DecideSplitsWildcardAndOrigins() {
  const NativeGeolocationFallbackCoordinates where{37.5, -122.25, 7};
  auto state = NativeGeolocationFallbackDecide(NativeSystemLocation::kDenied, where,
                                               {"*", "https://a.test", ""});
  assert(state.active && state.coordinates && state.any_origin);
  assert(state.origins.size() == 1 && state.origins[0] == "https://a.test");
  assert(state.latitude == 37.5 && state.longitude == -122.25 && state.accuracy == 7);

  state = NativeGeolocationFallbackDecide(NativeSystemLocation::kAllowed, where, {"*"});
  assert(!state.active && state.coordinates);

  // A cleared emulation or a revoked grant turns an installed fallback off;
  // the state still says which, so watchers get the native error.
  state = NativeGeolocationFallbackDecide(NativeSystemLocation::kDenied, std::nullopt,
                                          {"https://a.test"});
  assert(!state.active && !state.coordinates && state.origins.size() == 1);
  state = NativeGeolocationFallbackDecide(NativeSystemLocation::kServicesOff, where, {});
  assert(!state.active && state.coordinates && !state.any_origin && state.origins.empty());
}

static void StateJsonIsExactAndEscaped() {
  auto state = NativeGeolocationFallbackDecide(NativeSystemLocation::kDenied,
                                               NativeGeolocationFallbackCoordinates{1.5, -2, 0},
                                               {"https://a.test", "http://b\"</x>"});
  const auto json = suitecut::NativeGeolocationFallbackStateJson(state);
  assert(json ==
         "{\"active\":true,\"coords\":{\"latitude\":1.5,\"longitude\":-2,\"accuracy\":0},"
         "\"any\":false,\"origins\":[\"https://a.test\",\"http://b\\\"\\u003c/x\\u003e\"]}");
  state = NativeGeolocationFallbackDecide(NativeSystemLocation::kDenied, std::nullopt, {"*"});
  assert(suitecut::NativeGeolocationFallbackStateJson(state) ==
         "{\"active\":false,\"coords\":null,\"any\":true,\"origins\":[]}");
  // Doubles keep full precision.
  state = NativeGeolocationFallbackDecide(
      NativeSystemLocation::kDenied, NativeGeolocationFallbackCoordinates{37.7749, -122.4194, 7},
      {"*"});
  assert(suitecut::NativeGeolocationFallbackStateJson(state).find(
             "\"latitude\":37.774900000000002") != std::string::npos);
}

static void OverrideNamesAreExact() {
  using suitecut::NativeSystemLocationOverride;
  assert(!NativeSystemLocationOverride(nullptr));
  assert(!NativeSystemLocationOverride(""));
  assert(!NativeSystemLocationOverride("Denied"));
  assert(NativeSystemLocationOverride("allowed") == NativeSystemLocation::kAllowed);
  assert(NativeSystemLocationOverride("not-determined") == NativeSystemLocation::kNotDetermined);
  assert(NativeSystemLocationOverride("denied") == NativeSystemLocation::kDenied);
  assert(NativeSystemLocationOverride("services-off") == NativeSystemLocation::kServicesOff);
}

static void ScriptCarriesTokenAndState() {
  const auto& token = suitecut::NativeGeolocationFallbackToken();
  assert(token.size() == 32 && token.find_first_not_of("0123456789abcdef") == std::string::npos);
  assert(&token == &suitecut::NativeGeolocationFallbackToken());
  const std::string state = "{\"active\":false,\"coords\":null,\"any\":false,\"origins\":[]}";
  const auto script = suitecut::NativeGeolocationFallbackScript(token, state);
  assert(script.find("__SUITECUT_") == std::string::npos);
  assert(script.find("const token=\"" + token + "\",incoming=" + state + ",unavailable=\"\";") !=
         std::string::npos);
  assert(script.starts_with("(()=>{'use strict';") && script.ends_with("})()"));
}

int main() {
  OnlySystemDenialNeedsTheFallback();
  DecideSplitsWildcardAndOrigins();
  StateJsonIsExactAndEscaped();
  OverrideNamesAreExact();
  ScriptCarriesTokenAndState();
  std::puts("context geolocation fallback: ok");
}
