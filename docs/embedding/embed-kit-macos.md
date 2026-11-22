# The macOS embed kit

One folder of signed files that an app copies into its own bundle. There is no
separate `SuiteCut.app` to install. Napdock copies it into its built-in Browser
extension's `helpers/` folder, next to its Swift helper `browserhost`.

## Layout

```
suitecut-browser.app/                 the engine (com.suitecut.browser)
  Contents/Info.plist
  Contents/MacOS/suitecut-browser
  Contents/Resources/                 icon, CEF and libyuv licences
  Contents/Frameworks/
    Chromium Embedded Framework.framework   official CEF 156.0.30, en.lproj only
    suitecut-browser Helper.app             .helper
    suitecut-browser Helper (GPU).app       .helper.gpu       allow-jit
    suitecut-browser Helper (Renderer).app  .helper.renderer  allow-jit
    suitecut-browser Helper (Plugin).app    .helper.plugin
    suitecut-browser Helper (Alerts).app    .helper.alerts
suitecut-session                      the session service, protocols 0 and 1 (com.suitecut.session)
licenses/                             CEF, Chromium credits, libyuv, SuiteCut, OpenSSL, curl, libpsl, brotli
kit.json                              versions, protocols, signing, and every file's path, size and sha256
```

`kit.json`'s `files` lists every regular file except `kit.json` itself, as
`{path, size, sha256}`. The kit holds no symlinks, hidden names or characters
outside `[A-Za-z0-9 ()._@+-]`, because Napdock's installer rejects them.

The session takes the same arguments as `suitecut-shared-session`:

```
suitecut-session --session NAME --session-dir DIR \
  --browser KIT/suitecut-browser.app/Contents/MacOS/suitecut-browser --profile DIR start
```

`sc_runtime_inspect` accepts the kit folder as a runtime. It finds the engine
in `suitecut-browser.app`. A signing requirement is checked against both
programs and every top-level `.app` and `.framework`, nested code included.

## Why the engine keeps a full bundle

The plan was a flat folder with a bare `suitecut-browser` beside the
framework and the helpers. Two attempts on macOS 27 with the official CEF
156.0.30 build showed it can't work:

1. **A bare engine** (no `.app`; framework, helper and main-bundle paths set
   in `CefSettings`):
   - Every child failed to find the browser's Mach port rendezvous service
     (`bootstrap_look_up ... MachPortRendezvousServer.<pid>: Permission
     denied (1100)` and `Unknown service name (1102)`).
   - Chromium names the service after the main bundle's identifier. A bare
     executable has none, so the name started with just
     `.MachPortRendezvousServer.`.
   - Chromium also chooses the GPU, Renderer, Plugin and Alerts helpers
     only when the main executable is bundled. Every child ran in the plain
     helper.
2. **A minimal `.app`** (only `Info.plist` and the executable) with the
   framework and the helpers beside it:
   - The rendezvous then worked.
   - The sandboxed renderer could not load the framework (`file system
     sandbox blocked open()`), and the browser could not issue it a sandbox
     extension for the folder (`sandbox_extension_issue_file_to_process
     failed ... Operation not permitted`).
   - Chromium's renderer sandbox reads only inside the main bundle.

So the framework and the helpers stay inside the engine's bundle, in CEF's
standard layout. The engine needs no layout code of its own.

## No Homebrew libraries, macOS 14

The engine links curl, OpenSSL, libpsl and brotli. Homebrew builds them for
the host's macOS (`minos 27.0` here), so bundling them would not load on
macOS 14.
- `scripts/build-embed-kit-deps-macos.py` builds them from source as static
  libraries for macOS 14.
- The sources are pinned in `native/browser/macos-kit-deps.json`, with the
  same URLs and sha256 as the Homebrew formulae.
- curl is configured like Homebrew's for what the engine uses:
  - TLS: the OpenSSL backend, with certificates checked by Apple's trust
    store (`USE_APPLE_SECTRUST`; no CA bundle path is compiled in).
  - Apple IDN, for `CURLU_PUNYCODE`.
  - libpsl and GSS-API.
- curl leaves out HTTP/2 and HTTP/3 (the engine accepts only HTTP/1.x
  answers), brotli and zstd (the engine decodes bodies itself), SSH and LDAP.
- OpenSSL's compiled-in config folder is `/var/empty/suitecut-openssl`.
  Only root can create it, so no user can plant an `openssl.cnf` there.

`native/browser` and `native/client` now default `CMAKE_OSX_DEPLOYMENT_TARGET`
to 14.0. CEF's CMake asks for 13.0, and `native/browser/CMakeLists.txt` raises
it to the cached value.

The client parsed doubles with `std::from_chars`, which libc++ provides only
from macOS 26. On Apple it now uses `strtod_l` in the C locale.

## Signing

- `scripts/package-embed-kit-macos.py` signs inside out with the Developer
  ID from the shared signing policy (`native_macos_signing.py`). That is team
  `WTD7AF5L23` here.
- Every signature uses `--options runtime --timestamp`.
- Entitlements are in `native/browser/entitlements/`: `allow-jit` for the
  Renderer and GPU helpers only, nothing else.
- The kit is not notarized. Notarization sends the files to Apple and needs
  the owner's approval.

## CEF build: official by default

- `cef-build.json` has two macOS pins:
  - `macosarm64Official` is the official archive from
    cef-builds.spotifycdn.com. Its sha256 is `731b599e…b855` and its sha1
    matches CEF's index.
  - `macosarm64` is OpenArch's codec build.
- **The kit's default is the official build, which plays no H.264, AAC or
  HEVC.** The codec build is a third-party build with no codec patent
  licence, so a kit installed on other people's Macs defaults to the
  official archive.
- `--cef codecs` packages the codec build instead. The engine must then be
  built against that SDK too; the packager checks the build's `CEF_ROOT`.

## Size

The kit's files total 333 MB (318 MiB): the framework is 276 MiB, the
session 7 MiB and the licences 5 MiB. Before thinning they total 381 MB.
- Thinning keeps only `en.lproj` of the framework's 220 locale folders.
  SwiftShader is kept.
- Chromium falls back to en-US for any other language. On this Mac
  (`en-IN`, which Chromium maps to en-GB) the kit started with `en_GB.lproj`
  gone.
- A person whose system language has no `.lproj` gets English error pages
  and dialogs. Their pages may also see `navigator.language` as en-US unless
  the host sets the accept-language list; this was not checked on a
  non-English Mac.

## Recipe

```sh
# Static libraries for macOS 14
python3 scripts/build-embed-kit-deps-macos.py fetch --work .suitecut/kit-deps
python3 scripts/build-embed-kit-deps-macos.py build --work .suitecut/kit-deps
# The pinned official SDK
python3 scripts/package-embed-kit-macos.py fetch-cef --cef official --dest .suitecut/cef-official-sdk
# The engine
P=$PWD/.suitecut/kit-deps/prefix
cmake -S native/browser -B .suitecut/kit-engine-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPROJECT_ARCH=arm64 \
  -DCEF_ROOT=$PWD/.suitecut/cef-official-sdk -DLIBYUV_ROOT=$PWD/.suitecut/libyuv \
  -DCMAKE_PREFIX_PATH=$P "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" \
  -DOPENSSL_ROOT_DIR=$P -DOPENSSL_USE_STATIC_LIBS=ON \
  -DSUITECUT_PSL_INCLUDE_DIR=$P/include -DSUITECUT_PSL=$P/lib/libpsl.a \
  -DSUITECUT_BROTLI_INCLUDE_DIR=$P/include -DSUITECUT_BROTLI_DECODER=$P/lib/libsuitecut-brotlidec.a \
  -DSUITECUT_BROTLI_ENCODER=$P/lib/libsuitecut-brotlienc.a
cmake --build .suitecut/kit-engine-build --target suitecut-browser --parallel 2
# The session, the real-engine tests and the sample
cmake -S native/client -B .suitecut/kit-client-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSUITECUT_CLIENT_BUILD_TESTS=ON -DSUITECUT_CLIENT_BUILD_EXAMPLES=ON -DSUITECUT_CLIENT_BUILD_SESSION=ON \
  "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" -DOPENSSL_ROOT_DIR=$P -DOPENSSL_USE_STATIC_LIBS=ON
cmake --build .suitecut/kit-client-build --target suitecut-session \
  suitecut-native-cef_host_events suitecut-native-cef_embed_frames suitecut-embed-sample --parallel 2
# The kit, then its checks
python3 scripts/package-embed-kit-macos.py package --cef official --cef-sdk .suitecut/cef-official-sdk \
  --engine-build .suitecut/kit-engine-build --session .suitecut/kit-client-build/suitecut-session \
  --deps .suitecut/kit-deps --output .suitecut/embed-kit --replace
python3 scripts/verify-embed-kit-macos.py --kit .suitecut/embed-kit \
  --client-build .suitecut/kit-client-build --output .suitecut/kit-verify-NEW
```

## Checks

Build steps above run one at a time at `--parallel 2`; on a shared Mac, run
them through its build-slot wrapper.

`scripts/verify-embed-kit-macos.py` (2026-11-21, macOS 27 arm64): 56 of 56.

- **On the kit:**
  - `kit.json` lists exactly the kit's files, with the right sizes and
    sha256, in the form Napdock reads.
  - `otool -L`: the 11 Mach-O files link only `/usr/lib` and
    `/System/Library`.
  - `vtool`: nothing needs macOS newer than 14.0. The engine, the session
    and the five helpers are built for exactly 14.0.
  - `codesign --verify --deep --strict` passes for the framework, each
    helper, the engine's bundle and the session. Each has the hardened
    runtime, team `WTD7AF5L23`, and the entitlements `kit.json` names.
- **On a `cp -R` copy**, the way Napdock installs it, every check below ran
  against the copy:
  - The copy's signatures pass the same `codesign` check.
  - `suitecut-native-cef_host_events` passed 64 of 64, and
    `suitecut-native-cef_embed_frames` 23 of 23. These are the commands
    `ctest -L embed-host-events` and `ctest -L embed-frames` run.
  - `scripts/verify-embed-sample-macos.py` passed.
  - Protocol 0, as browserhost speaks it: the session starts with
    browserhost's arguments and environment. `tabs open`, `tabs list` (url
    and title), `view screenshot` (a 1280x720 PNG with the page's colour) and
    `tabs close` all work, and `close` ends the session and its engine.

The client's unit tests (`ctest -LE 'cef|embed'`) also pass at the 14.0
target: 26 of 26.

**Not checked:**
- macOS 14 itself. Only this macOS 27 Mac was available; the 14.0 minimum is
  checked in the load commands.
- The `--cef codecs` kit. It needs an engine built against the codec SDK
  with the static libraries.
- Notarization, Gatekeeper and quarantine.
- Linux, after the client changes. They are Apple-only (`#if __APPLE__`).
- HTTPS through the static curl. The checks serve pages over HTTP on
  127.0.0.1.
