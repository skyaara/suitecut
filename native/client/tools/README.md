# Native client real-browser verification

`tests/integration/real_browser.cpp` is a standalone C++ application linked only against the
native client library and ordinary C++/OS libraries. It launches the existing CEF
engine directly.

The shared `fixture.html` checks Unicode form values, genuine trusted pointer
input, checkbox and native select controls, shadow-root roles, engine-issued
observation references, actual PNG bytes, and context/page lifetimes. It includes
an input target that moves during hover and an overlay that must prevent a stale
pointer press. It checks observable page outcomes rather than successful command
acknowledgements alone.

Build the standalone client as documented in `native/client/README.md`, then run
from the repository root:

```sh
.suitecut/native-client/build/native-client-real-browser \
  --browser /absolute/path/to/suitecut-browser \
  --fixture native/client/tools/fixture.html \
  --iterations 30 --cycles 50 \
  --output .suitecut/native-client/cpp/results.json
```
