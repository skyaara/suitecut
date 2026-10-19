# Native BackForwardCache launch policy

`launchNativeBrowser({ executablePath, backForwardCache: false })` disables
Chromium BackForwardCache through its actual native launch feature switch.
Omitting the option, or passing `true`, preserves the pinned browser's existing
cache policy and history behavior. This is a process launch choice, not a
per-page setting. It does not fabricate worker close events on navigation.

With the preserved policy, an old document's dedicated worker may remain alive
across navigation. An Inspector channel detach does not prove author execution
ended. Actual same-target reattachment can recover that worker's preserved author
state with a new genuine control generation. Code requiring navigation to destroy
that document's worker may explicitly disable the cache at launch.

The supplemental `verify-native-worker-cache-policy.mjs` checker exercises
omitted, true, and false policies against actual targets and author state. The
existing six-check worker lifetime verifier can explicitly select the disabled
public option with
`SUITECUT_NATIVE_WORKER_LIFETIME_CACHE_POLICY=disabled`; all of its lifecycle
assertions remain unchanged. Its default mode still selects the native default
policy. Reports include the selected policy and must not label a disabled-policy
success as a default-policy success.

The argument-only diagnostic on the immutable c6 binary established six passing
lifetime assertions with BackForwardCache disabled, while the preserved native
default retained a live target and author value after navigation. The typed SDK
option requires matching compiled SDK/browser evidence before its public-policy
checker is claimed to pass. No default-policy change is implied by that probe.
