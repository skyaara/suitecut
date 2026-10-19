# Explicit root-script breakpoints

This slice adds exact current-root script breakpoints to the explicit native
debugger session. Matching eight-group browser verification passes on the sealed fb2068e8
candidate. The earlier debugger session proof is a separate scope.

The public methods are
`session.setBreakpoint({ scriptId, lineNumber, columnNumber }, options)` and
`session.removeBreakpoint(breakpoint, options)`. The returned opaque object
exposes only the actual resolved location. Native ownership validates the exact
session, Source epoch, root execution context and unique context identity. Script
IDs must come from an actual current `Debugger.scriptParsed` receipt. No URL
pattern, condition, cross-navigation persistence or breakpoint UI is included.

The native registry excludes attested factory FunctionLocation script identities
only. Matching author source bytes, sourceURLs or embedder/hash metadata cannot
attest native ownership and remain eligible. Renderer script tuples alone do
not authorize denying a mutation. Ownership is never inferred from a script
ID's spelling.

Each session retains at most 16 records; the process retains at most 128, including
pending set replies, removed IDs whose actual removal ACK has not arrived, and
successful replies with an unusable identity. Public deadlines remain original.
A late successful installation is removed using its actual returned native ID
before the developer domain lease may retire. Cleanup has a separate 30s native
physical watchdog. An unknown successful ID or failed removal requests posted
channel closure and keeps its record until an actual Inspector channel boundary.
A logical document change never impersonates that boundary.

Factory breakpoint pause routing retains priority, and closing this developer
session removes only its own IDs before releasing the shared Debugger lease.
Closing coverage cannot remove developer or factory breakpoints. Raw mutators
continue to conflict with internal ownership.

Behavior reference: [CDP Debugger domain](https://chromedevtools.github.io/devtools-protocol/tot/Debugger/),
`setBreakpoint` and `removeBreakpoint` public method contracts only. No external
breakpoint implementation was copied.

Controlled verification passes: the native store has 24 cases, the integrated
session controller 25, and the script inventory 16, each optimized and under
ASan/UBSan/LSan. These cover canceled/expired late set replies, actual remove ACKs,
unknown mutation results retained through real channel close, global/session
capacity, reentrant session replacement, and an author script whose metadata
collides with native metadata but lacks actual native attestation. The 27 SDK
cases, SDK build/type checks and scoped lint also pass.

`scripts/verify-native-developer-breakpoints.mjs` defines eight browser
checks using actual served source, paused frame IDs, author effects, coverage and
factory coexistence, installed-ID cleanup and constructor-abort cleanup. It
requires the matching compiled SDK/native, actual process exit and strict native
stderr. Its exact sealed SDK/native run passed all eight checks with empty native
stderr, actual helper exit 0/null and no remaining matching native processes.
All 1903 source/SDK/runtime inputs and 230 static assets matched before and
after the run. Held set-ACK cleanup is currently a controlled
native receipt test, not a claimed live browser gate.

The actual receipts show one installed native breakpoint with one sustained
pause, zero breakpoint records after remove while the pause remains, and zero
breakpoint/session/physical/receipt/pause ownership after close or constructor
abort. The factory lease remains one. Actual author effect37 completes after
constructor-abort resume. The deliberately retained opaque DataTransfer is
disposed before final helper exit. The immutable runtime index is
`/tmp/suitecut-artifacts-fb2068e8-breakpoint8-v1/runtime-index.json`, SHA256
`4186549a16ed74940445469c428b5099b477371b62da41b8431a4643401b2b38`.
