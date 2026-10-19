# Final fail-on-flaky policy

`--fail-on-flaky-tests` fails the final public native run when its validated
`Summary.flaky` is nonzero. It runs after actual execution and physical drain.
It does not change retries, attempts, expected/unexpected classification,
max-failure accounting, project dependencies, serial group outcomes or retention.
Internal actors clear the option; the outer directory/repeat/group coordinator
sums each actual source summary, without combining retry histories across sources.
Missing selections contribute no invented flaky executions.

The enabled-only native `runPolicy` record contains `failOnFlakyTests: true`,
`flakyTests` and `failed`. Both derived values are recomputed and validated when
reading a native source. The absent option preserves previous report bytes.
JUnit adds policy properties only, with no new testcase, error or global error;
HTML displays an explicit native policy banner. Actual authored outcomes remain
unchanged.

The native merge tool accepts the same flag as an explicit aggregate policy.
That policy returns exit 1 only for actual flaky tests. Original source policies
still contribute to source run-failure metadata, and `--fail-on-run-failure`
retains its existing opt-in exit behavior. A source policy and aggregate policy
never count a source's flaky tests twice.
