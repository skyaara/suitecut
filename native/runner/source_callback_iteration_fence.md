# Native Source callback-iteration fence

This adapter reuses the credentialed Source work channel without changing fd4
frames or enabling browser worker reuse. `BeginCallbackIterationFence` accepts
an exact native `CallbackIterationOwner` and a fixed physical-cleanup deadline.
Its `iterationOwner` hook must look up the actual scheduler/step records, including
assertion/invocation and the real callback owner. Author parent step zero is valid.
The caller must already hold real raw, SDK front-operation and descendant
step/file/publication drainage receipts; this adapter never infers that from
an empty JavaScript pending map. The caller's logical timeout remains independent
and cannot be reopened by a successful cleanup receipt.

The optional host-side `callbackIteration` purpose permits unrelated healthy
Source commands; the existing WorkerReuse sidecar is byte-for-byte unchanged.
After the genuine native cut and original stdout decoder observation, the host
must emit `SOURCE_WORK_READY ticket nonce` only once its raw barrier reply and
native ACK both settled. The adapter then requests authenticated Source RESUME,
waits for real RESUMED, and sends host `SOURCE_WORK_RESUME ticket nonce epoch`.
Only the actual SDK pending-clear acknowledgement
`SOURCE_WORK_HOST_RESUMED ticket nonce epoch` can complete the adapter fence.
Successful snapshots contain the exact cuts, issued/resume epochs and actual
native ingress timestamps; no raw-return timestamp is invented. Snapshot before
`FinishCallbackIterationFence`, which clears one completed challenge and permits
another under a strictly newer channel epoch. The 64-completed-history bound is
retained. Refusal, expiry, foreign receipts and partial send failures cannot erase
physical Source ownership. Canceled work retires through actual host waitpid,
descendant domain, channel EOF and pidfd exit. `SourcesPhysical` is never zeroed
by a successful work fence.

The native component uses real owned Host and exec Source children, the actual
adapter, SCM credentials, original component pipe observations, held host READY
and resume acknowledgements, and real waitpid/domain retirement. Its explicit
Source producer is a transport control, not CEF or Engine accepted-work proof.
The matching public retained-browser checker supplies that integrated evidence.
