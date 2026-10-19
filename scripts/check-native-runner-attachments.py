# SPDX-License-Identifier: MIT
"""Actual native artifact jobs and independent JSON/JUnit/HTML byte/ref checks."""
import tempfile
import argparse,hashlib,json,pathlib,subprocess,xml.etree.ElementTree as ET
from html.parser import HTMLParser
from urllib.parse import unquote
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);invocation=pathlib.Path(tempfile.mkdtemp(prefix='cases-',dir=out));groups=[]
if(out/'report.json').exists():(out/'report.json').replace(invocation/'previous-report.json')
class Links(HTMLParser):
 def __init__(self):super().__init__();self.paths=[]
 def handle_starttag(self,tag,attrs):
  if tag=='a':
   for key,value in attrs:
    if key=='href':self.paths.append(unquote(value))
def run(name,source,code=0):
 root=invocation/name;root.mkdir(exist_ok=False);suite=root/'suite.mjs';suite.write_text("import { defineNativeSuite } from "+json.dumps((a.sdk_directory/'native.js').resolve().as_uri())+";\nimport { strict as assert } from 'node:assert';\nimport { writeFile, unlink } from 'node:fs/promises';\nimport { setTimeout as wait } from 'node:timers/promises';\nconst root="+json.dumps(str(root))+";\n"+source)
 report=root/'actual.json';actual=subprocess.run([str(a.executable),'--node',a.node,'--host-script',str((a.sdk_directory/'native-runner-host.js').resolve()),'--suite',str(suite),'--report',str(report),'--junit',str(root/'actual.xml'),'--html',str(root/'actual.html')],capture_output=True,timeout=70);(root/'stdout.bin').write_bytes(actual.stdout);(root/'stderr.bin').write_bytes(actual.stderr);assert actual.returncode==code and not actual.stderr,(name,actual.returncode,actual.stdout,actual.stderr)
 value=json.loads(report.read_text());xml=ET.parse(root/'actual.xml');properties={p.attrib['name']:p.attrib['value'] for p in xml.findall('.//property')};html=(root/'actual.html').read_text();links=Links();links.feed(html);assert len(links.paths)==len(value['attachments'])
 ids=set();owners={(x['testId'],x['generation'],x['workerIndex'],x['pid']) for x in value['attempts']};total=0
 for artifact in value['attachments']:
  assert artifact['id'] not in ids;ids.add(artifact['id']);assert (artifact['testId'],artifact['generation'],artifact['workerIndex'],artifact['pid']) in owners
  assert artifact['writerExitConfirmed'] and artifact['writerExitCode']==0 and artifact['writerPid']>0 and artifact['writerPid']!=artifact['pid'];assert artifact['startedMonotonicMs']<=artifact['commitReceiptMonotonicMs']<=artifact['writerExitReceiptMonotonicMs'];assert artifact['durationMs']==artifact['writerExitReceiptMonotonicMs']-artifact['startedMonotonicMs']
  path=pathlib.Path(artifact['path']);data=path.read_bytes();info=path.stat();assert len(data)==artifact['byteLength'] and info.st_ino==int(artifact['inode']) and info.st_dev==int(artifact['device']);total+=len(data)
  assert properties['native.attachment'+artifact['id']+'.path']==str(path) and str(path) in links.paths
  assert properties['native.attachment'+artifact['id']+'.writerExitConfirmed']=='true';artifact['_sha256']=hashlib.sha256(data).hexdigest();artifact['_bytes']=data
 assert total<=64*1024*1024
 value['_properties']=properties;value['_html']=html;groups.append({'name':name,'report':str(report),'actualExitCode':actual.returncode,'artifactCount':len(value['attachments'])});return value
v=run('body-binary-utf16-empty-and-explicit-type',"export default defineNativeSuite({tests:[{title:'actual bytes',run:async(_,i)=>{const b=Buffer.from([0,1,128,255]);const p=i.attach('binary\\ud800\\0<&',{body:b,contentType:'application/octet-stream'});b.fill(9);await p;await i.attach('empty string',{body:''});await i.attach('empty bytes',{body:Buffer.alloc(0)});await i.attach('no options');await i.attach('',{});await i.attach('explicit empty type',{body:'actual',contentType:''})}}]});")
assert v['summary']['expected']==1 and len(v['attachments'])==6 and v['attachments'][0]['name']=='binary\ud800\0<&' and v['attachments'][0]['_bytes']==bytes([0,1,128,255]) and [x['contentType'] for x in v['attachments']]==['application/octet-stream','text/plain','application/octet-stream','text/plain','text/plain',''];assert v['_properties']['native.attachment1.nameXmlSanitizedUnits']=='2' and 'original-json' in v['_html']
v=run('actual-path-copy-and-source-unlink',"export default defineNativeSuite({tests:[{title:'real source',run:async(_,i)=>{const path=root+'/source space Ω.txt';await writeFile(path,'actual source Ω');await i.attach('../../../test',{path});await unlink(path)}}]});")
assert len(v['attachments'])==1 and v['attachments'][0]['_bytes']=='actual source Ω'.encode() and v['attachments'][0]['contentType']=='text/plain' and pathlib.Path(v['attachments'][0]['path']).name.startswith('-test-')
v=run('actual-flat-and-nested-hook-scope-owners',"const hook={run:(_,i)=>i.attach(i.hook,{body:i.hook})};export default defineNativeSuite({entries:[{title:'nested actual suite',hooks:{beforeAll:[hook],beforeEach:[hook],afterEach:[hook],afterAll:[hook]},entries:[{title:'actual body',run:(_,i)=>i.attach('body',{body:'body'})}]}]});")
assert v['summary']['expected']==1 and len(v['attachments'])==5 and {x['callbackKind'] for x in v['attachments']}=={'body','beforeAll','beforeEach','afterEach','afterAll'} and all(x['callbackToken']!='0' for x in v['attachments'])
v=run('ordinary-file-errors-caught-and-empty-source-preserved',"export default defineNativeSuite({tests:[{title:'actual errors',run:async(_,i)=>{await assert.rejects(i.attach('empty path',{path:''}),/errno=2/);await assert.rejects(i.attach('missing',{path:root+'/absent'}),/errno=2/);await writeFile(root+'/file','file');await assert.rejects(i.attach('ENOTDIR',{path:root+'/file/child'}),/errno=20/);await assert.rejects(i.attach('directory',{path:root}),/regular file/);assert.throws(()=>i.attach('both',{body:'x',path:'x'}),/Exactly one/);assert.throws(()=>i.attach(false),/name.*should be string/);assert.throws(()=>i.attach('too large',{body:Buffer.alloc(16*1024*1024+1)}),/capacity/);await i.attach('after actual errors',{body:'still native'})}}]});")
assert v['summary']['expected']==1 and len(v['attachments'])==1
v=run('actual-retries-preserve-artifacts-and-worker-identity',"export default defineNativeSuite({tests:[{title:'retry artifacts',retries:1,run:async(_,i)=>{await i.attach('attempt '+i.retry,{body:String(i.retry)});if(!i.retry)throw Error('actual first attempt failure')}}]});")
assert v['summary']['flaky']==1 and [x['status'] for x in v['attempts']]==['failed','passed'] and len(v['attachments'])==2 and v['attachments'][0]['generation']!=v['attachments'][1]['generation'] and v['attachments'][0]['pid']!=v['attachments'][1]['pid'] and [x['_bytes'] for x in v['attachments']]==[b'0',b'1']
v=run('stale-author-function-cannot-create-next-attempt-artifact',"let stale;export default defineNativeSuite({tests:[{title:'first',run:(_,i)=>{stale=i.attach}},{title:'next',run:async(_,i)=>{assert.throws(()=>stale('stale',{body:'wrong owner'}),/no longer active/);await i.attach('actual owner',{body:'next'})}}]});")
assert v['summary']['expected']==2 and len(v['attachments'])==1 and v['attachments'][0]['testId']=='2'
v=run('actual-four-parallel-file-workers',"export default defineNativeSuite({tests:[{title:'parallel actual jobs',run:async(_,i)=>{await Promise.all([0,1,2,3].map(n=>i.attach('actual '+n,{body:Buffer.alloc(200000,n)})))}}]});")
assert v['summary']['expected']==1 and len(v['attachments'])==4 and {x['_bytes'][0] for x in v['attachments']}=={0,1,2,3}
v=run('unawaited-success-waits-native-artifact-fence',"export default defineNativeSuite({tests:[{title:'native fence',run:async(_,i)=>{await writeFile(root+'/source',Buffer.alloc(1024*1024,7));void i.attach('unawaited source',{path:root+'/source'})}}]});")
assert v['summary']['expected']==1 and len(v['attachments'])==1 and v['attachments'][0]['_bytes']==bytes([7])*1024*1024
v=run('completed-artifact-preserved-on-real-test-skip',"export default defineNativeSuite({tests:[{title:'actual skip',run:async(_,i)=>{await i.attach('before skip',{body:'retained'});i.skip('actual runtime cause')}}]});")
assert v['summary']['skipped']==1 and len(v['attachments'])==1 and v['attachments'][0]['_bytes']==b'retained'
v=run('ordinary-uncaught-file-error-is-genuine-expected-failure',"export default defineNativeSuite({tests:[{title:'actual missing file',expectedFailure:true,run:(_,i)=>i.attach('missing',{path:''})}]});")
assert v['summary']['expected']==1 and v['attempts'][0]['status']=='failed' and v['attachments']==[]
v=run('body-4MiB-actual-chunk-writes',"export default defineNativeSuite({tests:[{title:'actual chunks',run:(_,i)=>i.attach('chunked',{body:Buffer.alloc(4*1024*1024,171)})}]});")
assert len(v['attachments'])==1 and v['attachments'][0]['_bytes']==bytes([171])*4*1024*1024
v=run('native-64MiB-real-output-quota',"export default defineNativeSuite({tests:[{title:'actual native disk quota',run:async(_,i)=>{await writeFile(root+'/source',Buffer.alloc(16*1024*1024,90));for(let n=0;n<4;++n)await i.attach('actual '+n,{path:root+'/source'});await assert.rejects(i.attach('over quota',{body:'x'}),/capacity/);await unlink(root+'/source')}}]});")
assert len(v['attachments'])==4 and sum(x['byteLength'] for x in v['attachments'])==64*1024*1024 and all(x['_sha256']==hashlib.sha256(bytes([90])*16*1024*1024).hexdigest() for x in v['attachments'])
v=run('native-128-artifact-count-quota',"export default defineNativeSuite({tests:[{title:'actual native count quota',run:async(_,i)=>{for(let n=0;n<128;++n)await i.attach('actual '+n);await assert.rejects(i.attach('over count'),/capacity/)}}]});")
assert len(v['attachments'])==128 and all(x['byteLength']==0 for x in v['attachments'])
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Actual native body/all-hook artifact admission, byte/path file jobs, original owner identity, native IO and JSON/JUnit/HTML references; no original reporter/fixture/outputPath claim','groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual native attachment process groups and independent reports')
