# Isolated CEF and Chromium fixes

The ordinary SuiteCut build uses the pinned stock CEF156 library and does not
apply these patches. User direction keeps custom-library changes isolated while
the native browser SDK builds independently. `bundle.json` pins eleven ordered
patches and all 40 touched source paths, including the exact V8 dependency.

| Candidate | Source component | Status |
| --- | --- | --- |
| Directory enumeration | CEF Alloy | Source patch; custom library pending |
| Content-setting registry validation | CEF request context | Standalone public MIDI crash reproduced; guard patch uncompiled |
| Empty file-input selection | Chromium Blink | Source patch; custom library pending |
| Process wall time | Chromium scheduler/Inspector | Process scope only; full clocks remain open |
| Ordinary MIDI separation | Chromium permissions/Blink | Source patch; explicit policy limits documented |
| Offscreen OOPIF raster | Chromium Blink | Candidate; child-surface synchronization remains unverified |
| CSS navigation retention | Chromium Inspector | Native consumer integrated; producer lacks required final lifecycle contract; custom build pending |
| JS navigation retention | V8 Inspector | Native consumer integrated; producer lacks required final lifecycle contract; custom build pending |
| WebSocket routing | Chromium Blink/Inspector | Native/SDK page/context/worker policies integrated; custom library and authenticated agent handoff pending |
| Original-widget mouse receipts | Chromium browser input | Native controller/SDK integrated behind exact runtime probe; custom library pending |
| Widget receipt revision probe | Chromium browser protocol | Read-only supplement follows widget patch; custom library pending |

Each adjacent manifest records exact source/output hashes and limitations. The
unapplied MIDI adapter is separate from the vendor bundle and is never enabled
by the stock build. The production native coverage controller requires `suitecut.coverage.final-lifecycle.v1`, JS
revision 2 and CSS revision 3. The older isolated JS/CSS producer candidates in
this bundle do not satisfy that contract and are deliberately rejected. Final
context/agent lifecycle producer support remains backend work.

The integrated original-widget mouse controller consumes the real lease API
only after its exact read-only revision probe succeeds. The probe supplement
must follow the widget receipt patch; its three source postimages are included
in the bundle's final hashes. Stock CEF keeps this capability false. See
[`docs/native-widget-mouse.md`](../../../docs/native-widget-mouse.md) for raw
root-widget/attached-OOPIF scope, physical cancellation and remaining backend
receipt families. These candidates do not constitute complete browser parity.

Check the combined bundle against a prepared pinned checkout without modifying
its source directories:

```bash
python3 scripts/check-cef-fix-bundle.py \
  --manifest native/browser/cef-patches/bundle.json \
  --root cef=/absolute/path/to/chromium/src/cef \
  --root chromium=/absolute/path/to/chromium/src
```

The checker uses temporary source shadows and validates final combined hashes.
A pass establishes file pins and patch applicability only. Vendor C++/generated
protocol compilation, a rebuilt CEF distribution, native adapters where still
needed, and runtime acceptance remain separate. The existing large-host recipe
below remains the starting point for that future custom-library build.

## CEF 156 Alloy directory enumeration repair

Status: proposed source repair; **a rebuilt `libcef.so` is not available or verified**. The ordinary SuiteCut binary still returns a controlled directory-upload error. Do not enable the folder implementation until the rebuilt library passes the native capability check below.

The pinned Alloy browser host does not override `content::WebContentsDelegate::EnumerateDirectory`. A genuine file-input directory request therefore reaches Chromium's default implementation, which cancels the original `FileSelectListener`. This affects both `DOM.setFileInputFiles` with a directory and an authenticated native directory drop. Opening the folder picker instead enters an upload-confirmation dialog whose view delegate is unavailable in the ordinary offscreen host.

The patch supplies the missing Alloy delegate method and forwards the real listener and directory path to Chromium's existing `FileSelectHelper::EnumerateDirectory`. That path performs native directory enumeration and returns native file information and the base directory to the original input. It uses `SELECT_NONE`, so it does not enter the folder-picker confirmation dialog. It preserves the browser runtime, capture, audio, input, and public CEF ABI. It does not construct a `FileList`, change `webkitRelativePath`, or transfer files from another browser.

## Exact inputs

| Input | Pin |
| --- | --- |
| CEF source commit | `47dbc1df023e99f006fb9e850a8d83cc19b4fdd9` |
| CEF branch | `8078` |
| CEF API/version | `156.0.7+g47dbc1d+chromium-156.0.8078.17` |
| Chromium checkout | `refs/tags/156.0.8078.17` |
| Platform | Linux x86-64 |
| SuiteCut libyuv source | `7c85a3a0820fab29abb502c207d8b9a394e352cc` (`native/browser/libyuv-build.json`) |
| Patch | `cef156-alloy-directory-enumeration.patch` |
| Patch SHA-256 | `830d3ce0821b66c41b0549fa124649f02f57d6940a4fea8bbf268a39c6101ccc` |
| Official pinned `automate-git.py` SHA-256 | `94d3ddd1e0d35482132f45dc101a593c9371b6eb2d8ff4af6cfe2734d2e48c6a` |
| Ordinary unmodified Linux `libcef.so` SHA-256 | `d6ae34b71b65685b9fc84f24b53214cdb71a17ceecca0034c3a95a9d4358d86f` |

The source commit's `CHROMIUM_BUILD_COMPATIBILITY.txt` selects the exact Chromium tag above. The patch applies cleanly to that commit. Official automate/distribution command options and the native enumerator signature were checked against those pinned sources; the build commands below have not been executed here.

## Build host and checkout

Provision a Linux x86-64 build host supported by Chromium, for example Ubuntu 24.04, with **500 GiB free storage, 64 GiB RAM, and 16 CPU cores** as a conservative build budget. These are provisioning recommendations rather than measured minimums for this revision. Source, toolchains, generated files, intermediate objects, and the final distribution must all fit. This workspace has a 32 GiB filesystem with approximately 15 GiB free, 17 GiB RAM, and five CPUs; it cannot hold the complete Chromium build tree and output.

Install Python 3, Git, curl, CMake, Ninja, a C++20 compiler, and Chromium's Linux build dependencies. Run Chromium's `build/install-build-deps.sh` on a prepared build host after checkout; inspect its package installation requirements first. Use the repository checkout's pinned toolchains and depot_tools for compilation.

Run these commands with `suitecut_repo` pointing to this repository and `cef_task_build_root` pointing to the large build volume. Do not reuse a normal SuiteCut SDK or build output directory.

```bash
suitecut_repo=/absolute/path/to/suitecut
cef_task_build_root=/mnt/cef156-alloy-directory-build
mkdir -p "$cef_task_build_root"
curl --fail --location \
  https://raw.githubusercontent.com/chromiumembedded/cef/47dbc1df023e99f006fb9e850a8d83cc19b4fdd9/tools/automate/automate-git.py \
  --output "$cef_task_build_root/automate-git.py"
python3 - "$cef_task_build_root/automate-git.py" <<'PY'
import hashlib, sys
assert hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest() == \
    '94d3ddd1e0d35482132f45dc101a593c9371b6eb2d8ff4af6cfe2734d2e48c6a'
PY
python3 "$cef_task_build_root/automate-git.py" \
  --download-dir="$cef_task_build_root" \
  --branch=8078 \
  --checkout=47dbc1df023e99f006fb9e850a8d83cc19b4fdd9 \
  --chromium-checkout=refs/tags/156.0.8078.17 \
  --no-chromium-history --x64-build --no-build --no-distrib
```

## Apply and compile

Do not rerun an updating automate command after applying the patch: updating can restore the working tree. Save the resulting `args.gn`, build logs, exact source revisions, patch digest, library digest, and distribution digest as provenance.

```bash
cef_task_source="$cef_task_build_root/chromium/src"
export PATH="$cef_task_build_root/depot_tools:$PATH"
cd "$cef_task_source/cef"
test "$(git rev-parse HEAD)" = 47dbc1df023e99f006fb9e850a8d83cc19b4fdd9
python3 - "$suitecut_repo/native/browser/cef-patches/cef156-alloy-directory-enumeration.patch" <<'PY'
import hashlib, sys
assert hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest() == \
    '830d3ce0821b66c41b0549fa124649f02f57d6940a4fea8bbf268a39c6101ccc'
PY
git apply --check "$suitecut_repo/native/browser/cef-patches/cef156-alloy-directory-enumeration.patch"
git apply "$suitecut_repo/native/browser/cef-patches/cef156-alloy-directory-enumeration.patch"
export GN_DEFINES='is_official_build=true is_component_build=false use_sysroot=true use_thin_lto=false symbol_level=1'
./cef_create_projects.sh
cd "$cef_task_source"
autoninja -C out/Release_GN_x64 -j 12 cefclient ceftests chrome_sandbox
cd "$cef_task_source/cef"
python3 tools/make_distrib.py \
  --output-dir="$cef_task_build_root/distribution" \
  --ninja-build --x64-build --minimal --no-archive
```

The selected GN settings create a release distribution with the public ABI of the pinned CEF version and avoid the additional linker memory cost of ThinLTO. Confirm that generated `out/Release_GN_x64/args.gn` contains the selected definitions and that the distribution's `include/cef_version.h` still matches the exact version pin. Do not silently accept a newer checkout, change the runtime style, or replace the ordinary pinned SDK in place.

## Genuine verification

First build SuiteCut into a separate directory against the rebuilt minimal distribution, substituting its actual extracted SDK path. Use the exact libyuv source pin from `native/browser/libyuv-build.json` and the native build instructions in `native/browser/README.md`; provision `.suitecut/libyuv` from that commit if it is absent.

```bash
cd "$suitecut_repo"
cmake -S native/browser -B .suitecut/cef156-directory-repair-native \
  -DCEF_ROOT=/absolute/path/to/rebuilt/minimal/sdk \
  -DLIBYUV_ROOT="$suitecut_repo/.suitecut/libyuv" \
  -DPROJECT_ARCH=x86_64 -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/cef156-directory-repair-native --parallel 4
SUITECUT_NATIVE_EXECUTABLE="$suitecut_repo/.suitecut/cef156-directory-repair-native/suitecut-browser" \
  node scripts/verify-cef-directory-upload.mjs
node --input-type=module -e \
  'import fs from "node:fs"; const r=JSON.parse(fs.readFileSync(".suitecut/cef-directory-upload/protocol-report.json","utf8")); if(r.directoryVerified!==true) throw new Error("Authentic native folder enumeration remains unavailable")'
```

That capability proof uses real temporary files, nested and Unicode names, the original file input, native `DOM.setFileInputFiles`, and actual browser `File.text()` and `webkitRelativePath` values. Preserve its binary/CEF hashes and complete report. It currently reports failure with the unmodified Alloy library.

The completed native folder adapter is source-compiled, while its public entry remains fail-fast on the pinned stock library. Apply the separate **unapplied SuiteCut-root** `suitecut-native-directory-adapter.patch` only after the genuine custom-library directory proof succeeds; its companion JSON pins source and patch bytes. It activates direct `DOM.setFileInputFiles` against the retained original input. The isolated observer requires the browser's trusted input/change sequence, a replaced native FileList, unchanged document identity, and browser-provided relative paths. All file mutations share a physical-ACK lane, and native observation survives logical cancellation through real completion and group-release ACK. The stock default cancellation did not produce an observed terminal DOM event in the historical proof, so attempting it would wait until the original deadline; the production entry preserves immediate rejection. Neither patch presence nor source compilation enables the public readiness capability. Exact custom-library compilation and directory/cancellation/navigation/page-close verification remain pending. The page-local lane serializes SDK file mutations; trusted event brand/order and relative paths do not independently identify an initiating native command against out-of-band picker, drop, or raw-CDP producers. That attribution boundary must be included in custom-runtime verification.

Only a successful rebuilt-library run can establish this repair. Debugger experiments are diagnostic only and must not enter production or replace the patched CEF source build.

## Separate explicit empty-selection proposal

[Chromium 156 explicit empty file selection](chromium156-empty-file-selection.md)
contains a separate Chromium-root patch for the native empty-file command. It
retains actual chooser cancellation and native FileList/events. That custom CEF
build and its runtime capability verification are also pending; the patch adds
no live SuiteCut upload capability.
