# SPDX-License-Identifier: MIT
"""Independent decoding of native local-step-skip report scalars and receipts."""
import argparse,pathlib,subprocess,json,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',type=pathlib.Path,required=True);p.add_argument('--directory',type=pathlib.Path,required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True)
actual=subprocess.run([str(a.executable),str(out)],capture_output=True,timeout=20);(out/'stdout.bin').write_bytes(actual.stdout);(out/'stderr.bin').write_bytes(actual.stderr);assert actual.returncode==0 and not actual.stderr,(actual.returncode,actual.stdout,actual.stderr)
v=json.loads((out/'local-skip.json').read_text());assert v['summary']['expected']==1 and v['summary']['skipped']==0 and len(v['steps'])==2 and len(v['attempts'])==1
assert v['attempts'][0]['status']=='passed' and 'skipReason' not in v['attempts'][0]
for s in v['steps']:
 assert s['status']=='skipped' and s['receipt']=='settled' and s['receiptOK'] and s['stepSkip']=={'requestId':'7','sourceStepId':'1','reason':'reason\ud800<&','descriptionPresent':True} and s['receiptStepSkipRequestId']=='7' and 'testSkip' not in s
xml=ET.parse(out/'local-skip.xml');assert len(xml.findall('.//testcase'))==1 and not xml.findall('.//skipped')
props={p.attrib['name']:p.attrib['value'] for p in xml.findall('.//property')}
for s in v['steps']:
 prefix='native.step'+s['id']+'.';assert props[prefix+'stepSkipRequestId']=='7' and props[prefix+'stepSkipSourceStepId']=='1' and props[prefix+'stepSkipDescriptionPresent']=='true' and props[prefix+'stepSkipReasonXmlSanitizedUnits']=='1' and props[prefix+'stepSkipReason']=='reason\ufffd<&' and props[prefix+'receiptStepSkipRequestId']=='7'
html=(out/'local-skip.html').read_text();assert html.count('data-step-id=')==2 and 'reason' in html and '&lt;&amp;' in html and 'Actual local-step skip reason' in html
(out/'report.json').write_text(json.dumps({'passed':14,'scope':'Native validation14 and independent JSON/XML/HTML decode; actual passed test with two local-skipped steps'},indent=2)+'\n');print('PASS native local-step skip validation14 and independent three-format report decoding')
