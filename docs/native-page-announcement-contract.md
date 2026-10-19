# Canonical Page adoption and actual announcement

An actual context route owner may expose a Page before Chromium/CEF's public
Page-ready announcement. That early observation adopts the canonical wrapper;
it does not publish a `page` or `popup` event or infer an opener.

A later genuine native `page-created` event supplies the actual opener metadata
and publishes the same wrapper exactly once. The per-Page announcement receipt
is set before calling user listeners, so reentrant or duplicate delivery does
not publish a second event. Conflicting actual opener metadata is rejected.
The receipt is a boolean in the existing weak Page binding, not another history
or target map. A Page retained in the existing actual-close book cannot be
resurrected by a late announcement; context closure also rejects new adoption.

This SDK repair does not reconstruct a missed native initial request. It does
not infer Page readiness from network completion, compare Source/broker event
sequences, or change the native popup/navigation policy. Initial popup network
capture and context-network projection before a native facade is available
remain separate browser proof scopes. Controlled tests cover canonical early
adoption followed by a genuine announcement, reentrant/duplicate delivery,
actual opener consistency and actual-close-before-announcement.

User listener exceptions do not interrupt actual Page/popup or Page-close
notification and cleanup. Each snapshot listener is attempted; its original
thrown value is rethrown in a microtask after synchronous lifecycle dispatch.
This preserves ordinary uncaught user errors outside the protocol decoder.
An isolated actual Node child verifies canonical Page/popup delivery, duplicate
idempotence, all close listeners and actual closed-book cleanup while observing
the original page, popup and close exceptions through `uncaughtException`.
