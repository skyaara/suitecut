# SPDX-License-Identifier: MIT
"""Negative controls for false-green, stale and malformed qualification evidence."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tests/cef'))
from harness.catalogue import catalogue, selected, validate_catalogue
from harness.contracts import check_contract, load_json, validate_contract
from harness.evidence import validate_reports
from harness import evidence


class EvidenceContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.contract = {'file': 'artifacts/results.json', 'rules': [
            {'path': 'ok', 'equals': True}, {'path': 'checks', 'minItems': 2}]}
        self.case = {'kind': 'cpp', 'evidence': self.contract}
        self.good = {'ok': True, 'checks': ['real first assertion', 'real second assertion']}

    def write(self, value):
        path = self.directory / self.contract['file']
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value))
        return path

    def test_valid_declared_evidence(self):
        self.write(self.good)
        self.assertEqual(validate_reports(self.directory, self.case), 2)

    def test_unrelated_json_does_not_satisfy_case(self):
        (self.directory / 'unrelated.json').write_text(json.dumps(self.good))
        with self.assertRaises(ValueError):
            validate_reports(self.directory, self.case)

    def test_empty_or_truncated_report_is_rejected(self):
        for value in ({}, {'ok': True}, {'ok': True, 'checks': []}, {'ok': True, 'checks': ['partial']}, []):
            with self.subTest(value=value), self.assertRaises(ValueError):
                check_contract(value, self.contract)

    def test_boolean_success_cannot_be_string_or_integer(self):
        for value in (1, 'true', False, None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                check_contract(dict(self.good, ok=value), self.contract)

    def test_failed_embedded_assertion_is_rejected(self):
        with self.assertRaises(ValueError):
            check_contract({'ok': True, 'checks': ['first', {'passed': False}]}, self.contract)

    def test_error_arrays_override_success(self):
        for key in ('failures', 'cleanupFailures', 'cleanupErrors', 'unhandled'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                check_contract(dict(self.good, **{key: ['failure']}), self.contract)

    def test_all_scenarios_must_explicitly_succeed(self):
        contract = {'file': 'result.json', 'rules': [{'path': 'cases', 'allTrue': 'ok'}]}
        for rows in ([], [{'ok': True}, {}], [{'ok': True}, {'ok': False}], [{'ok': 1}]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                check_contract({'cases': rows}, contract)

    def test_known_unobserved_boundary_is_allowed_but_never_required(self):
        contract = {'file': 'report.json', 'rules': [{'path': 'unexercised', 'allowedItems': ['known ordering']}]}
        check_contract({'unexercised': []}, contract)
        check_contract({'unexercised': ['known ordering']}, contract)
        for current in (['new silent skip'], [False], 'known ordering'):
            with self.subTest(current=current), self.assertRaises(ValueError):
                check_contract({'unexercised': current}, contract)

    def test_new_and_removed_fixture_inputs_change_inventory(self):
        suite = self.directory / 'suite'
        suite.mkdir()
        sdk = self.directory / 'sdk'
        sdk.mkdir()
        (sdk / 'native.js').write_text('// SDK')
        engine = self.directory / 'engine'
        engine.write_text('test executable identity')
        args = SimpleNamespace(engine=engine, sdk=sdk, node='node', ffmpeg=None, ffprobe=None, client_build=None)
        with patch.object(evidence, 'SUITE', suite), patch.object(evidence, 'ROOT', self.directory):
            before = evidence.pin_inputs(args, [{'kind': 'journey'}])
            fixture = suite / 'new-fixture.html'
            fixture.write_text('<!doctype html>new real app')
            after = evidence.pin_inputs(args, [{'kind': 'journey'}])
            self.assertNotEqual(before, after)
            self.assertIn(str(fixture), after)
            fixture.unlink()
            self.assertEqual(before, evidence.pin_inputs(args, [{'kind': 'journey'}]))

    def test_bad_contracts_fail(self):
        for patch in ({'file': '../old/report.json'}, {'file': '/tmp/report.json'}, {'rules': []},
                      {'rules': [{'path': 'ok', 'equal': True}]},
                      {'rules': [{'path': 'checks', 'minItems': True}]},
                      {'rules': [{'path': 'ok', 'equals': True, 'nonEmptyString': True}]}):
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                validate_contract(dict(self.contract, **patch))

    def test_symlink_to_previous_run_is_rejected(self):
        with tempfile.TemporaryDirectory() as previous:
            old = Path(previous) / 'report.json'
            old.write_text(json.dumps(self.good))
            target = self.directory / self.contract['file']
            target.parent.mkdir(parents=True)
            target.symlink_to(old)
            with self.assertRaises(ValueError):
                validate_reports(self.directory, self.case)

    def test_duplicate_json_keys_and_nan_are_rejected(self):
        for text in ('{"ok":false,"ok":true}', '{"checks":NaN}', '{"checks":Infinity}'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                load_json(text)

    def test_malformed_json_is_rejected(self):
        self.write(self.good).write_text('{')
        with self.assertRaises(ValueError):
            validate_reports(self.directory, self.case)

    def test_checks_stay_active_with_optimized_python(self):
        command = ('from harness.contracts import check_contract;'
                   'check_contract({"ok":1},{"file":"report.json","rules":[{"path":"ok","equals":True}]})')
        result = subprocess.run([sys.executable, '-O', '-c', command], cwd=ROOT / 'tests/cef', capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'ValueError', result.stderr)

    def test_journey_identity_seed_and_cleanup_are_required(self):
        good = {'id': 'test.a', 'seed': 23, 'passed': True, 'checks': ['outcome'], 'errors': [],
                'cleanupErrors': [], 'unhandled': [], 'processExit': {'code': 0, 'signal': None},
                'nativeStderrBytes': 0, 'capabilities': {'backend': 'cef'}}
        case = {'kind': 'journey', 'id': 'test.a'}
        path = self.directory / 'result.json'
        path.write_text(json.dumps(good))
        self.assertGreater(validate_reports(self.directory, case, 23), 0)
        for patch in ({'id': 'test.other'}, {'seed': 24}, {'processExit': {'code': 0, 'signal': 'SIGTERM'}}, {'cleanupErrors': ['leak']}):
            path.write_text(json.dumps(dict(good, **patch)))
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                validate_reports(self.directory, case, 23)


class CatalogueContracts(unittest.TestCase):
    def setUp(self):
        self.case = {'id': 'test.one', 'kind': 'journey', 'timeout': 90, 'tags': ['smoke']}

    def test_duplicate_ids_fail(self):
        with self.assertRaises(ValueError):
            validate_catalogue([self.case, self.case.copy()])

    def test_bad_fields_budgets_and_dependencies_fail(self):
        for patch in ({'timeuot': 90}, {'timeout': True}, {'timeout': 0}, {'timeout': 1801},
                      {'id': '../escape'}, {'tags': []}, {'tags': ['smoke', 'smoke']},
                      {'requires': ['unknown-tool']}, {'kind': 'skipped'}):
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                validate_catalogue([dict(self.case, **patch)])

    def test_missing_cpp_evidence_fails(self):
        case = dict(self.case, kind='cpp', binary='workflow')
        with self.assertRaises(ValueError):
            validate_catalogue([case])

    def test_unknown_selector_fails_even_with_another_valid_selector(self):
        with self.assertRaises(ValueError):
            selected([self.case], 'full', ['test.*', 'typo.*'])

    def test_real_catalogue_is_complete_and_valid(self):
        cases = catalogue('node')
        self.assertEqual({kind: sum(c['kind'] == kind for c in cases) for kind in ('journey', 'proof', 'cpp')},
                         {'journey': 29, 'proof': 56, 'cpp': 5})
        for case in cases:
            if case['kind'] != 'journey':
                validate_contract(case['evidence'])


if __name__ == '__main__':
    unittest.main()
