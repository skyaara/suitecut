// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "context_permissions.h"
#include <cassert>
#include <cstdio>
static void CapturedSurfacePermission(){
  using namespace suitecut;
  const auto* actual=NativePermission("captured-surface-control");
  assert(actual&&actual->flags==CEF_PERMISSION_TYPE_CAPTURED_SURFACE_CONTROL);
  assert(actual->count==1&&actual->settings[0]==CEF_CONTENT_SETTING_TYPE_CAPTURED_SURFACE_CONTROL);
  const auto denied=NativePermissionSettings({});
  assert(denied.at(CEF_CONTENT_SETTING_TYPE_CAPTURED_SURFACE_CONTROL)==CEF_CONTENT_SETTING_VALUE_BLOCK);
  const auto granted=NativePermissionSettings({"captured-surface-control"});
  assert(granted.at(CEF_CONTENT_SETTING_TYPE_CAPTURED_SURFACE_CONTROL)==CEF_CONTENT_SETTING_VALUE_ALLOW);
  assert(granted.at(CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA)==CEF_CONTENT_SETTING_VALUE_BLOCK);
  const auto unrelated=NativePermissionSettings({"geolocation"});
  assert(unrelated.at(CEF_CONTENT_SETTING_TYPE_GEOLOCATION)==CEF_CONTENT_SETTING_VALUE_ALLOW);
  assert(unrelated.at(CEF_CONTENT_SETTING_TYPE_CAPTURED_SURFACE_CONTROL)==CEF_CONTENT_SETTING_VALUE_BLOCK);
  const auto intrinsic=NativePermissionSettings({"clipboard-write","screen-wake-lock"});
  assert(intrinsic==denied); // Intrinsic names must not acquire broader grants.
}
int main(int argc,char**argv){
  CapturedSurfacePermission();
  assert(!suitecut::NativePermissionRequestGranted(0,0));
  assert(!suitecut::NativePermissionRequestGranted(0,UINT32_MAX));
  assert(suitecut::NativePermissionRequestGranted(CEF_PERMISSION_TYPE_CAMERA_STREAM,CEF_PERMISSION_TYPE_CAMERA_STREAM));
  assert(!suitecut::NativePermissionRequestGranted(CEF_PERMISSION_TYPE_CAMERA_STREAM|CEF_PERMISSION_TYPE_MIC_STREAM,CEF_PERMISSION_TYPE_CAMERA_STREAM));
  if(argc==2&&std::string(argv[1])=="--captured-surface-only"){
    std::puts("Native captured-surface permission mapping: focused control PASS");return 0;
  }
  assert(argc==1);
  using namespace suitecut;
  assert(NativePermission("storage-access"));assert(NativePermission("screen-wake-lock"));assert(!NativePermission("foo"));
  const auto denied=NativePermissionSettings({});for(const auto&[type,value]:denied){(void)type;assert(value==CEF_CONTENT_SETTING_VALUE_BLOCK);}
  const auto granted=NativePermissionSettings({"geolocation","notifications"});assert(granted.at(CEF_CONTENT_SETTING_TYPE_GEOLOCATION)==CEF_CONTENT_SETTING_VALUE_ALLOW);assert(granted.at(CEF_CONTENT_SETTING_TYPE_NOTIFICATIONS)==CEF_CONTENT_SETTING_VALUE_ALLOW);assert(granted.at(CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA)==CEF_CONTENT_SETTING_VALUE_BLOCK);
  // Native aliases OR grants rather than letting a later alias revoke them.
  const auto aliases=NativePermissionSettings({"accelerometer","midi-sysex","local-network-access"});assert(aliases.at(CEF_CONTENT_SETTING_TYPE_SENSORS)==CEF_CONTENT_SETTING_VALUE_ALLOW);assert(!aliases.contains(CEF_CONTENT_SETTING_TYPE_MIDI));assert(aliases.at(CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX)==CEF_CONTENT_SETTING_VALUE_ALLOW);assert(aliases.at(CEF_CONTENT_SETTING_TYPE_LOCAL_NETWORK)==CEF_CONTENT_SETTING_VALUE_ALLOW);assert(aliases.at(CEF_CONTENT_SETTING_TYPE_LOOPBACK_NETWORK)==CEF_CONTENT_SETTING_VALUE_ALLOW);
  assert(!NativePermission("midi"));
  assert(NativePermissionError("midi")=="Native permission override unsupported by pinned CEF156: midi");
  assert(NativePermissionError("idle-detection").empty());
  assert(NativePermissionError("foo")=="Unknown permission: foo");
  const auto* sysex=NativePermission("midi-sysex");
  assert(sysex&&sysex->flags==CEF_PERMISSION_TYPE_MIDI_SYSEX&&sysex->count==1);
  const auto combined=NativePermissionSettings({"midi-sysex"});
  assert(combined.at(CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX)==CEF_CONTENT_SETTING_VALUE_ALLOW);
  assert(denied.at(CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX)==CEF_CONTENT_SETTING_VALUE_BLOCK);
  assert(!combined.contains(CEF_CONTENT_SETTING_TYPE_MIDI)&&!denied.contains(CEF_CONTENT_SETTING_TYPE_MIDI));
  // Even the raw policy helper cannot emit unsafe MIDI on a mixed input. The
  // actual public ingress rejects that name before mutating context state.
  assert(!NativePermissionSettings({"midi","midi-sysex","idle-detection"}).contains(CEF_CONTENT_SETTING_TYPE_MIDI));
  for(const auto& definition:NativePermissionDefinitions())for(size_t i=0;i<definition.count;++i)
    assert(definition.settings[i]!=CEF_CONTENT_SETTING_TYPE_MIDI);
  for(int i=0;i<100000;++i){const auto cycle=NativePermissionSettings(i%2?std::set<std::string>{"geolocation","storage-access"}:std::set<std::string>{});assert(cycle.size()==denied.size());assert(cycle.at(CEF_CONTENT_SETTING_TYPE_GEOLOCATION)==(i%2?CEF_CONTENT_SETTING_VALUE_ALLOW:CEF_CONTENT_SETTING_VALUE_BLOCK));}
}
