# Native locator highlight

The public slice provides `NativeLocator.highlight()` returning an asynchronous disposable and `NativeLocator.hideHighlight()`. It selects a genuine Element through the existing native query/world/adoption path, obtains its actual backend node identity, and calls Chromium's Overlay domain. It inserts no author DOM or CSS.

Chromium exposes one node-tool per actual Inspector session. This implementation explicitly rejects arbitrary multiple matches and the optional arbitrary CSS `style` argument. Different in-process frames share a session and replace its prior node-tool; a separate OOPIF session has its own native tool. Empty queries clear the current session's tool. Chromium also hides its node-tool on mouse movement; this initial slice does not claim persistent painting across pointer movement. Native diagnostic lease counts describe retained ownership, rather than asserting that every pixel remains visible or that an invisible/offscreen node has a rendered box.

The pinned public documentation is commit `606ecd8b34bac100db32561b6278ffd76e5eb3cd`, class-locator highlight and hideHighlight sections. The pinned public locator-highlight test additionally checks implementation-specific `x-pw-*` DOM nodes, which this native implementation does not create. No original test assertions are changed. Protocol/source contracts were read at Chromium `156.0.8078.17`, `Overlay.pdl`, `inspector_overlay_agent.cc` and `inspect_tools.cc`; no upstream implementation was copied.

## Native ownership

`native_highlight_source.inc` is the Source include seam. Full actual frame/session/loader, numeric and unique main/utility contexts, and native document generation are checked before each new publication boundary, with original expiry checked first. A bounded controller-owned sender retains accepted Inspector calls through logical cancellation and actual acknowledgments. True channel destruction is the alternative physical retirement boundary. Teardown failures request the existing posted native fatal-close path; the watchdog never fabricates an acknowledgment.

A source-specific monotonically increasing opaque token is delivered after the actual highlight acknowledgment. The SDK must claim it before its original deadline and a maximum one-second unclaimed delivery TTL. Failed or canceled delivery retires the actual tool. A stale token cannot remove its replacement. Disposal follows the token's actual Source/session rather than reacquiring a potentially changed selector or frame. Hide and claim use the existing bounded native control lane, so ordinary evaluation saturation cannot consume their admission quota.

`DOMDomainLeases` shares real DOM.enable/disable ownership between native Overlay, CSS coverage and explicit raw DOM users. The last owner waits for the actual disable acknowledgment. A separate 128-waiter bound rejects release admission before mutating the owned lease, preventing canceled enable/release cycles from growing retained callbacks indefinitely. Fatal settlement preserves actual ownership while disabling further polling; only the real receipt or channel destruction retires it. A late enable acknowledgment still reaches a canceled coverage owner so its physical cleanup is preserved. Raw DOM.disable and mutating Overlay commands cannot defeat other active native owners. Raw Overlay ownership established before a native show is preserved by rejecting that show until the raw caller disables its own domain.

## Source integration contract

The narrow hooks are in Source command admission/dispatch, Complete cancellation, deadline Tick/NeedsTick, actual frame context changes and handle-frame invalidation, real child session detach, real main-renderer death, OnBeforeClose, raw DOM/Overlay admission, and diagnostics. CSS coverage's Source sender routes only DOM.enable/disable through the shared lease. Its generic standalone fallback and CSS/Profiler/Debugger algorithms remain unchanged. SDK changes are the command union/control classifiers, locator API wrappers, exported types and tracing metadata.

## Verification status

The prior capability probe on exact `fcbaec21` observed native CEF BGRA pixels change at a real backend node and restore after actual Overlay teardown, while author HTML remained identical. It is a capability proof, separate from this new Source pipeline.

The standalone controlled tests cover 12 shared-domain lifecycle cases and 15 native highlight controller cases using actual CEF dictionaries. SDK tests cover 10 packet/delivery/disposer contracts. The public `verify-native-locator-highlight.mjs` checker additionally requires real OSR pixels, shadow/OOPIF selection, CSS sharing, raw ownership, renderer-held original deadlines and actual unclaimed-delivery cleanup. A matching compiled Source/public runtime pass is required before claiming that pipeline is supported; it has not been executed at this commit's preparation time.
