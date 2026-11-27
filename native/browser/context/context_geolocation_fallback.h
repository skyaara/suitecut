// SPDX-License-Identifier: MIT
#pragma once
// Geolocation emulation when macOS denies location.
//
// Chromium on macOS (GeolocationPermissionContextSystem) turns a granted
// geolocation content setting into BLOCK while the system denies location to
// the app, or Location Services is off. DevTools permission overrides would
// skip that check, but CEF cannot address a CEF request context's profile
// with Browser.setPermission (CEF issue #4379, also in CEF 158:
// https://github.com/chromiumembedded/cef/issues/4379). Once that is fixed,
// replace this fallback with a per-context DevTools permission override and
// remove it. Until then, in exactly that state (context grant, an emulated
// position, system denial) the page gets a JavaScript fallback that answers
// navigator.geolocation and permissions.query from the emulated position.
// When the system allows location, the native path is untouched.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>
namespace suitecut {
enum class NativeSystemLocation { kAllowed, kNotDetermined, kDenied, kServicesOff };

// Only a system denial blocks a granted request. Not-determined reaches the
// ordinary permission prompt, which the engine answers from the grant.
inline bool NativeGeolocationFallbackNeeded(NativeSystemLocation system, bool coordinates,
                                            bool granted) {
  return coordinates && granted &&
         (system == NativeSystemLocation::kDenied || system == NativeSystemLocation::kServicesOff);
}

// SUITECUT_GEOLOCATION_SYSTEM_STATUS lets tests stand in for the system answer.
inline std::optional<NativeSystemLocation> NativeSystemLocationOverride(const char* value) {
  if (!value) return std::nullopt;
  const std::string_view text(value);
  if (text == "allowed") return NativeSystemLocation::kAllowed;
  if (text == "not-determined") return NativeSystemLocation::kNotDetermined;
  if (text == "denied") return NativeSystemLocation::kDenied;
  if (text == "services-off") return NativeSystemLocation::kServicesOff;
  return std::nullopt;
}

// Reads macOS CoreLocation on the UI thread (system_location_mac.mm).
// Elsewhere Chromium has no OS-level geolocation check.
#if defined(__APPLE__)
NativeSystemLocation NativeSystemLocationStatus();
#else
inline NativeSystemLocation NativeSystemLocationStatus() {
  return NativeSystemLocationOverride(std::getenv("SUITECUT_GEOLOCATION_SYSTEM_STATUS"))
      .value_or(NativeSystemLocation::kAllowed);
}
#endif

struct NativeGeolocationFallbackState {
  bool active = false, coordinates = false;
  double latitude = 0, longitude = 0, accuracy = 0;
  bool any_origin = false;  // granted for every HTTP(S) top-level origin
  std::vector<std::string> origins;
};
struct NativeGeolocationFallbackCoordinates {
  double latitude = 0, longitude = 0, accuracy = 0;
};

// granted lists the context's geolocation grant origins; "*" grants every
// HTTP(S) origin. The page decides per frame from its top-level origin, as
// Chromium does for geolocation.
inline NativeGeolocationFallbackState NativeGeolocationFallbackDecide(
    NativeSystemLocation system, const std::optional<NativeGeolocationFallbackCoordinates>& coords,
    const std::vector<std::string>& granted) {
  NativeGeolocationFallbackState state;
  state.coordinates = coords.has_value();
  if (coords) {
    state.latitude = coords->latitude;
    state.longitude = coords->longitude;
    state.accuracy = coords->accuracy;
  }
  for (const auto& origin : granted) {
    if (origin == "*")
      state.any_origin = true;
    else if (!origin.empty())
      state.origins.push_back(origin);
  }
  state.active = NativeGeolocationFallbackNeeded(
      system, state.coordinates, state.any_origin || !state.origins.empty());
  return state;
}

inline std::string NativeGeolocationJsonString(std::string_view text) {
  std::string out = "\"";
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20 || c == 0x7f || c == '<' || c == '>' || c == '&') {
      char escape[7];
      std::snprintf(escape, sizeof escape, "\\u%04x", c);
      out += escape;
    } else
      out += static_cast<char>(c);
  }
  return out + "\"";
}
inline std::string NativeGeolocationJsonNumber(double value) {
  if (!std::isfinite(value)) return "0";
  char text[32];
  std::snprintf(text, sizeof text, "%.17g", value);
  return text;
}
inline std::string NativeGeolocationFallbackStateJson(const NativeGeolocationFallbackState& state) {
  std::string out = "{\"active\":";
  out += state.active ? "true" : "false";
  out += ",\"coords\":";
  if (state.coordinates)
    out += "{\"latitude\":" + NativeGeolocationJsonNumber(state.latitude) +
           ",\"longitude\":" + NativeGeolocationJsonNumber(state.longitude) +
           ",\"accuracy\":" + NativeGeolocationJsonNumber(state.accuracy) + "}";
  else
    out += "null";
  out += ",\"any\":";
  out += state.any_origin ? "true" : "false";
  out += ",\"origins\":[";
  for (size_t i = 0; i < state.origins.size(); ++i) {
    if (i) out += ',';
    out += NativeGeolocationJsonString(state.origins[i]);
  }
  return out + "]}";
}

// One random token per browser process. The installed fallback recognises it
// as the first argument of its getCurrentPosition and takes the second as its
// new state; any other call behaves as the API does.
inline const std::string& NativeGeolocationFallbackToken() {
  static const std::string token = [] {
    std::random_device device;
    std::string out;
    static constexpr char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 4; ++i) {
      const uint32_t word = device();
      for (int shift = 28; shift >= 0; shift -= 4) out += kHex[(word >> shift) & 0xf];
    }
    return out;
  }();
  return token;
}

// The page installer for a state JSON (context_geolocation_fallback.cc).
std::string NativeGeolocationFallbackScript(const std::string& token,
                                            const std::string& state_json);
}  // namespace suitecut
