# Launch context video

`launchNativeBrowser({executablePath, recordVideo: {dir, size?}})` records the
initial page, subsequent default-context pages and their actual popups. This
also works with `userDataDir`; CEF continues to own the persistent profile.
Use `createNativePage(source).video()` or the default context's page object for
the stable native `NativeVideo` handle. Explicit new contexts keep their own
independent `recordVideo` option.

The SDK validates scalar paths and forwards one bounded native launch option.
The existing native background owner prepares a private directory and resolves
FFmpeg before the initial CEF page is created. A dedicated startup operation
retains its real generation and original deadline through page readiness. The
native ready packet carries the recorder's actual ID; no extra metadata IPC,
synthetic page event or fake first frame is needed. Before that first ready
packet, native recording receipts remain in the actual native registry; a real
encoder failure becomes a launch error. Encoder startup and all subsequent
file operations use the existing recorder and retained post-close registry.

`startupTimeoutMs` bounds launch, including directory preparation, page
initialization and the recorder's first-frame deadline. Launch readiness still
follows actual document/bootstrap readiness; it does not wait for the first
encoded frame. A later encoder failure remains an actual video/context-close
failure. Native configuration
parsing, preparation, page-creation and observed encoder failures reject launch
and drain through the existing native shutdown owners. The SDK still owns its
existing stricter process-start timeout and termination fallback. With recording
omitted, the existing synchronous page-creation path starts no preparation job,
encoder process or additional protocol command.

This addition received a scoped SDK typecheck and parser-header syntax check.
The full Source and production native browser target now compile/link, and the
matching browser SDK is emitted and installed. Actual launch recording,
persistence, startup-failure combinations and video output await the deferred
integration/E2E pass.
See `context_video.md` for the existing recorder geometry, retention and file IO
limits; this feature does not alter them.
