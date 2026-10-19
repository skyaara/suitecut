# Chromium 156 explicit empty file selection

Status: proposed vendor source repair. It applies to the pinned source; **the
patched Blink tests and a rebuilt `libcef.so` have not been compiled or run**.
The custom CEF build remains pending. The ordinary SuiteCut runtime has no new
empty-selection capability from this patch.

The exact source target is Chromium `refs/tags/156.0.8078.17`, used by CEF
`47dbc1df023e99f006fb9e850a8d83cc19b4fdd9` / version
`156.0.7+g47dbc1d+chromium-156.0.8078.17`. Apply
`chromium156-empty-file-selection.patch` at the Chromium `src` root. Its SHA-256
is `b34cc8e6f5fb6d109e2357813ec787fa4b7e7986b3544ceae281def3133aa34b`.
This is a Chromium-root patch; the separate Alloy directory-enumeration patch
in this directory applies at the CEF checkout root.

## Native contract

In the pinned source, `InspectorDOMAgent::setFileInputFiles` validates a real
file input, converts the supplied protocol array to native paths, then calls
`HTMLInputElement::SetFilesFromPaths`. That forwards to
`FileInputType::SetFilesFromPaths`, whose early return on an empty array makes
`DOM.setFileInputFiles(files: [])` acknowledge without changing the input.
The recorded unmodified native capability probe confirmed this: a populated
required input retained its file and validity, with no new input/change event
after either empty command.

An explicit empty selection is distinct from canceling a chooser. The patch
routes only the empty `SetFilesFromPaths` call through the existing
`FilesChosen(empty, empty-base-directory)` path, before the directory branch.
That code creates Blink's own `FileList`, installs it using `SetFiles`, updates
form state/validity/view, dispatches native input/change when selection changed,
and performs the existing accessibility and chooser-disconnect work. No files,
relative paths or event brands are supplied by the SDK. With no connected chooser requesting a forced change, clearing an already
empty input retains the existing unchanged-selection cancel event semantics.
The existing connected-chooser `force_change_event_` policy is unchanged; the
patch adds no separate forced-event policy.

The pinned cancellation branches remain byte-for-byte unchanged:

- `FileChooser::DidChooseFiles(null)` calls `FileChooserCanceled` when
  `FilePickerEventsFix` is enabled; its existing disabled-feature legacy branch
  still calls `FilesChosen(empty)`, exactly as before.
- `FileInputType::FileChooserCanceled` emits the native cancel event and
  disconnects the chooser without replacing selected files.
- An Inspector-suppressed canceled picker calls
  `SetFilesAndDispatchEvents(nullptr)`, which preserves its existing behavior.
- `ReceiveDroppedFiles` rejects an empty native drag before calling
  `SetFilesFromPaths`, so an empty drag still cannot clear the input.

Nonempty single/multiple uploads and directory enumeration use their original
branches. No public CEF API, sandbox setting, renderer bootstrap or SuiteCut
controller changes are part of this proposal.

## Pinned inputs and source checks

| Chromium path under `third_party/blink/renderer/core/` | Original SHA-256 |
| --- | --- |
| `html/forms/file_input_type.cc` | `98bb621e81556e5dff699fb5c1abf49e68697d2b6e7bc7aeab6dee38774a0990` |
| `html/forms/file_input_type_test.cc` | `c583b973a9afca7568b799be25a9cfd6fac00662446f18d72b6d856d4a3da739` |
| `html/forms/mock_file_chooser.h` | `a3ebe4e8d7d679d3669e1f106fd6221a79d9d6ffd1069d66bcebce81febe40aa` |

The proposed tests use the actual `HTMLInputElement` setter and native listener
path to check populated single/multiple selection, a directory-flagged control,
required validity, old/new native `FileList` ownership, trusted ordered events,
and already-empty behavior. A separate test sends an actual null result through
the existing Mojo chooser mock with `FilePickerEventsFix` enabled; it requires
the previous native selection and only a cancel event. The mock gains only the
explicit null-result completion used by that test.

`git apply --check` against the pinned source subset succeeds. This is source
validation, not a Blink build or runtime result.

## Deferred build and verification

Use the exact checkout and deferred build instructions in [README.md](README.md).
After checkout, before generating or compiling CEF projects:

```bash
cd "$cef_task_source"
git apply --check "$suitecut_repo/native/browser/cef-patches/chromium156-empty-file-selection.patch"
git apply "$suitecut_repo/native/browser/cef-patches/chromium156-empty-file-selection.patch"
```

Retain the exact Chromium/CEF revisions, patch hashes, GN arguments, build logs
and resulting `libcef.so` digest. Build and run the new Blink tests alongside
existing file-input tests on that host:

```bash
autoninja -C out/Release_GN_x64 -j 12 blink_unittests
out/Release_GN_x64/blink_unittests --gtest_filter='FileInputTypeTest.*'
```

Then build SuiteCut separately against that rebuilt pinned CEF distribution.
First repeat the raw `DOM.setFileInputFiles(files: [])` capability check against
an actual populated input, preserving selected backend/document identity, real
File bytes, native event order/trust, required validity, stderr and physical
shutdown. Repeat native chooser cancellation and all unchanged upload/chooser
originals. The pinned chooser reset case is
`tests/page/page-filechooser.spec.ts:293`.

The SuiteCut empty-upload controller now sends the genuine empty native file
command against the retained original input, after installing an isolated native
event observer. It requires trusted ordered input/change, a replaced FileList
and zero resulting files. Clearing an already empty input may complete through
the existing trusted native cancel event only when both the original and final
selection are empty. It does not synthesize File objects or events and does not
fall back to an authored DataTransfer. The stock backend's acknowledged no-op
returns an explicit unsupported error without changing the input or waiting
until timeout. Original action deadlines remain in use. A page-local mutation lane serializes
all file-input mutations through their actual DevTools ACKs. The native observer
uses a dedicated object group and survives logical cancellation until a real
native terminal event and group-release ACK arrive. An unknown observer failure
rejects further file mutations and requests actual page closure; only the real
OnBeforeClose event retires that lane. Cleanup refusal likewise closes the page
instead of manufacturing a release ACK. Successful replies recheck the original
action deadline after native completion and object-group release.
The adapter is compiled independently of vendor activation; the public native
reset capability remains false until both pieces are verified against the same
rebuilt runtime. Directory upload remains an independent deferred vendor repair.
