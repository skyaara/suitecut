# Native assertion-deadline receipts

This following feature distinguishes an assertion that reaches its own original
native deadline without an admitted observation from an observed mismatch. The
first Source slice is limited to direct utility-world scalar DOM assertions;
other assertion paths keep their existing failure behavior. A bare transport
timeout, Error constructor, message or class name is insufficient
evidence. The Source-owned matcher entry must issue the typed deadline result
only after its actual query/group release acknowledgments retire. Until then,
the original runner soft lease remains physically pending.

The private SDK producer retains the original Error and exact invocation
receipt. It settles `assertionDeadline` only for that genuine producer; the host
marshals native `SOFT_SETTLED ... D ...` and waits for the real native ACK. It
cannot reinterpret an ordinary mismatch receipt as a deadline receipt.

Native C++ stores distinct `recognizedDeadline` raw outcome and
`acceptedDeadline` receipt. JSON and JUnit expose `acceptedDeadline: true` and
`acceptedMismatch: false`; HTML names the distinct receipt. No observed value is
invented. A typed accepted-soft-failure predicate covers both mismatch and
accepted deadline for attempt/step outcomes, history capacity and primary soft
error selection. The original Error/stack, call-site and actual step provenance
remain retained separately from the native receipt classification.

The existing exact callback, test, global attempt generation, worker/PID and
step ownership checks apply. A callback or step original deadline that expires
before the actual typed matcher receipt yields Late, and genuine native
cancellation yields Canceled. Neither is an accepted soft failure. Global stop
and raw hard/cleanup infrastructure errors keep their current precedence. Real
admitted afterEach cleanup uses its original cleanup clock as fixed by the
native global-stop repair; no budget is reset or extended here.

BeforeEach accepted deadline permits the real body callback to execute; raw
callback PASS remains distinct from the eventual failed test. A softly failed
step still resolves its genuine callback result only after the joint raw,
soft, file and append fences. Expected-failure classification applies to a body
soft failure, while an afterEach cleanup soft failure remains infrastructure
failure. The 64 physical lease and 32 per-attempt accepted failure bounds remain
shared across mismatch and deadline kinds, with no silent overflow drop.

The Source reply must remain held until physical retirement. Earlier logical
publication would require a separate authenticated physical-fence protocol and
is outside this slice. Original browser soft-six assertions and their 40 ms
budgets remain unchanged; a prior untagged timeout is not retroactively renamed.
