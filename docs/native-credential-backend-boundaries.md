# Native credential support and backend dependencies

This audit covers pinned CEF
`156.0.7+g47dbc1d+chromium-156.0.8078.17`. TLS client certificates and WebAuthn
virtual credentials use separate browser subsystems. Neither missing capability
means ordinary TLS, HTTP authentication or sequential native WebAuthn is absent.

## What the engine already owns

- `CredentialControls` serializes configuration mutations, observation commits,
  deletion, capture and cancellation/rollback within each browser context.
- `CredentialPage` creates the genuine main-frame WebAuthn virtual
  authenticator. Descendant frames inherit that browser-side environment. Each
  clear, import and removal retains its callback until the actual native reply.
- Context state commits after the current page fanout acknowledges. An observed
  credential can adopt the originating page's actual device state without
  reimporting it into that same page.
- New document initialization waits on the browser-owned credential-readiness
  resource before authored startup code executes. This is a document startup
  barrier; it does not stop an existing document's ongoing ceremony.
- Separate contexts have separate canonical stores and operation queues. The
  pending concurrency limitation concerns overlapping ceremonies and mutation
  synchronization sharing one context's credential state.

The dequeue-time observation check also removes redundant work: if an earlier
acknowledged observation already committed the same credential identity and an
equal or newer counter, the queued observation completes without another
snapshot/version publication or native clear/import fanout. This only reuses
already committed genuine state; it does not acknowledge an uncompleted native
operation or synthesize an assertion counter.

Implementation: `native/browser/credential_controls.cc`, `credential_page.h`,
`credential_source.inc`, `credential_ready.h`, and `renderer_script_sources.h`.

## Shared-context concurrent ceremonies require a browser-side contract

Chromium's pinned `AuthenticatorEnvironment` stores virtual authenticator
managers in a map keyed by `FrameTreeNode*`. Lookup walks actual parent frames,
not other top-level pages sharing a `BrowserContext`. Therefore two separate
pages in the same SuiteCut context use distinct native virtual authenticator
stores which the engine currently synchronizes.

Pinned upstream sources:

- [AuthenticatorEnvironment declaration](https://chromium.googlesource.com/chromium/src/+/refs/tags/156.0.8078.17/content/browser/webauth/authenticator_environment.h)
- [Manager creation and parent-frame lookup](https://chromium.googlesource.com/chromium/src/+/refs/tags/156.0.8078.17/content/browser/webauth/authenticator_environment.cc)

Queuing SDK configuration commands does not serialize browser-side
`navigator.credentials.create/get` already running in other pages. Pausing
renderer JavaScript likewise does not prove an ongoing browser-side
authenticator transaction has retired. Independent copies can both assert from
the same starting counter; taking the maximum observed count cannot establish
one shared authenticator's atomic counter history. Clear/import synchronization
can also overlap a new native registration.

Completing this feature needs an actual browser-side shared authenticator/store
or a transaction/admission barrier which covers all ceremony producers and
supplies physical completion before mutation. After that backend contract is
available, our adapter must bind it to context ownership, cancellation, page
attachment and counter publication. No such backend patch is included in the
current isolated bundle.

The current engine detects a genuine ceremony overlapping that page's active
credential synchronization and closes the affected page rather than accepting
incoherent material. This restriction remains explicit;
`concurrentCredentialCeremonies` stays false. It should not be listed as an
unimplemented SDK queue or as a confirmed general CEF browsing bug.

## TLS client-certificate import requires a public CEF API extension

CEF exposes `CefRequestHandler::OnSelectClientCertificate` and
`CefSelectClientCertificateCallback::Select`. The supplied list contains
Chromium-owned certificates already filtered by the server's trusted issuers.
`CefX509Certificate` exposes certificate metadata and encoded certificate chains;
it does not expose private-key import or a signing-provider callback.
`CefRequestContext` does not provide a context-local PEM/PFX/private-key import
method in this pinned API.

Pinned upstream sources:

- [Client-certificate callback](https://github.com/chromiumembedded/cef/blob/47dbc1df023e99f006fb9e850a8d83cc19b4fdd9/include/cef_request_handler.h)
- [Certificate interface](https://github.com/chromiumembedded/cef/blob/47dbc1df023e99f006fb9e850a8d83cc19b4fdd9/include/cef_x509_certificate.h)
- [Request-context API](https://github.com/chromiumembedded/cef/blob/47dbc1df023e99f006fb9e850a8d83cc19b4fdd9/include/cef_request_context.h)

Selecting a certificate that already exists in an external/platform store is
different from importing user-supplied certificate/key material into an isolated
automation context. A custom `CefX509Certificate` implementation containing DER
bytes cannot provide Chromium's missing private-key ownership/signing contract.
Server-certificate bypasses, shared global certificate-store changes or an
external TLS proxy do not establish this requested context-local feature.

The backend extension needs actual per-request-context credential/key ownership,
origin/proxy selection rules, browser-side signing, key cleanup, and real TLS
handshake completion. Our remaining adapter work starts once that interface
exists. No client-certificate import backend patch is currently in the isolated
bundle; `clientCertificateImport` remains false.

## Verification scope

The duplicate-observation regression uses actual CEF dictionaries and the native
credential/key store. It holds the component's queued transport callback, queues a
second observation while the canonical counter is still old, and verifies that
the first acknowledgment commits the counter and releases a queued read without
dispatching another fanout. It also retains the existing mutation rollback and
physically retained cancellation checks in that component.

This component verification does not establish concurrent live ceremonies, TLS
client-certificate handshakes, or a custom CEF implementation. Broad end-to-end
and fuzz verification remain deferred by user direction.
