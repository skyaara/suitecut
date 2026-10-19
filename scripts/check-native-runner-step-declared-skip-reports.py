# SPDX-License-Identifier: MIT
"""Decode actual native notInvoked reports without inventing callback metadata."""
import argparse,pathlib,subprocess,json,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',type=pathlib.Path,required=True);p.add_argument('--directory',type=pathlib.Path,required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);r=subprocess.run([str(a.executable),str(out)],capture_output=True,timeout=20);(out/'stdout.bin').write_bytes(r.stdout);(out/'stderr.bin').write_bytes(r.stderr);assert r.returncode==0 and not r.stderr,(r.returncode,r.stdout,r.stderr)
v=json.loads((out/'declared.json').read_text());assert v['summary']['expected']==1 and v['summary']['skipped']==0 and len(v['steps'])==2 and len(v['attempts'])==1
for row in v['steps']:
 assert row['receipt']=='notInvoked' and row['callbackInvoked'] is False and row['receiptOK'] is False and row['declaredSkipRequestId']==row['id'] and 'physicalDurationMs' not in row and 'stepSkip' not in row and 'testSkip' not in row and row['startedMonotonicMs']==row['endedMonotonicMs']==row['receiptMonotonicMs']
assert v['steps'][0]['title']=='declared\ud800<&'
x=ET.parse(out/'declared.xml');assert not x.findall('.//skipped');props={p.attrib['name']:p.attrib['value'] for p in x.findall('.//property')};assert props['native.step1.receipt']=='notInvoked' and props['native.step1.callbackInvoked']=='false' and props['native.step1.titleXmlSanitizedUnits']=='1';h=(out/'declared.html').read_text();assert 'author callback deliberately not invoked' in h and 'Physical duration (ms)' not in h and 'original-json' in h
(out/'report.json').write_text(json.dumps({'passed':12,'scope':'Native validation12 plus independent JSON/XML/HTML no-invocation decoding'},indent=2)+'\n');print('PASS native declared-step validation12 and independent no-invocation JSON/XML/HTML decoding')
