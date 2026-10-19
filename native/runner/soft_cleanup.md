# Soft assertion ownership during native cleanup

The original suite deadline still caps body and beforeEach soft work. A genuinely admitted afterEach uses its existing original cleanup phase deadline, capped by the already established suite-stop physical cleanup deadline. No clock is restarted by soft admission or settlement.

Admission after suite stop requires a live exact afterEach context, and worker closing/kill state still rejects new work. Mismatch association validates the actual current Invoke lease, token, scope and generation before accepting prior terminal body status in cleanup. The attempt remains interrupted or timed out; cleanup mismatch is recorded separately, and actual callback/file/soft retirement still precedes worker exit.

The public control uses the existing source-issued matcher receipt producer, not a DOM matcher or a fabricated native ACK. Original phase-info and live-timeout checkers are unchanged. The first authored soft cleanup negative is preserved in the handoff evidence.

A separate existing limitation remains: another synchronous invocation preparation failure (for example a capacity/allocation exception after the phase enters Invoke but before CALL is sent) can strand that phase until physical retirement. This repair removes the demonstrated expired-suite-clock cause; it does not invent raw callback settlement for generic preparation exceptions.
