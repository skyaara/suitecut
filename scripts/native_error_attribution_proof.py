# SPDX-License-Identifier: MIT
"""Assertions for genuine attempt-associated diagnostics in native process proofs."""


def assert_attempt_secondary(report, error, *, origin='phaseResult', first_line=False,
                             attempt_index=0, scope_id='1', scope_generation='1', scopes=None):
    attempt = report['attempts'][attempt_index]
    worker = next(row for row in report['workers']
                  if row['workerIndex'] == attempt['workerIndex'] and row['pid'] == attempt['pid'])
    assert worker['exitStatusKnown'], worker
    errors = attempt.get('secondaryErrors', [])
    expected_scopes = {(scope_id, scope_generation)} if scopes is None else scopes
    for row in errors:
        assert all(row[field] == attempt[field]
                   for field in ['testId', 'generation', 'workerIndex', 'pid']), row
        assert row['origin'] in ['phaseResult', 'fixtureRaw'], row
        assert isinstance(row['error'], str) and row['error'], row
        assert (row['scopeId'], row['scopeGeneration']) in expected_scopes, row
        if 'originatingStepId' in row:
            step = next(step for step in report['steps'] if step['id'] == row['originatingStepId'])
            assert all(step[field] == row[field] for field in
                       ['testId', 'generation', 'workerIndex', 'pid', 'scopeId', 'scopeGeneration']), row
    # Authored Error stacks remain complete in the report. Existing fixtures
    # declare their exact first line; generated native diagnostics are scalars.
    matching = [row for row in errors if row['origin'] == origin and
                row['scopeId'] == scope_id and row['scopeGeneration'] == scope_generation and
                (row['error'].split('\n', 1)[0] if first_line else row['error']) == error]
    assert len(matching) == 1, (error, errors)
    return matching[0]
