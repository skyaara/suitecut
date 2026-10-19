# Native callback retry scheduling

`CallbackRetrySchedule` is an inactive scheduling component. It does not install
`expectNative(callback).toPass()` or provide callback, Source or file retirement.
The public adapter remains pending those genuine physical ownership joins.

The native adapter supplies the first genuinely admitted iteration step as the
assertion identity. Each later invocation uses another real native step and an
exact assertion/iteration/step tuple. All steps retain the original callback's
test, attempt, worker, PID and hook identity. Existing native step record,
metadata and admission budgets remain authoritative; this component adds no
parallel iteration history or independent run quota.

The component owns the fixed assertion origin, optional assertion deadline,
current owner deadline, attempt count, one current invocation and backoff. The
default intervals are 100, 250, 500 and 1000 milliseconds, repeating the final
interval. An explicit interval list contains 1–64 nonnegative safe integers.
Timeout zero has no separate assertion deadline. All values are validated
natively. A large interval saturates at the remaining original cap before
addition, rather than overflowing or creating an arbitrary duration limit.

A raw callback receipt moves an invocation into draining, not into another
invocation. Only its actual joint physical receipt permits success or schedules
backoff. The adapter must join descendant steps, soft operations, attachment
writers/publication and authenticated Source cutoff/resume receipts. Ordinary
errors from that exact iteration can cause another invocation; true native
infrastructure failures remain latched independently of a later raw return.
Public attachment ownership must not be reassigned to iteration steps.

Timeout and cancellation may reject logically while the current raw invocation
remains physical. Only its actual physical settlement or the caller's exact
host waitpid receipt releases that ownership. The adapter retains the existing
fixed cleanup grace and must never substitute a Promise race, SDK map removal,
pidfd readability or Source shutdown label for real retirement.

Owner deadline amendments cannot revive an expired operation. Backoff retains
its original physical-settlement origin even if the owner deadline is extended
after an earlier wake was clamped. `NextWake()` exposes the next actual native
deadline or backoff boundary; it does not add an SDK timer or polling loop.

The 21 component controls cover these scheduling and typed-identity boundaries.
They use controlled native state, not actual worker/process receipts. They do
not establish activated public API, Source barriers, attachment attribution,
whole-run performance or browser reuse.
