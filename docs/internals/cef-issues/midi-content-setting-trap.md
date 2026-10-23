# Report title

SetContentSetting with public MIDI enum raises SIGTRAP

# Exact proposed issue body

**Describe the bug**

A standalone CEF application calling
`CefRequestContext::SetContentSetting` with the named public enum
`CEF_CONTENT_SETTING_TYPE_MIDI` and `CEF_CONTENT_SETTING_VALUE_BLOCK` on the
browser UI thread terminates with `SIGTRAP`. The same program using
`CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX` returns from the call normally.

The public header lists ordinary MIDI among content-setting types. The pinned
Chromium registry does not register an ordinary MIDI setting, while CEF forwards
the mapped type to `HostContentSettingsMap::SetContentSettingDefaultScope`.
Existing symbolized diagnostics identify the trap at the missing registry-entry
check in that function.

This is a request for safe handling or explicit documentation of unsupported
public enum values. It does not request new ordinary-MIDI permission policy or
claim that ordinary MIDI should be mapped to SysEx.

**To Reproduce**

After initializing CEF normally, run this in
`CefBrowserProcessHandler::OnContextInitialized`:

```cpp
void OnContextInitialized() override {
  CefRequestContext::GetGlobalContext()->SetContentSetting(
      "https://example.test", "https://example.test",
      CEF_CONTENT_SETTING_TYPE_MIDI, CEF_CONTENT_SETTING_VALUE_BLOCK);
  std::fprintf(stderr, "SetContentSetting returned\n");
}
```

No navigation, MIDI hardware, renderer JavaScript, custom CEF patch, or SuiteCut
code is necessary. Replacing only `CEF_CONTENT_SETTING_TYPE_MIDI` with
`CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX` is the control.

Actual standalone process observations:

```text
ordinary MIDI:
  Setting MIDI (27) to BLOCK on the browser UI thread
  [process terminated with SIGTRAP, signal 5; no returned marker]

SysEx control:
  Setting MIDI_SYSEX (13) to BLOCK on the browser UI thread
  SetContentSetting returned
  [original application process exited 0]
```

Both use named public enums, with no numeric cast, injected library, or sandbox
disabling. The standalone source has no dependency on the browser automation
engine. Browser startup emits DBus-unavailable diagnostics in this container.
The external subprocess supervisor reaped every descendant; it also records an
adopted Chromium startup utility exiting 1 in both cases, so this is not a claim
that the control passed a completely clean browser lifecycle test. The direct
ordinary-MIDI API-call trap and returned SysEx control marker are the observations
being reported.


<details>
<summary>Complete standalone source and Linux build/run</summary>

```cpp
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
```

Build with a matching CEF SDK and `libcef_dll_wrapper.a`:

```sh
c++ -std=c++20 -O2 -DNDEBUG -DCEF_USE_SANDBOX \
  -I/absolute/path/to/cef-sdk midi-content-setting.cc \
  /absolute/path/to/libcef_dll_wrapper.a \
  /absolute/path/to/cef-sdk/Release/libcef.so \
  -pthread -ldl '-Wl,-rpath,$ORIGIN' -o /absolute/path/to/run/midi-content-setting
```

Put the matching runtime assets beside the executable (`libcef.so`, ICU data,
PAK files, locales, V8 context snapshot and normal CEF sandbox resources). Run
with fresh absolute profile directories. These are the exact display-free flags
used for the focused reproduction, without disabling the sandbox:

```sh
./midi-content-setting --probe-profile=/absolute/unique/profile-midi \
  --ozone-platform=headless --disable-gpu
./midi-content-setting --probe-profile=/absolute/unique/profile-sysex \
  --ozone-platform=headless --disable-gpu --sysex-control
```

The external supervisor was used only to retain actual child signal/exit and
reap descendants. It does not intercept the CEF API or alter the application.

</details>

**Expected behavior**

An unavailable content-setting type should be rejected safely or be clearly
documented as invalid for this API, rather than appearing in the supported public
enum and terminating the browser process. The existing API warning about
incorrect usage causing instability is acknowledged; please clarify whether the
public MIDI value is intentionally invalid for `SetContentSetting` in this build.

**Versions**

- OS: Debian GNU/Linux 13.6 (trixie), Linux x86-64.
- Embedder compiler: GCC (Debian 14.2.0-19) 14.2.0.
- CEF: `156.0.7+g47dbc1d+chromium-156.0.8078.17`.
- CEF commit: `47dbc1df023e99f006fb9e850a8d83cc19b4fdd9`.
- Chromium: `156.0.8078.17`.
- Stock `libcef.so` SHA-256:
  `d6ae34b71b65685b9fc84f24b53214cdb71a17ceecca0034c3a95a9d4358d86f`.

**Additional context**

Sources: [public MIDI enum](https://github.com/chromiumembedded/cef/blob/47dbc1df023e99f006fb9e850a8d83cc19b4fdd9/include/internal/cef_types_content_settings.h),
[public API and warning](https://github.com/chromiumembedded/cef/blob/47dbc1df023e99f006fb9e850a8d83cc19b4fdd9/include/cef_request_context.h),
and [native forwarding](https://github.com/chromiumembedded/cef/blob/47dbc1df023e99f006fb9e850a8d83cc19b4fdd9/libcef/browser/request_context_impl.cc).

This was reproduced in the standalone CEF application. Stock `cefclient`,
`cefsimple`, newer CEF versions, and other platforms have not been tested.
Google Chrome has no equivalent public CEF request-context API, so no same-API
Chrome comparison is claimed.

Issue searches for `SetContentSetting` and MIDI found no matching registry-entry
crash report on 2026-10-17. #1028 concerns historical Web MIDI/SysEx permission
handling and appears separate from this public API failure.

This report and reproduction were prepared with assistance from OpenAI Codex.
