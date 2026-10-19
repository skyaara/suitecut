# Native context video

`recordVideo: {dir, size?}` enables the existing native VP9/WebM recorder for each
actual page, including popups, before its page descriptor is published. It uses
actual Source paint bytes and the existing native FFmpeg subprocess owner. No
frame, encoder-completion receipt, or page event is synthesized. A native startup
failure rejects an explicit new-page request; an unpublished popup has no success
event and its waiter may reach its original timeout. The first actual native
admission/encoder/final-file error is also retained for context close.

The default canvas fits the initial actual CSS viewport within 800 by 800.
An explicit VP9/WebM canvas accepts integer width 1..3840 and height 1..2160,
including odd dimensions. Every genuine native BGRA paint carries its current
physical and native `GetViewRect` CSS extents before the legacy fixed-capture fit.
CSS dimensions are retained with the paint rather than reconstructed by dividing
rounded physical pixels by DPR. Late paints whose dimensions do not match the
current reported native view are excluded from video input. CEF does not expose
a paint resize generation when distinct CSS sizes round to identical physical
sizes, so that collision follows the current native reported view. CSS extents
up to 15360 by 8640 accommodate accepted DPR0.25 viewports while actual paint
memory remains bounded to 3840 by 2160 physical pixels. The recorder's
worker fits that paint into the fixed video canvas, never enlarges CSS content,
and anchors it at the top left with black padding. Later viewport resizes use
the current paint geometry, so they no longer inherit the launch capture's
centered fit or initial CSS content limit. One bounded newest-paint slot discards
superseded paints before scaling; disabled/manual recording paths do not copy
raw paints. The actual native Source frame clock, resampling and original
first-frame/close deadlines remain authoritative. Manual recordings retain
their previous even canvas, pixel format and geometry defaults. For launch/default persistent-context recording, supply `recordVideo` on
`launchBrowser` or `launchNativeBrowser`; see [launch video](launch_video.md).

`page.video()` returns the actual recording descriptor or null when disabled.
`path()`, `saveAs(path)` and `delete()` may wait before page/context closure.
Their timeout-zero logical waits do not prevent recording finalization or
context close. File phases run on the existing bounded native background owner,
with a 60-second phase cap and any stricter original native request deadline.
Deadline/cancel checks bracket native file syscalls; a blocking kernel call is
not synchronously interruptible, and its actual owner remains retained until it returns.
Cancellation removes publication permission but retains the real job and file
references until the actual queued callable and its captures are destroyed.
Operations on one video run in native FIFO order. `saveAs` creates parent
directories and atomically renames a fully copied staging file; cancellation or
an error after rename can leave that real side effect and returns failure.

Preparation makes a unique canonical private directory below `dir` and retains
its actual directory FD. Finalization seals the actual regular file, byte count
and inode only after the existing recorder has joined its encoder. Copies read
that retained FD; path/deletion reject a replacement inode. External filesystem
writers are not serialized with validation and rename/unlink syscalls; these
checks do not claim an atomic exclusion boundary against unrelated writers.
Deletion retains the actual unlink fact even if a later durability check fails.
The first implementation is Linux/POSIX native file IO, not remote-browser
artifact downloading.

The registry retains up to 256 undeleted video identities across backend
metadata eviction and page/context removal; it refuses new admission rather
than evicting such handles. Completed, explicitly deleted rows can be reclaimed
on new admission. The most recent 256 reclaimed deleted identities keep
idempotent deletion; older deleted handles reject as unavailable. At most 64
logical waiters and eight actual file jobs are retained. Video records remain
usable after page/context close while the original browser transport exists.
The default disabled path starts no encoder or file job, emits no video
metadata, and sends no additional command.

`context_video_test.cc` exercises real bounded native files and the controller
using a controlled recorder terminal hook and controlled background/owner
queues. It is not a browser/paint/codec acceptance test. The separate existing
RecordingControls controls use actual FFmpeg; neither control substitutes for a
matching production Source/browser lifecycle run. The composed `bf480072`
browser binary passed a live scope covering real page, popup and immediately
closed-page videos, post-close file operations, VP9 dimensions/frame counts,
and decoded top-left content/black padding. That historical scope predates the dynamic viewport/odd-canvas implementation;
those changes have compile checks but no new browser acceptance run. Launch-context
recording was outside that historical scope.
