#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import hashlib,io,json,pathlib,re,sys,zipfile
from PIL import Image
root=pathlib.Path(sys.argv[1]);report=json.loads((root/'report.json').read_text());assert report['result']=='pass' and report['nativeShutdown']=='clean' and report['nativeStderrBytes']==0
raw=pathlib.Path(report['raw'])
def rows(name):return [json.loads(line) for line in (raw/(name+'.trace')).read_text().splitlines()]
def safe_zip(name):
 z=zipfile.ZipFile(root/name);assert z.testzip() is None
 assert len(z.infolist())<=4096 and sum(i.file_size for i in z.infolist())<=128*1024*1024
 return z
with safe_zip('first.zip') as z:
 original=[json.loads(line) for line in z.read('trace.trace').decode().splitlines()];actual=rows('first');assert original[0]['type']=='context-options' and len(original)==len(actual)
 prefix=None;png_count=aria_count=click_png=0;clicks={e["callId"] for e in original if e["type"]=="before" and e["method"]=="click"}
 for a,b in zip(original,actual):
  if a['type'] in ('screenshot','aria-snapshot','screencast-frame'):
   file=b['file'];p,expected=file.split('/',1);assert expected==a['file'];assert prefix is None or prefix==p;prefix=p
   assert z.read(a['file'])==(raw/file).read_bytes()
   if a['type']=='screenshot':
    png_count+=1;image=Image.open(io.BytesIO(z.read(a['file']))).convert('RGB');assert image.size==(320,240)
    if a["callId"] in clicks:
     click_png+=1;assert image.getpixel((310,230))==((0,0,255) if a['phase']=='before' else (255,0,0))
   if a['type']=='aria-snapshot':aria_count+=1;json.loads(z.read(a['file']))
   b=dict(b);b['file']=a['file']
  assert a==b,(a,b)
 assert prefix and png_count>=3 and aria_count>=3 and click_png==3
 for f in z.namelist():
  if f not in ('trace.trace','trace.network','trace.stacks'):assert z.read(f)==(raw/prefix/f).read_bytes(),f
 assert z.read('trace.stacks')==(raw/'first.stacks').read_bytes()
 source='src/'+report['callerSHA1']+'.mjs';assert hashlib.sha1(z.read(source)).hexdigest()==report['callerSHA1'];assert z.read(source)==pathlib.Path(report['caller']).read_bytes()
 # DOM snapshots, including isolated UTF16 keys/values, remain exact native rows.
 dom=[l for l in z.read('trace.trace').splitlines() if b'"type":"frame-snapshot"' in l]
 assert dom and any(b'\\ud800' in line and b'\\udfff' in line for line in dom)
 for line in dom:assert line in (raw/'first.trace').read_bytes().splitlines()
 network=[json.loads(l) for l in z.read('trace.network').splitlines()];raw_network=[json.loads(l) for l in (raw/'first.network').read_bytes().splitlines()];assert len(network)==len(raw_network)
 css_found=False
 for old,new in zip(network,raw_network):
  content=old['snapshot']['response']['content'];actual_content=new['snapshot']['response']['content']
  if '_file' in content:
   assert actual_content['_file']==prefix+'/'+content['_file']
   body=(raw/actual_content['_file']).read_bytes();assert body==z.read(content['_file'])
   assert hashlib.sha1(body).hexdigest()==content['_sha1'].split('.')[0]
   if 'text/css' in content['mimeType']:assert body.decode()==report['css'];css_found=True
   actual_content['_file']=content['_file']
  assert old==new
 assert css_found
second=rows('second');links=[e['file'] for e in second if e['type'] in ('screenshot','aria-snapshot')];assert links and all(not p.startswith(prefix+'/') for p in links);assert all((raw/p).is_file() for p in links);assert not (root/'second.zip').exists()
for name in ('pending-first','pending-second'):
 events=rows(name);seen=set()
 for e in events:
  if e['type']=='before':seen.add(e['callId'])
  elif e['type']=='after':assert e['callId'] in seen,('orphan after',name,e)
 with safe_zip(name+'.zip') as z:assert [json.loads(l) for l in z.read('trace.trace').splitlines()]==events
first_ids={e['callId'] for e in rows('pending-first') if e['type']=='before'};second_ids={e['callId'] for e in rows('pending-second') if e['type'] in ('before','after')};assert first_ids and first_ids.isdisjoint(second_ids)
api=rows('actual-api');assert any(e['type']=='before' and e['class']=='APIRequestContext' and e['method']=='fetch' for e in api)
assert (raw/'actual-api.network').stat().st_size>0
assert not any(p.name.startswith('.suitecut-trace-') for p in raw.iterdir())
output={'result':'pass','rawNamedChunks':True,'rawOnlyStop':True,'exactNativePNG':png_count,'exactNativeARIA':aria_count,'exactCSSBodyHash':True,'allZIPResourcesUnchanged':True,'UTF16DOMLinesVerbatim':len(dom),'independentResourceNamespaces':True,'noOrphanAfterAcrossChunks':True,'genuineAPIContext':True,'nativeStderrBytes':0,'exactCallerSource':True,'nativeENOTDIRRecovery':bool(report.get('defaultName'))}
(root/'archive-decode-report.json').write_text(json.dumps(output,indent=2));print(json.dumps(output))
