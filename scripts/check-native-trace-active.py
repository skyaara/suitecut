#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent stdlib ZIP/CRC/raw reader for the actual live native artifacts."""
import hashlib,json,pathlib,sys,zipfile
root=pathlib.Path(sys.argv[1]);report=json.loads((root/'report.json').read_text());assert report['result']=='pass' and report['nativeShutdown']=='clean' and report['nativeStderrBytes']==0
raw=pathlib.Path(report['raw'])
for name in ('active','second'):
 with zipfile.ZipFile(root/(name+'.zip')) as z:
  assert z.testzip() is None
  assert len(z.infolist())<=4096 and sum(i.file_size for i in z.infolist())<=128*1024*1024
  original=[json.loads(line) for line in z.read('trace.trace').splitlines()]
  actual=[json.loads(line) for line in (raw/(name+'.trace')).read_bytes().splitlines()]
  assert original==actual
  assert original[0]['type']=='context-options'
  if name=='active':
   rows=[json.loads(line) for line in z.read('trace.network').splitlines()]
   disk=[json.loads(line) for line in (raw/'active.network').read_bytes().splitlines()]
   assert len(rows)==len(disk)
   for before,after in zip(rows,disk):
    for field,child in [('response','content'),('request','postData')]:
     old=before['snapshot'].get(field,{}).get(child,{})
     new=after['snapshot'].get(field,{}).get(child,{})
     if '_file' in old:
      link=new['_file'];assert link.split('/',1)[1]==old['_file'];body=(raw/link).read_bytes();assert body==z.read(old['_file']);new['_file']=old['_file']
    assert before==after
   path=report['resourcePath'];body=(raw/path).read_bytes();assert body.decode()==report['css'];assert z.read(path.split('/',1)[1])==body
   assert any(e['type']=='console' and e['text']=='native-idle-\ud800' for e in original)
   assert report['activeTrace'].encode().splitlines()[-1] in (raw/'active.trace').read_bytes().splitlines()
   assert hashlib.sha1(body).hexdigest() in path
  else:assert all('native-idle-body-' not in str(e) for e in original if e['type']=='console')
assert (raw/'api-active.trace').is_file() and (raw/'api-active.network').stat().st_size>0
output={'result':'pass','exactRawAndZIPJournalRows':True,'actualCSSSHA1':True,'sameNativeLiveResourceNamespace':True,'isolatedUTF16ConsoleExact':True,'chunkIsolation':True,'independentAPIContext':True,'stderrBytes':0}
(root/'archive-decode-report.json').write_text(json.dumps(output,indent=2)+'\n');print(json.dumps(output))
