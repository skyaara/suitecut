# SPDX-License-Identifier: MIT
import argparse,json,pathlib,subprocess,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',required=True);p.add_argument('--directory',type=pathlib.Path,required=True);a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True)
actual=subprocess.run([a.executable,str(a.directory)],capture_output=True,timeout=20);assert actual.returncode==0,(actual.stdout,actual.stderr);assert not actual.stderr
v=json.loads((a.directory/'steps.json').read_text());assert v['summary']['expected']==1 and len(v['steps'])==1
s=v['steps'][0];assert s['title']=='actual\ud800\0<&' and s['error']=='real error\udfff\0<&' and s['receiptError']==s['error']
assert s['status']=='failed' and s['receipt']=='settled' and not s['receiptOK'] and s['durationMs']==1
assert s['generation']==v['attempts'][0]['generation'] and s['pid']==v['workers'][0]['pid']
x=ET.parse(a.directory/'steps.xml');assert x.find(".//property[@name='native.step1.title']").attrib['value']=='actual\ufffd\ufffd<&';assert x.find(".//property[@name='native.step1.titleXmlSanitizedUnits']").attrib['value']=='2'
assert x.find(".//property[@name='native.step1.receiptErrorXmlSanitizedUnits']").attrib['value']=='2'
h=(a.directory/'steps.html').read_text();assert 'data-step-id="1"' in h and 'actual&#' not in h and 'Original UTF16 JSON string' in h and '&lt;&amp;' in h
assert (a.directory/'protected.json').read_text()=='actual prior report'
print('PASS14 independently decoded native step JSON/JUnit/HTML validation groups')
