# Hardened-runtime entitlements for the macOS embed kit

`scripts/package-embed-kit-macos.py` signs the kit with the hardened runtime
and only these entitlements:

| Program | Entitlements | Why |
|---|---|---|
| `suitecut-browser Helper (Renderer)` | `helper-renderer.plist`: `com.apple.security.cs.allow-jit` | V8 compiles JavaScript to machine code |
| `suitecut-browser Helper (GPU)` | `helper-gpu.plist`: `com.apple.security.cs.allow-jit` | SwiftShader compiles shaders when there is no GPU |
| `suitecut-browser`, `suitecut-session`, `suitecut-browser Helper`, `(Plugin)`, `(Alerts)` | none | |

These match the JIT entitlements of Chromium's own renderer and GPU helpers.
No program gets `disable-library-validation`: everything the kit loads is
signed by the same team. Camera and microphone use is attributed to the app
that starts the session (its responsible process), so the engine carries no
device entitlements.
