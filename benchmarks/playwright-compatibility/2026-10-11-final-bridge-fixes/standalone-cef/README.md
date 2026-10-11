# Standalone CEF srcdoc reproduction

This diagnostic links only CEF, its C++ wrapper and X11. It reproduces the
initial about:blank/srcdoc crash without SuiteCut or Playwright. Both recorded
modes report `SRC_CRASH:status=2 exit=5`; the program returns 2 on failure.
The codec-enabled pinned CEF version and digest are in the parent metadata.

Build command on the tested workspace (use matching SDK/runtime paths elsewhere):

```bash
/usr/bin/c++ -std=c++20 -DCEF_USE_SANDBOX \
  -I/workspace/suitecut/.suitecut/cef-codecs-sdk \
  cef-srcdoc-repro.cc \
  /tmp/suitecut-cef-full-regression-build/libcef_dll_wrapper/libcef_dll_wrapper.a \
  /workspace/suitecut/.suitecut/cef-codecs-sdk/Release/libcef.so \
  -Wl,-rpath,/tmp/suitecut-cef-full-regression-build -pthread \
  -L/workspace/suitecut/.suitecut/obs-deps/root/usr/lib/x86_64-linux-gnu \
  -l:libX11.so.6 -o /tmp/suitecut-cef-full-regression-build/cef-srcdoc-repro
```

Run under the same Xvfb/library environment with a distinct absolute profile
directory per trial. `--no-sandbox --disable-gpu --profile=/tmp/unique-cef-profile`
selects the tested host configuration. Add `--windowed` for the windowed trial.
The binary must live with the matching CEF resources, locales and native
libraries; this source is not a standalone runtime distribution.
