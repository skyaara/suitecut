# SPDX-License-Identifier: MIT
import argparse,json,pathlib,subprocess,time,os
parser=argparse.ArgumentParser()
parser.add_argument("--executable",type=pathlib.Path,required=True)
parser.add_argument("--sdk-directory",type=pathlib.Path,required=True)
parser.add_argument("--browser-executable",type=pathlib.Path,required=True)
parser.add_argument("--directory",type=pathlib.Path,required=True)
parser.add_argument("--node",required=True)
args=parser.parse_args()
root=args.directory.resolve();root.mkdir(parents=True,exist_ok=True);sdk=args.sdk_directory.resolve()
results=[]
prelude="import assert from 'node:assert/strict';import fs from 'node:fs';import process from 'node:process';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";import {createNativeBrowserFixtures} from "+json.dumps((sdk/'native-runner-fixtures.js').as_uri())+";\n"+'''
function childPid(){for(const name of fs.readdirSync('/proc')){if(!/^\\d+$/.test(name))continue;let value;try{value=fs.readFileSync('/proc/'+name+'/stat','utf8')}catch{continue}const fields=value.slice(value.lastIndexOf(')')+1).trim().split(/\\s+/);if(Number(fields[1])===process.pid)return Number(name)}throw new Error('Actual CEF child unavailable')}
'''
def run(name,body,code,timeout=30):
 d=root/'cef-evidence'/name;d.mkdir(parents=True,exist_ok=True);diagnostic=d/'native-stderr.txt';marker=d/'helper.json';suite=d/'suite.mjs';report=d/'report.json'
 declaration="const marker="+json.dumps(str(marker))+";const fixtures=createNativeBrowserFixtures({browser:{executablePath:"+json.dumps(str(args.browser_executable.resolve()))+",headless:true,width:320,height:240,framesPerSecond:1,onDiagnostic:text=>fs.appendFileSync("+json.dumps(str(diagnostic))+",text)}});\n"
 suite.write_text(prelude+declaration+body)
 start=time.monotonic();p=subprocess.run([str(args.executable.resolve()),'--node',args.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report)],capture_output=True,timeout=timeout)
 (d/'stdout.bin').write_bytes(p.stdout);(d/'stderr.bin').write_bytes(p.stderr);assert p.returncode==code,(name,p.returncode,p.stdout,p.stderr)
 v=json.loads(report.read_text());assert not p.stderr,p.stderr
 diag=diagnostic.read_text() if diagnostic.exists() else '';assert not diag,(name,diag)
 if marker.exists():
  records=json.loads(marker.read_text());records=records if isinstance(records,list) else [records]
  for record in records:
   try:os.kill(record['helper'],0)
   except ProcessLookupError:pass
   else:raise AssertionError((name,'actual helper survived native runner report',record))
 results.append({'name':name,'exitCode':p.returncode,'elapsedMs':(time.monotonic()-start)*1000,'report':str(report),'diagnosticBytes':len(diag)})
 return v,d
v,d=run('actual-browser-context-page-fixtures',"""
let previousSource,firstPage;
export default defineNativeSuite({fixtures,tests:[
 {title:'real page',fixtures:['page','nativeSource'],timeout:5000,run:async({page,nativeSource})=>{previousSource=nativeSource;firstPage=page;fs.writeFileSync(marker,JSON.stringify({helper:childPid(),host:process.pid}));await page.setContent('<input id=field><button id=go>go</button>');await page.locator('#field').fill('actual CEF');assert.equal(await page.locator('#field').inputValue(),'actual CEF');await page.evaluate(()=>window.proof=42)}},
 {title:'isolated next real page',fixtures:['page','nativeSource'],timeout:5000,run:async({page,nativeSource})=>{assert.equal(nativeSource,previousSource);assert.notEqual(page,firstPage);assert.equal(await page.evaluate(()=>window.proof),undefined)}}
]});""",0)
assert v['summary']['expected']==2 and len(v['workers'])==1 and not v['globalErrors'],v
v,d=run('actual-browser-new-worker-retry',"""
export default defineNativeSuite({fixtures,tests:[{title:'actual CEF retry fence',fixtures:['page'],timeout:5000,retries:1,run:async({page},info)=>{
 await page.setContent('<p>genuine CEF</p>');assert.equal(await page.locator('p').textContent(),'genuine CEF');
 if(!info.retry){fs.writeFileSync(marker,JSON.stringify({helper:childPid(),host:process.pid}));throw new Error('actual body failure')}
 const old=JSON.parse(fs.readFileSync(marker,'utf8'));assert.notEqual(old.host,process.pid);assert.throws(()=>process.kill(old.helper,0),{code:'ESRCH'});
 fs.writeFileSync(marker,JSON.stringify([{...old},{helper:childPid(),host:process.pid}]))
}}]});""",0)
assert v['summary']['flaky']==1 and len(v['workers'])==2 and not v['globalErrors'],v
v,d=run('actual-browser-helper-eof-after-killed-host',"""
export default defineNativeSuite({fixtures,tests:[{title:'never settling raw browser callback',fixtures:['page'],timeout:1000,run:async({page})=>{
 await page.setContent('<p>actual pending callback</p>');fs.writeFileSync(marker,JSON.stringify({helper:childPid(),host:process.pid}));await new Promise(()=>{})
}}]});""",1)
assert v['attempts'][0]['status']=='timedOut' and v['workers'][0]['signal']==9,v
assert not any('descendant' in text for text in v['globalErrors']),v
(root/'cef-evidence'/'report.json').write_text(json.dumps({'passed':len(results),'groups':results},indent=2)+'\n')
print('PASS',len(results),'actual CEF builtin-fixture/physical-helper groups')
# Existing genuine context-manager identity must not erase the browser facade.
v,d=run('actual-browser-close-with-canonical-manager',"import {NativeBrowserContexts} from "+json.dumps((sdk/'native-context.js').as_uri())+";export default defineNativeSuite({fixtures,hooks:{beforeAll:[{fixtures:['nativeSource'],run:async({nativeSource})=>{new NativeBrowserContexts(nativeSource)}}],afterAll:[{fixtures:['nativeSource'],run:async({nativeSource})=>{assert.equal(nativeSource.state,'closed');await nativeSource.closed}}]},tests:[{title:'actual browser.close with existing canonical manager',fixtures:['browser'],timeout:5000,run:async({browser})=>{const helper=childPid();fs.writeFileSync(marker,JSON.stringify({helper,host:process.pid}));assert(browser.contexts().length);const page=await browser.newPage();await page.setContent('<p>real facade page</p>');assert.equal(await page.locator('p').textContent(),'real facade page');await browser.close();assert.throws(()=>process.kill(helper,0),{code:'ESRCH'});await browser.close();}}]});",0)
assert v['summary']['expected']==1 and not v['globalErrors'],v
(root/'cef-evidence'/'report.json').write_text(json.dumps({'passed':len(results),'groups':results},indent=2)+'\n')
print('PASS',len(results),'actual CEF fixture groups including canonical manager and physical browser.close')
