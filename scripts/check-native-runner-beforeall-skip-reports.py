# SPDX-License-Identifier: MIT
"""Independent JSON/XML decoding of the native beforeAll skip report contract."""
import argparse,json,pathlib,subprocess,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',required=True);p.add_argument('--directory',type=pathlib.Path,required=True);a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True);subprocess.run([a.executable,str(a.directory)],check=True)
r=json.loads((a.directory/'valid.json').read_text());assert r['summary']['tests']==2 and r['summary']['attempts']==1 and r['runtimeSkipped']==[{'testId':'12','causeId':'77','title':'not admitted','annotations':[]}]
assert r['skipCauses'][0]['reason']=='actual\ud800\x00<&' and r['skipCauses'][0]['associatedGeneration']==r['attempts'][0]['generation']
x=ET.parse(a.directory/'valid.xml');c=[c for c in x.findall('.//testcase') if c.attrib['name']=='not admitted'][0];assert 'time' not in c.attrib
assert c.find("./properties/property[@name='native.noAttempt']").attrib['value']=='true' and c.find('skipped').attrib['type']=='runtimeBeforeAll'
assert not any(p.attrib['name'].endswith('.pid') or p.attrib['name'].endswith('.generation') or p.attrib['name'].endswith('.durationMs') for p in c.findall('.//property'))
assert x.find(".//property[@name='native.beforeAllSkipCause77.reasonXmlSanitizedUnits']").attrib['value']=='2'
assert '1e-' not in (a.directory/'valid.xml').read_text();h=(a.directory/'valid.html').read_text();assert 'runtime-skip-cause' in h and 'data-cause-id="77"' in h and 'data-test-id="12"' in h
failed=json.loads((a.directory/'accepted-cause-cleanup-failed.json').read_text());assert len(failed['skipCauses'])==1 and not failed['runtimeSkipped'] and failed['attempts'][0]['status']=='failed'
print('PASS independent JSON/XML/HTML runtime beforeAll cause/no-attempt projection')
