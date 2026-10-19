#!/usr/bin/env python3
"""Independently decode the native reporter component's real file outputs."""
import argparse
import json
from decimal import Decimal
import pathlib
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--directory', type=pathlib.Path, required=True)
parser.add_argument('--executable', type=pathlib.Path)
args = parser.parse_args()
if args.executable:
    subprocess.run([str(args.executable.resolve()), str(args.directory.resolve())], check=True)

report = json.loads((args.directory / 'results.json').read_bytes())
assert report['version'] == 1
assert report['summary'] == dict(tests=6, attempts=7, expected=2, unexpected=3,
                               skipped=1, flaky=1, didNotRun=0, globalErrors=1,
                               totalAttemptDurationMs=23)
first = report['attempts'][0]
assert first['testId'] == '18446744073709551614'
assert first['generation'] == '1'
assert first['title'] == 'retry <& \U0001f680 \ud800 \x00'
assert first['error'] == 'first actual error <&\nline\t\r'
assert report['workers'][0]['stdout'] == dict(encoding='hex', byteLength=7,
                                            data='00ff3c3e260d0a')
assert bytes.fromhex(report['workers'][0]['stderr']['data']) == b'\xfe\0'
assert report['workers'][1]['exitCode'] is None
assert report['workers'][1]['signal'] == 15
assert report['workers'][2]['exitStatusKnown'] is False
assert report['workers'][2]['exitCode'] is None
assert report['workers'][2]['signal'] is None
assert report['globalErrors'] == ['global \udfff \ufffe <& \x01']

root = ET.parse(args.directory / 'results.xml').getroot()
assert root.attrib == dict(tests='7', failures='2', errors='2', skipped='1', time='0.023')
suite, = root.findall('testsuite')
assert suite.attrib['tests'] == '7'
cases = {case.attrib['name']: case for case in suite.findall('testcase')}
retry = cases['retry <& \U0001f680 \ufffd \ufffd']
assert retry.find('failure') is None
assert retry.attrib['time'] == '0.004'
properties = {item.attrib['name']: item.attrib['value']
              for item in retry.findall('properties/property')}
assert properties['native.titleXmlSanitizedUnits'] == '2'
assert properties['native.testId'] == '18446744073709551614'
assert properties['native.attempt0.error'] == 'first actual error <&\nline\t\r'
assert properties['native.attempt0.expected'] == 'false'
assert properties['native.attempt1.expected'] == 'true'
assert cases['expected failure'].find('failure') is None
assert cases['unexpected pass'].find('failure').attrib['type'] == 'unexpectedPass'
assert cases['timeout'].find('failure').attrib['type'] == 'timedOut'
assert cases['skip'].find('skipped') is not None
assert cases['interrupted'].find('error').attrib['type'] == 'interrupted'
assert cases['Native runner global error 1'].find('error').text == 'global \ufffd \ufffd <& \ufffd'
assert '00ff3c3e260d0a' in suite.find('system-out').text
assert 'fe00' in suite.find('system-err').text
assert not list(args.directory.glob('*.tmp'))
tiny = ET.parse(args.directory / 'submillisecond.xml').getroot()
for element in tiny.iter():
    if 'time' in element.attrib:
        assert 'e' not in element.attrib['time'].lower()
        assert Decimal(element.attrib['time']) == Decimal('0.000001')

unrun = json.loads((args.directory / 'unrun.json').read_bytes())
assert unrun['summary']['tests'] == 7 and unrun['summary']['attempts'] == 7
assert unrun['summary']['didNotRun'] == 1 and unrun['summary']['skipped'] == 1
assert unrun['unrun'] == [dict(testId='77', title='not started \ud800 <&', reason='beforeAll failed <&')]
assert all(attempt['testId'] != '77' for attempt in unrun['attempts'])
unrun_root = ET.parse(args.directory / 'unrun.xml').getroot()
assert unrun_root.attrib['tests'] == '8' and unrun_root.attrib['skipped'] == '2'
unrun_case = next(case for case in unrun_root.findall('testsuite/testcase') if case.attrib['name'] == 'not started \ufffd <&')
assert 'time' not in unrun_case.attrib and unrun_case.find('skipped').attrib['type'] == 'didNotRun'
unrun_props = {item.attrib['name']: item.attrib['value'] for item in unrun_case.findall('properties/property')}
assert unrun_props['native.noAttempt'] == 'true' and unrun_props['native.didNotRun'] == 'true'
assert not any('worker' in key.lower() or 'pid' in key.lower() or 'duration' in key.lower() for key in unrun_props)
print('Native JSON/JUnit independently decoded; retry, unrun declarations, UTF16/XML, byte streams and atomic-failure checks passed')
