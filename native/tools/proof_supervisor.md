# Standalone proof supervisor

`suitecut-proof-supervisor` owns one isolated Linux subreaper domain before
launching a standalone checker. It preserves the command's argv, environment,
stdin, stdout and stderr. Its original child has one exact waitpid owner;
`DescendantOwner` reaps only genuinely adopted children after that child exits.
The optional before-wait observer records actual parent/PID/start identity
immediately before each adopted waitpid, including zombie processes.

The independent `--report` path records the original raw child waitpid status,
descendant receipts, timeout/interruption, and `cleanupFailed` separately.
Actual child signals are re-raised after cleanup. If the child exits0 but cleanup
requires SIGKILL or observes an abnormal descendant, the supervisor exits125;
the child's actual exit0 stays in the report. Observation failures remain errors
while ownership is retained for bounded cleanup. No successful retirement is
invented at a watchdog expiry. The cleanup budget does not extend test deadlines.

```sh
suitecut-proof-supervisor --report /tmp/new-supervisor-report.json \
  --timeout-ms 120000 --cleanup-grace-ms 1000 --physical-ms 6000 \
  -- node /absolute/original-checker.mjs
```

The process controls cover genuine fork/double-fork adoption, held and abnormal
descendants, signals/timeouts, inherited stdio/environment/arguments and an
observer exception that leaves its actual child owned until a later real reap.
No browser behavior is established by these controls. Existing PPID1 zombies
cannot be adopted or reaped retroactively by this utility.
