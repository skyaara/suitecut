// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include "include/internal/cef_types.h"
namespace suitecut {
struct NativePermissionDefinition {
  std::string_view name;
  uint32_t flags;
  std::array<cef_content_setting_types_t,3> settings;
  size_t count;
};
// Native CEF permission/content-setting overrides and accepted intrinsic names.
// Aliases share the actual browser setting; overrides accumulate and deny each
// unlisted mapped type only inside an explicitly overridden origin scope.
// Intrinsic names have no native override: grant/deny/reset do not control them.
inline const auto& NativePermissionDefinitions(){
  static constexpr NativePermissionDefinition values[]={
    {"geolocation",CEF_PERMISSION_TYPE_GEOLOCATION,{CEF_CONTENT_SETTING_TYPE_GEOLOCATION},1},
    {"notifications",CEF_PERMISSION_TYPE_NOTIFICATIONS,{CEF_CONTENT_SETTING_TYPE_NOTIFICATIONS},1},
    {"camera",CEF_PERMISSION_TYPE_CAMERA_STREAM,{CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA},1},
    {"microphone",CEF_PERMISSION_TYPE_MIC_STREAM,{CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_MIC},1},
    {"camera-pan-tilt-zoom",CEF_PERMISSION_TYPE_CAMERA_PAN_TILT_ZOOM|CEF_PERMISSION_TYPE_CAMERA_STREAM,{CEF_CONTENT_SETTING_TYPE_CAMERA_PAN_TILT_ZOOM,CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA},2},
    {"clipboard-read",CEF_PERMISSION_TYPE_CLIPBOARD,{CEF_CONTENT_SETTING_TYPE_CLIPBOARD_READ_WRITE},1},
    {"clipboard-write",0,{},0}, // CEF sanitized write is intrinsically allowed.
    {"captured-surface-control",CEF_PERMISSION_TYPE_CAPTURED_SURFACE_CONTROL,{CEF_CONTENT_SETTING_TYPE_CAPTURED_SURFACE_CONTROL},1},
    // CEF156 exposes MIDI in the enum but its browser content-setting registry
    // does not register it. SetContentSetting(MIDI, even BLOCK) traps. Do not
    // project it for grants, deny-unlisted policy or resets. Ordinary MIDI
    // overrides are rejected below; SysEx keeps its distinct real authority.
    {"midi-sysex",CEF_PERMISSION_TYPE_MIDI_SYSEX,{CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX},1},
    {"idle-detection",CEF_PERMISSION_TYPE_IDLE_DETECTION,{CEF_CONTENT_SETTING_TYPE_IDLE_DETECTION},1},
    {"local-fonts",CEF_PERMISSION_TYPE_LOCAL_FONTS,{CEF_CONTENT_SETTING_TYPE_LOCAL_FONTS},1},
    {"sensors",CEF_PERMISSION_TYPE_SENSORS,{CEF_CONTENT_SETTING_TYPE_SENSORS},1},
    {"accelerometer",CEF_PERMISSION_TYPE_SENSORS,{CEF_CONTENT_SETTING_TYPE_SENSORS},1},
    {"ambient-light-sensor",CEF_PERMISSION_TYPE_SENSORS,{CEF_CONTENT_SETTING_TYPE_SENSORS},1},
    {"gyroscope",CEF_PERMISSION_TYPE_SENSORS,{CEF_CONTENT_SETTING_TYPE_SENSORS},1},
    {"magnetometer",CEF_PERMISSION_TYPE_SENSORS,{CEF_CONTENT_SETTING_TYPE_SENSORS},1},
    {"storage-access",CEF_PERMISSION_TYPE_STORAGE_ACCESS,{CEF_CONTENT_SETTING_TYPE_STORAGE_ACCESS},1},
    {"local-network-access",CEF_PERMISSION_TYPE_LOCAL_NETWORK|CEF_PERMISSION_TYPE_LOOPBACK_NETWORK,{CEF_CONTENT_SETTING_TYPE_LOCAL_NETWORK,CEF_CONTENT_SETTING_TYPE_LOOPBACK_NETWORK},2},
    {"local-network",CEF_PERMISSION_TYPE_LOCAL_NETWORK,{CEF_CONTENT_SETTING_TYPE_LOCAL_NETWORK},1},
    {"loopback-network",CEF_PERMISSION_TYPE_LOOPBACK_NETWORK,{CEF_CONTENT_SETTING_TYPE_LOOPBACK_NETWORK},1},
    {"screen-wake-lock",0,{},0}, // CEF screen locks need no extra permission.
    {"background-sync",0,{CEF_CONTENT_SETTING_TYPE_BACKGROUND_SYNC},1},
    {"payment-handler",0,{CEF_CONTENT_SETTING_TYPE_PAYMENT_HANDLER},1},
    {"persistent-storage",CEF_PERMISSION_TYPE_DISK_QUOTA,{CEF_CONTENT_SETTING_TYPE_PERSISTENT_STORAGE},1},
    {"window-management",CEF_PERMISSION_TYPE_WINDOW_MANAGEMENT,{CEF_CONTENT_SETTING_TYPE_WINDOW_MANAGEMENT},1},
    {"keyboard-lock",CEF_PERMISSION_TYPE_KEYBOARD_LOCK,{CEF_CONTENT_SETTING_TYPE_KEYBOARD_LOCK},1},
    {"pointer-lock",CEF_PERMISSION_TYPE_POINTER_LOCK,{CEF_CONTENT_SETTING_TYPE_POINTER_LOCK},1},
    {"xr-spatial-tracking",CEF_PERMISSION_TYPE_AR_SESSION|CEF_PERMISSION_TYPE_VR_SESSION,{CEF_CONTENT_SETTING_TYPE_AR,CEF_CONTENT_SETTING_TYPE_VR},2},
  };
  return values;
}
inline const NativePermissionDefinition* NativePermission(std::string_view name){for(const auto&entry:NativePermissionDefinitions())if(entry.name==name)return &entry;return nullptr;}
// An unmapped/empty CEF prompt carries no supported permission authority.
inline bool NativePermissionRequestGranted(uint32_t requested,uint32_t granted){
  return requested!=0&&(requested&granted)==requested;
}
inline std::string NativePermissionError(std::string_view name){
  if(name=="midi")return "Native permission override unsupported by pinned CEF156: midi";
  if(!NativePermission(name))return "Unknown permission: "+std::string(name);
  return {};
}
inline std::map<cef_content_setting_types_t,cef_content_setting_values_t> NativePermissionSettings(const std::set<std::string>&names){
  std::map<cef_content_setting_types_t,cef_content_setting_values_t> result;
  for(const auto&entry:NativePermissionDefinitions())for(size_t i=0;i<entry.count;++i){const auto type=entry.settings[i];if(!result.contains(type))result[type]=CEF_CONTENT_SETTING_VALUE_BLOCK;if(names.contains(std::string(entry.name)))result[type]=CEF_CONTENT_SETTING_VALUE_ALLOW;}
  return result;
}
}
