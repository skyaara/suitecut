# SPDX-License-Identifier: MIT
"""Real native runner request fixtures and real origin socket closure."""
import argparse,json,pathlib,subprocess,threading,time
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
parser=argparse.ArgumentParser();parser.add_argument('--executable',type=pathlib.Path,required=True);parser.add_argument('--browser-executable',type=pathlib.Path,required=True);parser.add_argument('--sdk-directory',type=pathlib.Path,required=True);parser.add_argument('--node',required=True);parser.add_argument('--directory',type=pathlib.Path,required=True);args=parser.parse_args()
root=args.directory.resolve();root.mkdir(parents=True,exist_ok=True);sdk=args.sdk_directory.resolve();wire=[];arrived=threading.Event();closed=threading.Event()
class Handler(BaseHTTPRequestHandler):
 protocol_version='HTTP/1.1'
 def log_message(self,*args):pass
 def do_GET(self):
  wire.append({'path':self.path,'headers':dict(self.headers)})
  if self.path=='/held':
   arrived.set();self.connection.settimeout(8)
   try:
    if self.rfile.read(1)==b'':closed.set()
   except OSError:pass
   self.close_connection=True;return
  if self.path=='/accepted':
   assert arrived.wait(4),'actual native request never reached origin';body=b'accepted'
  elif self.path=='/closed':
   assert closed.wait(4),'actual native request socket remained open';body=b'closed'
  elif self.path=='/cookie':body=self.headers.get('Cookie','').encode()
  else:body=self.headers.get('X-Runner','').encode()
  self.send_response(200)
  if self.path=='/set':self.send_header('Set-Cookie','actual=one; Path=/')
  self.send_header('Content-Type','text/plain');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
server=ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();prefix=f'http://127.0.0.1:{server.server_port}'
prelude="import assert from 'node:assert/strict';import process from 'node:process';import fs from 'node:fs';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";import {createNativeBrowserFixtures} from "+json.dumps((sdk/'native-runner-fixtures.js').as_uri())+";\n"
groups=[]
def execute(name,source,code=0):
 folder=root/name;folder.mkdir(exist_ok=True);report=folder/'report.json';suite=folder/'suite.mjs';diag=folder/'native-stderr.txt'
 declaration="const prefix="+json.dumps(prefix)+";const fixtures=createNativeBrowserFixtures({browser:{executablePath:"+json.dumps(str(args.browser_executable.resolve()))+",headless:true,width:320,height:240,framesPerSecond:1,onDiagnostic:text=>fs.appendFileSync("+json.dumps(str(diag))+",text)},request:{baseURL:prefix,extraHTTPHeaders:{'X-Runner':'actual native fixture'}}});\n"
 suite.write_text(prelude+declaration+source);start=time.monotonic();actual=subprocess.run([str(args.executable.resolve()),'--node',args.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report)],capture_output=True,timeout=25)
 (folder/'stdout.bin').write_bytes(actual.stdout);(folder/'stderr.bin').write_bytes(actual.stderr);assert actual.returncode==code,(name,actual.returncode,actual.stdout,actual.stderr)
 assert not actual.stderr,actual.stderr;diagnostics=diag.read_text() if diag.exists() else '';assert not diagnostics,diagnostics
 value=json.loads(report.read_text());groups.append({'name':name,'exitCode':actual.returncode,'elapsedMs':(time.monotonic()-start)*1000,'report':str(report),'diagnosticBytes':len(diagnostics)});return value
try:
 value=execute('real-options-and-cookie-isolation',"""
 let firstRequest,firstWorker;
 export default defineNativeSuite({fixtures,hooks:{afterAll:[{fixtures:['nativeSource'],run:async({nativeSource})=>{
 const state=await nativeSource.runAutomation('diagnostics');for(const key of ['httpContexts','httpTransfers','httpActive','httpCookieVisits','httpCookieCallbacks','httpProfilePhysical'])assert.equal(state[key],0,key);
 }}]},tests:[
 {title:'real configured request and distinct page cookie jar',fixtures:['request','context'],timeout:5000,run:async({request,context})=>{
 firstRequest=request;firstWorker=process.pid;const response=await request.get('/set');assert.equal(await response.text(),'actual native fixture');assert((await request.storageState()).cookies.some(cookie=>cookie.name==='actual'));assert(!(await context.cookies()).some(cookie=>cookie.name==='actual'));
 }},
 {title:'new request jar after exact prior disposal',fixtures:['request'],timeout:5000,run:async({request})=>{
 assert.equal(process.pid,firstWorker);assert.notEqual(request,firstRequest);assert.equal(firstRequest.isDisposed,true);assert.equal(await(await request.get('/cookie')).text(),'');
 }}]});""")
 assert value['summary']['expected']==2 and not value['globalErrors'],value
 value=execute('actual-held-socket-disposal-before-next-test',"""
 let prior,worker,pending;
 export default defineNativeSuite({fixtures,hooks:{afterAll:[{fixtures:['nativeSource'],run:async({nativeSource})=>{
 const state=await nativeSource.runAutomation('diagnostics');for(const key of ['httpContexts','httpTransfers','httpActive','httpCookieVisits','httpCookieCallbacks','httpProfilePhysical'])assert.equal(state[key],0,key);
 }}]},tests:[
 {title:'accepted genuine held request',fixtures:['request'],timeout:5000,run:async({request})=>{
 prior=request;worker=process.pid;pending=request.get('/held',{timeout:0}).catch(error=>error);assert.equal(await(await request.get('/accepted')).text(),'accepted');
 }},
 {title:'actual origin EOF before next fixture finishes',fixtures:['request'],timeout:5000,run:async({request})=>{
 assert.equal(process.pid,worker);assert.equal(prior.isDisposed,true);const error=await pending;assert(error instanceof Error);assert.match(error.message,/disposed|closed|abort/i);assert.equal(await(await request.get('/closed')).text(),'closed');
 }}]});""")
 assert value['summary']['expected']==2 and not value['globalErrors'] and closed.is_set(),value
 value=execute('fresh-real-request-per-all-hook-and-test-scope',"""
 let beforeRequest,testRequest;
 export default defineNativeSuite({fixtures,hooks:{
 beforeAll:[{fixtures:['request'],run:async({request})=>{beforeRequest=request;assert.equal(await(await request.get('/set')).text(),'actual native fixture');assert((await request.storageState()).cookies.some(cookie=>cookie.name==='actual'));}}],
 afterAll:[{fixtures:['request'],run:async({request})=>{assert.notEqual(request,beforeRequest);assert.notEqual(request,testRequest);assert.equal(beforeRequest.isDisposed,true);assert.equal(testRequest.isDisposed,true);assert.equal(await(await request.get('/cookie')).text(),'');}},
 {fixtures:['nativeSource'],run:async({nativeSource})=>{const state=await nativeSource.runAutomation('diagnostics');for(const key of ['httpContexts','httpTransfers','httpActive','httpCookieVisits','httpCookieCallbacks','httpProfilePhysical'])assert.equal(state[key],0,key);}}]
 },tests:[{title:'fresh request during actual test scope',fixtures:['request'],timeout:5000,run:async({request})=>{testRequest=request;assert.notEqual(request,beforeRequest);assert.equal(beforeRequest.isDisposed,true);assert.equal(await(await request.get('/cookie')).text(),'');await request.get('/set');}}]});""")
 assert value['summary']['expected']==1 and not value['globalErrors'],value
 (root/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups,'wire':wire,'originSocketEOF':closed.is_set()},indent=2)+'\n');print('PASS',len(groups),'actual native runner request fixture groups')
finally:
 server.shutdown();server.server_close();thread.join(timeout=5)
