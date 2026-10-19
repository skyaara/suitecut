#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent JSON/XML/HTML decoding of controlled native soft report data."""
import argparse
import json
import subprocess
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--executable', type=Path)
args = parser.parse_args()
if args.executable:
    args.directory.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(args.executable.resolve()), str(args.directory.resolve())], check=True)
report = json.loads((args.directory / 'soft.json').read_text())
row = report['softAssertions'][0]
assert row['acceptedMismatch'] and row['rawOutcome'] == 'recognizedMismatch' and row['receipt'] == 'mismatch'
assert row['id'] == report['attempts'][0]['softErrorId'] == report['steps'][0]['softErrorId']
assert row['stepId'] == report['steps'][0]['id']
assert row['error'] == report['attempts'][0]['error'] == report['steps'][0]['error']
assert '\ud800' in row['error'] and '\0' in row['error'] and '<&>' in row['error']
assert report['steps'][0]['callbackReceiptOK'] and report['steps'][0]['receiptOK']
assert row['location'] == {'file': '/actual/caller.ts', 'line': 7, 'column': 9, 'origin': 'callsite'}
xml = ET.parse(args.directory / 'soft.xml')
properties = {p.attrib['name']: p.attrib['value'] for p in xml.findall('.//property')}
prefix = 'native.softAssertion' + row['id'] + '.'
assert properties[prefix + 'acceptedMismatch'] == 'true'
assert properties[prefix + 'error'] == row['error'].replace('\ud800', '\ufffd').replace('\0', '\ufffd')
assert properties[prefix + 'errorXmlSanitizedUnits'] == '2'
assert properties['native.attempt0.softErrorId'] == row['id']
assert properties['native.step1.softErrorId'] == row['id']
assert properties[prefix + 'location.origin'] == 'callsite'
html = (args.directory / 'soft.html').read_text()
assert 'native-soft-assertion' in html and 'Original matcher error and stack' in html and 'raw mismatch' in html
hard = json.loads((args.directory / 'hard.json').read_text())
assert hard['attempts'][0]['error'] == 'genuine hard primary' and 'softErrorId' not in hard['attempts'][0]
assert hard['softAssertions'] == report['softAssertions']
print('PASS independent native soft JSON/XML/HTML decoding and raw UTF16/source preservation')
