# SPDX-License-Identifier: MIT
"""Independent decoding of native admitted step location metadata."""
import argparse,json,pathlib,subprocess,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',required=True);p.add_argument('--directory',type=pathlib.Path,required=True);a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True)
v=subprocess.run([a.executable,str(a.directory)],capture_output=True,timeout=20);assert v.returncode==0 and not v.stderr,(v.returncode,v.stdout,v.stderr)
r=json.loads((a.directory/'location.json').read_text());s=r['steps'];assert len(s)==4 and s[0]['location']=={'file':'dummy-file.ts','line':123,'column':45,'origin':'explicit'}
assert s[1]['location']=={'file':'a\ud800\0','line':7,'column':9,'origin':'callsite'} and s[1]['parentId']==s[0]['id'];assert s[2]['receipt']=='notInvoked' and s[2]['location']==s[0]['location'];assert s[3]['status']=='timedOut' and s[3]['location']==s[0]['location']
props={x.attrib['name']:x.attrib.get('value','')for x in ET.parse(a.directory/'location.xml').findall('.//property')};assert props['native.step1.location.file']=='dummy-file.ts' and props['native.step1.location.line']=='123' and props['native.step1.location.column']=='45' and props['native.step1.location.origin']=='explicit'
assert props['native.step2.location.file']=='a\ufffd\ufffd' and props['native.step2.location.fileXmlSanitizedUnits']=='2' and props['native.step2.location.origin']=='callsite'
h=(a.directory/'location.html').read_text();assert 'dummy-file.ts'in h and 'author-declared'in h and 'runtime callsite'in h and 'Original UTF16 JSON string'in h
print('PASS9 independently decoded native step location JSON/JUnit/HTML groups')
