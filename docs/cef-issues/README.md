# Upstream CEF reports and feature ownership

Prepared 2026-10-17 against stock CEF
`156.0.7+g47dbc1d+chromium-156.0.8078.17`. The reports below are proposed exact
publication text. Publishing was attempted through the connected GitHub app
and the existing authenticated CLI; both returned `Resource not accessible by
integration`. Neither report is published. [The publication receipt](publication.json)
records the access failure and exact issue-body hashes. Neither candidate vendor patches nor a successful
source-application check establish a compiled fix.

| Report | Classification and evidence | Publication |
| --- | --- | --- |
| [Alloy directory enumeration](alloy-directory-enumeration.md) | CEF-specific delegate gap; real protocol control and native debugger trace | Reviewed; publication blocked by GitHub access |
| [MIDI public content-setting trap](midi-content-setting-trap.md) | Public CEF API safety/documentation issue; standalone named-enum SIGTRAP and returning SysEx control | Reviewed; publication blocked by GitHub access |

The ordinary MIDI automation feature is separate from the public setter crash.
Avoiding an unregistered content type in SuiteCut was our necessary engine fix.
The standalone named-enum reproduction establishes an upstream API issue worth
reporting without claiming that CEF must adopt SuiteCut's independent MIDI policy.

## Reproduction code

- [Directory CDP probe](repro/directory-upload.mjs): dependency-free Node 22+
  script; syntax checked, not run as a standalone CEF sample here. It reproduces
  the same direct protocol sequence as the earlier actual embedder/GDB evidence.
- [MIDI CEF probe](repro/midi-content-setting.cc): compiled and run against the
  actual stock library and existing official CEF wrapper. No SuiteCut browser
  sources, synthetic API responses, JavaScript permission patch, or injected
  library is used.
- [Evidence receipt](evidence.json): version/hash pins, actual direct-child and
  descendant receipts, earlier protocol trace, duplicate search and qualifications.
  Detailed logs remain at `/workspace/suitecut-cef-report-proof-20261017`.

For the directory probe, start a dedicated disposable `cefclient` Alloy browser
with remote debugging enabled (for example, `--use-alloy-style
--off-screen-rendering-enabled --remote-debugging-port=9222` in a sample build
that supports those switches), then run:

```sh
node docs/cef-issues/repro/directory-upload.mjs http://127.0.0.1:9222
```

The script replaces the first debuggable page's body, creates temporary real
files, runs regular-file and directory controls, prints actual results, and exits
1 if the directory selection is not populated. Run the server and script on the
same filesystem. Exact sample switches may vary by CEF sample version; the
standalone sample command itself has not been run here.

For the MIDI probe, use a matching SDK and built `libcef_dll_wrapper.a`. A Linux
example (substitute absolute paths) is:

```sh
c++ -std=c++20 -O2 -DNDEBUG -DCEF_USE_SANDBOX \
  -I/absolute/path/to/cef-sdk \
  docs/cef-issues/repro/midi-content-setting.cc \
  /absolute/path/to/libcef_dll_wrapper.a \
  /absolute/path/to/cef-sdk/Release/libcef.so \
  -pthread -ldl '-Wl,-rpath,$ORIGIN' -o /absolute/path/to/run/midi-content-setting
```

Place matching runtime assets beside the executable, including `libcef.so`, ICU
data, PAK files, locales and the V8 context snapshot. Preserve the normal CEF
sandbox setup. On this display-free Linux workspace, the actual focused commands
were:

```sh
./midi-content-setting --probe-profile=/absolute/unique/profile-midi \
  --ozone-platform=headless --disable-gpu
./midi-content-setting --probe-profile=/absolute/unique/profile-sysex \
  --ozone-platform=headless --disable-gpu --sysex-control
```

Use a subprocess owner/reaper and retain actual signal/exit receipts. Ordinary
MIDI intentionally reproduces a browser-process trap. The SysEx call returned
and its direct application exited 0. The supervisor separately observed a
Chromium startup utility exiting 1 in both runs; it reaped all descendants.
This qualification is preserved instead of labeling the whole control clean.

## What should not be filed as a CEF browsing bug

| Pending capability | Ownership / upstream route | Current reporting decision |
| --- | --- | --- |
| WebSocket frame routing/mocking | Chromium Blink/DevTools extension plus our controller/SDK | Feature request/design discussion only; no CEF bug established |
| Full clocks across contexts/frames/workers | Chromium scheduler/DevTools extension and our scheduling barriers | Feature extension; process-wall-time candidate does not complete clocks |
| Explicit empty native file selection | Chromium Blink file-input semantics | Candidate API behavior extension; no standalone confirmed CEF bug submitted |
| Coverage retention after navigation | V8/Chromium Inspector retention plus our adapter/interval merge | Feature extension; destruction/process-change guarantees still unverified |
| Offscreen OOPIF full-page capture | Chromium raster/lifecycle and our capture synchronization | Possible capture correctness issue; insufficient standalone evidence to submit as confirmed bug |
| Context-local client certificate/private-key import | CEF public API extension plus native credential integration | Feature/API extension; do not describe general TLS or existing certificate selection as broken |
| Concurrent credential ceremonies | Our session/credential controller plus real backend constraints | Finish native ownership/integration first; no concrete CEF bug established |
| Original-widget input receipts | Chromium input acknowledgment observability plus native controller | API extension/design work; not a claim that ordinary input delivery is broken |

Do not submit the isolated policy-changing ordinary-MIDI patch as the fix for
the setter trap. A missing-entry guard/documentation repair has a narrower
purpose and should preserve upstream permission policy.

## Tracker, duplicate checks and contribution rules

The official tracker is [chromiumembedded/cef issues](https://github.com/chromiumembedded/cef/issues).
The [current contribution rules](https://github.com/chromiumembedded/cef/blob/master/CONTRIBUTING.md)
require checking existing issues and including the version, OS/compiler and
reproduction for bugs. General usage questions belong on the CEF Forum.

Searches for `EnumerateDirectory` and `SetContentSetting` in this tracker found
no matching issue on 2026-10-17. Related reports were reviewed: [#958](https://github.com/chromiumembedded/cef/issues/958),
[#3314](https://github.com/chromiumembedded/cef/issues/3314),
[#3316](https://github.com/chromiumembedded/cef/issues/3316), and
[#1028](https://github.com/chromiumembedded/cef/issues/1028). They concern historical
picker/SysEx support or dialog unification; they do not contain these precise
direct-enumeration and registry-entry reproductions. Searches are bounded and
do not guarantee absence of every related issue.

The [CEF AI policy](https://github.com/chromiumembedded/cef/blob/master/AI_POLICY.md)
permits assistance while requiring human accountability, accurate reviewed
output, and non-extractive contributions. The exact issue bodies disclose Codex
assistance. The root reviews facts, reproduction and routing before publication.
No code contribution or upstream pull request is included in this task.
