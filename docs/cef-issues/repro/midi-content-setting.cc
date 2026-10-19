// SPDX-License-Identifier: MIT
// Standalone public-CEF API reproduction. No SuiteCut browser code is used.
#include <cstdio>
#include <string>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_request_context.h"
#include "include/cef_task.h"

class QuitProbe final : public CefTask {
 public:
  void Execute() override { CefQuitMessageLoop(); }
 private:
  IMPLEMENT_REFCOUNTING(QuitProbe);
};

class MidiSettingProbe final : public CefApp, public CefBrowserProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnContextInitialized() override {
    const auto command = CefCommandLine::GetGlobalCommandLine();
    const bool control = command->HasSwitch("sysex-control");
    const auto type = control ? CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX
                              : CEF_CONTENT_SETTING_TYPE_MIDI;
    std::fprintf(stderr, "Setting %s (%d) to BLOCK on the browser UI thread\n",
                 control ? "MIDI_SYSEX" : "MIDI", static_cast<int>(type));
    std::fflush(stderr);
    CefRequestContext::GetGlobalContext()->SetContentSetting(
        "https://example.test", "https://example.test", type,
        CEF_CONTENT_SETTING_VALUE_BLOCK);
    std::fprintf(stderr, "SetContentSetting returned\n");
    // Let CEF finish asynchronous browser-process startup before shutdown.
    CefPostDelayedTask(TID_UI, new QuitProbe(), 1000);
  }

 private:
  IMPLEMENT_REFCOUNTING(MidiSettingProbe);
};

int main(int argc, char** argv) {
  CefMainArgs args(argc, argv);
  CefRefPtr<MidiSettingProbe> app = new MidiSettingProbe();
  const int subprocess = CefExecuteProcess(args, app, nullptr);
  if (subprocess >= 0) return subprocess;
  const auto command = CefCommandLine::CreateCommandLine();
  command->InitFromArgv(argc, argv);
  const auto profile = command->GetSwitchValue("probe-profile").ToString();
  if (profile.empty() || profile.front() != '/') {
    std::fprintf(stderr, "Pass --probe-profile=/absolute/unique/directory\n");
    return 2;
  }
  CefSettings settings;
  CefString(&settings.root_cache_path) = profile;
  settings.windowless_rendering_enabled = true;
  if (!CefInitialize(args, settings, app, nullptr)) return 3;
  CefRunMessageLoop();
  app = nullptr;
  CefShutdown();
  return 0;
}
