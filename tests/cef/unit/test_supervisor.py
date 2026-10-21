# SPDX-License-Identifier: MIT
"""Checks the regression runner's failure detection using real child processes."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/cef'))
from harness import supervisor as runner
from harness import evidence, reporting, catalogue



class SupervisorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cef-supervisor-test-')
        self.root = Path(self.temporary.name)
        runner.STOP.clear()

    def tearDown(self):
        runner.STOP.clear()
        self.temporary.cleanup()

    def run_child(self, source, timeout=5):
        return runner.supervised([sys.executable, '-c', source], self.root,
                                 self.root / 'attempt', dict(os.environ), timeout)

    def test_clean_child(self):
        result = self.run_child('print("real-child")')
        self.assertTrue(result['passed'], result)
        self.assertEqual(result['remainingProcesses'], [])
        self.assertEqual((self.root / 'attempt/stdout.log').read_text(), 'real-child\n')

    def test_fast_log_overflow_is_failed_even_after_exit(self):
        result = self.run_child('import os;os.write(1,b"x"*(17*1024*1024))')
        self.assertFalse(result['passed'])
        self.assertIn('Attempt log exceeded 16 MiB bound', result['errors'])

    def test_report_preserves_authored_unpaired_surrogates(self):
        value = {'authored': 'native-\ud800'}
        reporting.atomic_json(self.root / 'unicode.json', value)
        self.assertEqual(json.loads((self.root / 'unicode.json').read_text()), value)

    def test_nonzero_is_not_success(self):
        result = self.run_child('raise SystemExit(7)')
        self.assertFalse(result['passed'])
        self.assertEqual(result['exitCode'], 7)

    def test_timeout_kills_held_child(self):
        result = self.run_child('import time;time.sleep(30)', .2)
        self.assertFalse(result['passed'])
        self.assertIn('Hard attempt deadline exceeded', result['errors'])
        self.assertEqual(result['remainingProcesses'], [])

    def test_orphan_is_failed_and_retired(self):
        result = self.run_child('import subprocess,sys;subprocess.Popen([sys.executable,"-c","import time;time.sleep(30)"])')
        self.assertFalse(result['passed'])
        self.assertIn('Process descendants remained after the test exited', result['errors'])
        self.assertTrue(result['cleanup']['before'])
        self.assertEqual(result['remainingProcesses'], [])

    def test_interruption_is_not_success(self):
        runner.STOP.set()
        result = self.run_child('import time;time.sleep(30)')
        self.assertFalse(result['passed'])
        self.assertTrue(any('Interrupted' in e for e in result['errors']))
        self.assertEqual(result['remainingProcesses'], [])

    def test_zero_exit_with_failed_evidence_is_failed(self):
        (self.root / 'result.json').write_text(json.dumps({'passed': False, 'checks': ['not enough']}))
        with self.assertRaises(ValueError):
            evidence.validate_reports(self.root, {'kind':'journey','id':'test.a'})

    def test_process_inspection_failure_still_reaps_child(self):
        with patch.object(runner,'group_members',side_effect=RuntimeError('ps unavailable')):
            result=self.run_child('import time;time.sleep(30)',.2)
        self.assertFalse(result['passed'])
        self.assertNotEqual(result['exitCode'],None)
        self.assertTrue(any('observation failed' in e for e in result['errors']))

    def test_zero_exit_without_assertions_is_failed(self):
        (self.root / 'result.json').write_text(json.dumps({'passed': True, 'checks': []}))
        with self.assertRaises(ValueError):
            evidence.validate_reports(self.root, {'kind':'journey','id':'test.a'})

    def test_repetition_keeps_failure_in_junit(self):
        attempts = [dict(id='a', kind='journey',seed=i,repeat=i,passed=(i==2),errors=[] if i==2 else ['original failure'],directory=f'cases/a-{i}') for i in (1,2)]
        reporting.write_reports(self.root, dict(passed=False,attempts=attempts,durationSeconds=1,globalErrors=[]))
        xml = ET.parse(self.root / 'junit.xml').getroot()
        self.assertEqual(xml.attrib['tests'], '2')
        self.assertEqual(xml.attrib['failures'], '1')
        self.assertIn('original failure', (self.root / 'report.html').read_text())

    def test_global_integrity_error_is_red_in_junit(self):
        reporting.write_reports(self.root, dict(passed=False,attempts=[],durationSeconds=1,globalErrors=['input drift']))
        xml = ET.parse(self.root / 'junit.xml').getroot()
        self.assertEqual(xml.attrib['failures'], '1')

    def test_case_filter_and_empty_selection(self):
        cases = [dict(id='race.a',tags=['race'],kind='journey'),dict(id='proof.b',tags=[],kind='proof')]
        self.assertEqual(catalogue.selected(cases,'races',[]),cases[:1])
        with self.assertRaises(ValueError):
            catalogue.selected(cases,'full',['no-such-case'])
        self.assertEqual(catalogue.selected(cases,'smoke',['proof.*']),cases[1:])


if __name__ == '__main__':
    unittest.main()
