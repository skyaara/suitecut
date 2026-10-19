# Native callback retry verification

Status: the first slice has composed actual acceptance at production `a238fec`.
Eight affected registered checks and four affected public scenarios pass on its
common build; the earlier first three public scenarios retain separate `14b8e08`
positives. This is not a fresh full seven-scenario or full-suite replay. Exact
scope, original negatives and independent review are recorded in
`.suitecut/diagnostics/native-to-pass-a238fec-result-v1.json`.

`check-native-runner-to-pass.py` owns seven serial native runner invocations.
It does not install a bridge, manufacture a callback receipt, drive a retry
loop, or choose a native verdict. Author counters change genuine conditions;
actual `callbackAssertions` and native iteration `StepRecord` metadata determine
dispatch count, ownership, backoff and completion. No callback is inferred from
a step title or numeric ID range.

| Scope | Actual evidence |
| --- | --- |
| Hooks/body/nested steps, outer retry, saved assertion and stale async context | Genuine callback throws then returns; native intervals/count; real hook owner; fresh outer retry generation/worker; later live owner accepts a saved assertion while old async context invokes no callback. |
| Ordinary unawaited attachment failure, within that first scope | The explicit nested step genuinely returns before its missing-input file fence fails; failed native raw-P step history survives, then the callback retry recovers. Later committed bytes match device/inode; TestInfo attachment remains test-level and captured step attachment keeps its original public step. Plain attach does not invent an append declaration. |
| Append admission recovery and skip, within that first scope | Five body declarations queued synchronously precede any OPEN-driven CHUNK/FINISH, so the existing four physical slots admit four real writers and reject the fifth without a job. All five declaration facts survive; retry recovers before genuine local and test skip. No timer or injected pause establishes the capacity condition. |
| Fixed explicit deadline | Exactly one actual throw when the next configured interval exceeds the original fixed cap; exact original error identity is rethrown and caught without changing the authored attempt. |
| Raw-held logical timeout | Public rejection precedes actual raw settlement; observer records a genuine live owned host PID/start/PPID before releasing its file gate; afterEach and next test run only after settlement. |
| Omitted and explicit-zero timeout | Both retain their actual original test deadline, create no independent assertion cap, and cannot become passing attempts after native cancellation. |
| Original global deadline and cleanup | Genuine global interruption retains the raw promise; afterEach starts only after actual body retirement, under its original cleanup grant, and can admit a new bounded cleanup assertion. |
| Never-settling raw callback | Actual forced worker signal9 retires physical work; expectedFailure does not hide the native cleanup infrastructure error and no raw receipt is invented. |
| Actual host exit17 | Genuine process death remains an infrastructure failure with no accepted callback success or fabricated settlement. |

Every invocation uses the existing native supervisor with its original45s
command,1s cleanup and10s physical guards. The444s CTest guard includes all seven
scopes and52s orchestration. The driver waits the actual supervisor before
raising observation failures, retains first failures and hashes the exact
fixtures, executables, helper and SDK inputs. PID absence includes zombies.
`--case` can select an affected/unrun tail; it never rewrites assertions or
extends budgets. `--prepare-only` writes fixtures without starting a runner.

These are CEF-free author/step/file/process controls. The separate genuine
Source acceptance checker owns registered Source CUT/ACK/RESUME evidence. They
do not claim generic `expect.poll`, all-hook/fixture support, nested toPass,
`.not`, `.soft`, callback custom messages or snapshot baseline IO.
