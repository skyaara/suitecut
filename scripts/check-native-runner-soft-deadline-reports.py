#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Decode controlled native deadline reports independently of the C++ writer."""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
subprocess.run([str(args.executable.resolve()), str(args.directory.resolve())], check=True)
report = json.loads((args.directory / 'deadline.json').read_bytes())
row = report['softAssertions'][0]
assert row['rawOutcome'] == 'recognizedDeadline' and row['receipt'] == 'acceptedDeadline'
assert row['acceptedDeadline'] is True and row['acceptedMismatch'] is False
assert row['id'] == report['attempts'][0]['softErrorId'] == report['steps'][0]['softErrorId']
assert row['error'] == report['attempts'][0]['error'] == report['steps'][0]['error']
assert report['steps'][0]['callbackReceiptOK'] and report['steps'][0]['receiptOK']
assert row['location'] == {'file':'/actual/deadline.ts', 'line':7, 'column':9, 'origin':'callsite'}
assert '\ud800' in row['error'] and '\0' in row['error'] and '<&>' in row['error']
properties = {p.attrib['name']:p.attrib['value'] for p in ET.parse(args.directory / 'deadline.xml').findall('.//property')}
prefix = 'native.softAssertion' + row['id'] + '.'
assert properties[prefix + 'acceptedDeadline'] == 'true' and properties[prefix + 'acceptedMismatch'] == 'false'
assert properties[prefix + 'rawOutcome'] == 'recognizedDeadline' and properties[prefix + 'receipt'] == 'acceptedDeadline'
assert properties[prefix + 'error'] == row['error'].replace('\ud800','\ufffd').replace('\0','\ufffd')
assert properties[prefix + 'errorXmlSanitizedUnits'] == '2'
assert properties['native.attempt0.softErrorId'] == row['id']
assert properties['native.step1.softErrorId'] == row['id']
html = (args.directory / 'deadline.html').read_text()
assert 'acceptedDeadline' in html and 'recognizedDeadline' in html and 'raw assertion deadline' in html
hard = json.loads((args.directory / 'hard.json').read_bytes())
assert hard['attempts'][0]['error'] == 'actual raw hard primary' and 'softErrorId' not in hard['attempts'][0]
assert hard['softAssertions'] == report['softAssertions']
print('PASS independent distinct native assertion-deadline JSON/JUnit/HTML decoding; raw UTF16/stack/source retained')
