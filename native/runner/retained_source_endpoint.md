# Process endpoint startup and shutdown-boundary transport

This followup is an independent native transport primitive. No browser.cc,
worker host, fixture callback or reuse-eligibility switch is wired by this unit.
The endpoint is owned once by the native browser helper process/Engine. Its
multiple Source pages, contexts, broker, HTTP and file/media owners do not get
separate process registrations.

Native registration configuration carries the inherited seqpacket descriptor,
native ticket and kernel SO_COOKIE. The SDK only forwards those native-issued
fields. Prepare validates AF_UNIX/SOCK_SEQPACKET/cookie identity and marks
FD_CLOEXEC before CefExecuteProcess or CefInitialize can create subprocesses.
Its result is trivially copyable; an owning endpoint/buffer need not span the
zygote dispatch wrapper. A --type subprocess ignores registration configuration
and closes only a descriptor whose socket type/family/cookie match the private
endpoint. A numeric fd may have been reused by CEF after CLOEXEC; a different
cookie preserves that unrelated fd. Malformed primary configuration is an
explicit error. Default launches without private fields remain disabled.

WriteStartup emits the existing native SOURCE_START record using actual
getpid/parent/kernel start identity and executable observation. The runner still
requires SO_PASSCRED and all original exact ownership checks. This is process
startup, not CEF/browser/page readiness. A refused/nonblocking write leaves its
written flag false and must become an explicit startup failure at the binding.
There is no swallowed channel error or guessed successful receipt.

WriteShutdownReached emits a distinct SOURCE_SHUTDOWN_REACHED record only at a
future actual process hook after Engine destruction, CefShutdown returns and
Writer.Stop joins its thread. Its result code is the intended main return value,
not an observed exit status. The record includes original and currently observed
parent identity so adoption does not rewrite the original host lease. This label
is neither core CloseReceipt, accepted-work completion, idle, nor physical exit.
The original process/domain owner must still observe actual exit and retirement.
No zero-valued diagnostic count is supplied by this transport.

The future central hook contract is narrow: prepare the trivial configuration
before CefExecuteProcess; pass it to RunSuiteCutBrowserMain; own the endpoint
there and explicitly handle startup-write failure; after the existing actual
engine/CefShutdown/Writer.Stop sequence, explicitly handle shutdown-label write
failure. Source startup before CEF initialization is permitted only with its
process-startup meaning; normal existing SDK browser-ready handling is preserved.
No hook may feed the shutdown label into RetainedSourceLeases::ObserveClose.

The actual accepted-work idle barrier remains separate unfinished work. Audit
already identifies Source::SubmitDevTools, context_workers.cc deferred sends,
utility_browser.cc ExecuteDevToolsMethod, Engine controllers for HTTP/files/media,
original queued callbacks, and Writer output as distinct owners. One page's
operation registry or diagnostic counters cannot cover that process. Until all
required actual ownership is bound, CEF children remain ineligible for reuse.

The nine authored endpoint controls are transport components, including a real
native exec child and exec descendant. Their synthetic call to the shutdown-label
method does not execute CEF and makes no actual CEF shutdown claim. Native kernel
credentials, CLOEXEC, exact known-PID waitpid, malformed/cookie/socket rejection,
subprocess fd preservation, real EAGAIN backpressure and peer close are checked.
Optimized and ASan/UBSan/LSan verification passed all nine groups with stderr0
and unchanged compile inputs. The source-level CEF hook itself remains unbound
and requires its separate exact compiled Source/process receipt proof.
