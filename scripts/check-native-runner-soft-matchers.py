# SPDX-License-Identifier: MIT
"""Public asynchronous native soft matchers on genuine browser/runner callbacks.

The script never installs a bridge, supplies matcher observations, or derives a
native outcome from beacons. It checks the real native journal/report separately
from author continuation and original-Error identity assertions.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--sdk-directory', type=Path, required=True)
parser.add_argument('--browser-executable', type=Path, required=True)
parser.add_argument('--node', default='node')
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--prepare-only', action='store_true')
parser.add_argument('--api-response-only', action='store_true')
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='soft-', dir=args.directory.resolve()))
sdk = args.sdk_directory.resolve()
node = shutil.which(args.node)
assert node, 'Actual Node executable unavailable'

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

# Pin actual executable/SDK bytes. A surrounding sealed-epoch driver additionally
# checks its full native source inventory and all CEF runtime assets.
inputs = [Path(node).resolve(), Path(__file__).resolve()]
if not args.prepare_only:
    inputs += [args.executable.resolve(), args.browser_executable.resolve()]
inputs += [path for path in sdk.rglob('*') if path.is_file() and not path.is_symlink()]
before = {str(path): digest(path) for path in sorted(set(inputs))}
(root / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')

PRELUDE = '''import assert from 'node:assert/strict';
import fs from 'node:fs';
import process from 'node:process';
import * as util from 'node:util';
import {defineNativeSuite,createNativeBrowserFixtures,expectNative,NativeAssertionError} from SUITE_API;
const emit=(event,info,extra={})=>process.stdout.write(JSON.stringify({event,pid:process.pid,testId:info.testId,retry:info.retry,...extra})+'\\n');
const fixtureAPI=createNativeBrowserFixtures({browser:{executablePath:BROWSER,headless:true,width:80,height:60,framesPerSecond:30,onDiagnostic:text=>fs.appendFileSync(DIAGNOSTIC,text)}});
const fixtures={...fixtureAPI,actualPage:{scope:'test',dependencies:['page'],setup:async({page},use,info)=>{
  const children=[];
  for(const entry of fs.readdirSync('/proc')){
    if(!/^\\d+$/.test(entry))continue;
    try{
      const raw=fs.readFileSync('/proc/'+entry+'/stat','utf8');
      const stat=raw.slice(raw.lastIndexOf(')')+1).trim().split(/\\s+/);
      if(Number(stat[1])===process.pid)children.push({pid:Number(entry),start:stat[19]});
    }catch(error){if(error?.code!=='ENOENT'&&error?.code!=='ESRCH')throw error}
  }
  assert.equal(children.length,1,'one genuine native browser helper');
  const child=children[0];
  assert.equal(fs.realpathSync('/proc/'+child.pid+'/exe'),fs.realpathSync(BROWSER),'direct child must be the pinned native browser');
  const current=fs.readFileSync('/proc/'+child.pid+'/stat','utf8');
  const identity=current.slice(current.lastIndexOf(')')+1).trim().split(/\\s+/);
  assert.equal(Number(identity[1]),process.pid);assert.equal(identity[19],child.start);
  fs.appendFileSync(HELPERS,JSON.stringify({...child,host:process.pid})+'\\n');
  await page.setContent('<p id=status>actual</p>');
  assert.equal(await page.locator('#status').textContent(),'actual');
  emit('fixture-ready',info,{callsitePrimitive:typeof util.getCallSites==='function'});
  await use(page);
  emit('fixture-after-use',info);
}}};
const mismatch=page=>expectNative.soft(page.locator('#status')).toHaveText('wanted',{timeout:40});
'''
SCOPES = {
'body-errors-and-continuation': '''
export default defineNativeSuite({fixtures,tests:[{title:'body native soft identity',fixtures:['actualPage'],run:async({actualPage:page},info)=>{
  assert.equal(info.errors.length,0);
  await expectNative.soft(page.locator('#status')).toHaveText('actual',{timeout:400});
  assert.equal(info.errors.length,0,'a real passing matcher is not an error row');
  await mismatch(page);
  assert.equal(info.errors.length,1);
  const first=info.errors[0];assert(first instanceof NativeAssertionError);
  assert.match(first.message,/Received: "actual"/);
  const identity={actualError:true};Object.defineProperty(first,'authorObservedIdentity',{value:identity});
  assert.strictEqual(info.errors[0],first);assert.strictEqual(info.errors[0].authorObservedIdentity,identity);
  await expectNative.soft(page.locator('#absent')).toBeVisible({timeout:40});
  assert.equal(info.errors.length,2);assert.strictEqual(info.errors[0],first);assert.notStrictEqual(info.errors[1],first);
  emit('body-continued',info,{errors:info.errors.map(error=>error.stack)});
}}]});''',
'before-each-continues-body': '''
export default defineNativeSuite({fixtures,hooks:{
 beforeEach:[{fixtures:['actualPage'],run:async({actualPage:page},info)=>{await mismatch(page);assert.equal(info.errors.length,1);emit('before-continued',info)}}],
 afterEach:[{run:async(_,info)=>{assert.equal(info.errors.length,1);emit('after-continued',info)}}]
},tests:[{title:'before soft preserves body',fixtures:['actualPage'],run:async({actualPage:page},info)=>{assert.equal(info.errors.length,1);assert.equal(await page.locator('#status').textContent(),'actual');emit('body-continued',info)}}]});''',
'nested-step-real-return': '''
export default defineNativeSuite({fixtures,tests:[{title:'nested soft native result',fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 const actualValue={answer:42};
 const returned=await info.step('outer',()=>info.step('inner',async()=>{await mismatch(page);return actualValue}));
 assert.strictEqual(returned,actualValue,'soft step must return actual callback object');
 const later=await info.step('later passing',()=>{emit('later-step',info);return 73});assert.equal(later,73);
 assert.equal(info.errors.length,1);emit('steps-continued',info);
}}]});''',
'mixed-hard-keeps-primary': '''
export default defineNativeSuite({fixtures,tests:[{title:'mixed actual errors',fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 await mismatch(page);assert.equal(info.errors.length,1);emit('soft-before-hard',info);
 throw new Error('GENUINE_HARD_PRIMARY');
 emit('must-not-execute',info);
}}]});''',
'cleanup-soft-defeats-expected-failure': '''
export default defineNativeSuite({fixtures,hooks:{afterEach:[{fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 await mismatch(page);assert.equal(info.errors.filter(error=>error instanceof NativeAssertionError).length,1);emit('cleanup-continued',info);
}}]},tests:[{title:'expected body with unexpected soft cleanup',expectedFailure:true,fixtures:['actualPage'],run:async(_,info)=>{
 emit('expected-body-error',info);throw new Error('GENUINE_EXPECTED_BODY_PRIMARY');
}}]});''',
'ignored-soft-retains-physical-fence': '''
export default defineNativeSuite({fixtures,hooks:{afterEach:[{run:async(_,info)=>{assert.equal(info.errors.length,1);emit('after-ignored',info)}}]},tests:[{
 title:'unawaited actual matcher receipt',fixtures:['actualPage'],run:({actualPage:page},info)=>{
  const pending=mismatch(page);
  void pending.then(()=>emit('ignored-actual-settlement',info),error=>{emit('ignored-unexpected-rejection',info);throw error});
  emit('body-returned-with-soft-pending',info);
 }
}]});''',
}
if args.api_response_only:
    SCOPES = {'api-response-issued-status': '''
export default defineNativeSuite({fixtures,tests:[{title:'native HTTP response soft receipts',fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 const {createServer}=await import('node:http');
 const requests=[];
 const server=createServer((request,response)=>{
   requests.push(request.url);
   if(request.url==='/ok'){response.writeHead(200,{'Content-Type':'application/octet-stream'});response.end('actual native success bytes')}
   else{response.writeHead(503,{'Content-Type':'text/plain'});response.end('actual native HTTP unavailable body')}
 });
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve)});
 try{
   const address=server.address();assert(address&&typeof address!=='string');
   const origin='http://127.0.0.1:'+address.port;
   const unavailable=await page.request.get(origin+'/unavailable');
   const okay=await page.request.get(origin+'/ok');
   assert.equal(unavailable.status(),503);assert.equal(okay.status(),200);
   assert.equal(await unavailable.text(),'actual native HTTP unavailable body');
   await expectNative.soft(okay,'Successful native status').toBeOK();
   await expectNative.soft(unavailable).not.toBeOK();assert.equal(info.errors.length,0);
   await expectNative.soft(unavailable,'Actual HTTP service context').toBeOK();
   assert.equal(info.errors.length,1);const first=info.errors[0];
   assert(first instanceof NativeAssertionError);
   assert.match(first.message,/^Actual HTTP service context\\n\\nexpect\\(response\\)/);
   assert.match(first.message,/← 503 Service Unavailable/);
   assert.match(first.message,/Response text:\\nactual native HTTP unavailable body/);
   await expectNative.soft(okay,{message:'Actual negated HTTP context'}).not.toBeOK();
   assert.equal(info.errors.length,2);assert.strictEqual(info.errors[0],first);
   assert.match(info.errors[1].message,/^Actual negated HTTP context\\n\\nexpect\\(response\\)\\.not\\./);
   assert.match(info.errors[1].message,/← 200 OK/);
   await unavailable.dispose();
   assert.throws(()=>expectNative.soft(unavailable),/actual native HTTP dispatch result/);
   assert.equal(info.errors.length,2,'retired response is not a new accepted soft mismatch');
   emit('http-soft-continued',info,{errors:info.errors.map(error=>error.stack),statuses:[unavailable.status(),okay.status()],requests});
   await okay.dispose();
 }finally{await new Promise((resolve,reject)=>server.close(error=>error?reject(error):resolve()))}
}}]});'''}

def processes():
    result = {}
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
            result[int(entry.name)] = {'ppid': int(fields[1]), 'group': int(fields[2]), 'start': fields[19], 'exe': os.readlink(entry / 'exe')}
        except (OSError, ValueError, IndexError):
            pass
    return result


def execute(command, directory):
    with (directory / 'stdout.bin').open('wb') as stdout, (directory / 'stderr.bin').open('wb') as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True)
        observed, started, timed_out = {}, time.monotonic(), False
        while process.poll() is None:
            current = processes()
            owned = {pid for pid, state in current.items() if state['group'] == process.pid}
            while True:
                descendants = {pid for pid, state in current.items() if state['ppid'] in owned}
                if descendants <= owned:
                    break
                owned |= descendants
            observed.update({pid: current[pid] for pid in owned})
            if time.monotonic() - started > 45:
                timed_out = True
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
                break
            time.sleep(.02)
    remaining = [{'pid': pid, **state} for pid, state in processes().items() if pid in observed and state['start'] == observed[pid]['start']]
    physical = {'exit': process.returncode, 'outerTimeout': timed_out, 'observedOwned': observed, 'remainingOwned': remaining}
    (directory / 'physical-closure.json').write_text(json.dumps(physical, indent=2) + '\n')
    assert not timed_out and not remaining, physical
    return process.returncode


def validate_report(name, directory):
    report = json.loads((directory / 'report.json').read_text())
    workers = {worker['workerIndex']: worker for worker in report['workers']}
    assert workers and all(worker['exitStatusKnown'] and worker['exitCode'] == 0 and worker['signal'] is None and worker['stderr']['byteLength'] == 0 for worker in workers.values()), workers
    assert len(report['attempts']) == 1
    attempt = report['attempts'][0]
    assert attempt['status'] == 'failed' and not attempt['expected'], attempt
    assert report['summary']['unexpected'] == 1, report['summary']
    rows = report.get('softAssertions', [])
    accepted = [row for row in rows if row['acceptedMismatch']]
    assert len(accepted) == (2 if name in ['body-errors-and-continuation', 'api-response-issued-status'] else 1) and len(rows) == len(accepted), rows
    for row in accepted:
        assert row['receipt'] == 'mismatch' and row['rawOutcome'] == 'recognizedMismatch'
        assert row['testId'] == attempt['testId'] and row['generation'] == attempt['generation']
        assert workers[row['workerIndex']]['pid'] == row['pid'] == attempt['pid']
        assert row['receiptMonotonicMs'] >= row['admittedMonotonicMs'] > 0
        assert row['cleanup'] == (row['callbackKind'] == 'afterEach')
        assert all(isinstance(row[field], str) and row[field].isdecimal() for field in ['id', 'requestId', 'testId', 'generation', 'callbackToken', 'scopeId', 'scopeGeneration'])
        assert row['error']
        if 'location' in row:
            location = row['location']
            assert location['origin'] == 'callsite' and Path(location['file']).resolve() == (directory / 'suite.mjs').resolve()
            assert 0 < location['line'] <= len((directory / 'suite.mjs').read_text().splitlines()) and location['column'] > 0
    current = processes()
    for line in (directory / 'helpers.jsonl').read_text().splitlines():
        helper = json.loads(line)
        assert helper['pid'] not in current or current[helper['pid']]['start'] != helper['start'], helper
        assert helper['host'] in {worker['pid'] for worker in workers.values()}, helper
    events = [json.loads(line) for worker in workers.values() for line in bytes.fromhex(worker['stdout']['data']).splitlines()]
    names = [event['event'] for event in events]
    if name == 'body-errors-and-continuation':
        observed = next(event for event in events if event['event'] == 'body-continued')
        assert len(observed['errors']) == 2
        assert [row['error'] for row in accepted] == observed['errors']
    elif name == 'before-each-continues-body':
        assert names.index('before-continued') < names.index('body-continued') < names.index('after-continued')
        assert accepted[0]['callbackKind'] == 'beforeEach'
    elif name == 'nested-step-real-return':
        steps = {step['title']: step for step in report['steps']}
        for title in ['outer', 'inner']:
            assert steps[title]['status'] == 'failed' and steps[title]['callbackReceiptOK'] and steps[title]['receiptOK']
            assert steps[title]['softErrorId'] == accepted[0]['id']
        assert accepted[0]['stepId'] == steps['inner']['id'] and steps['later passing']['status'] == 'passed'
        assert 'steps-continued' in names and 'later-step' in names
    elif name == 'mixed-hard-keeps-primary':
        assert 'GENUINE_HARD_PRIMARY' in attempt['error'] and 'softErrorId' not in attempt
        assert 'soft-before-hard' in names and 'must-not-execute' not in names
    elif name == 'cleanup-soft-defeats-expected-failure':
        assert accepted[0]['cleanup'] and accepted[0]['callbackKind'] == 'afterEach'
        assert 'GENUINE_EXPECTED_BODY_PRIMARY' in attempt['error'] and 'softErrorId' not in attempt
        assert 'cleanup-continued' in names
    elif name == 'ignored-soft-retains-physical-fence':
        assert names.index('body-returned-with-soft-pending') < names.index('ignored-actual-settlement') < names.index('after-ignored')
        assert 'ignored-unexpected-rejection' not in names
    elif name == 'api-response-issued-status':
        observed = next(event for event in events if event['event'] == 'http-soft-continued')
        assert observed['statuses'] == [503, 200]
        assert observed['requests'] == ['/unavailable', '/ok']
        assert [row['error'] for row in accepted] == observed['errors']
        assert '503 Service Unavailable' in accepted[0]['error'] and 'actual native HTTP unavailable body' in accepted[0]['error']
        assert '200 OK' in accepted[1]['error'] and '.not.toBeOK()' in accepted[1]['error']
    if name not in ['mixed-hard-keeps-primary', 'cleanup-soft-defeats-expected-failure']:
        assert attempt['softErrorId'] == accepted[0]['id']
    assert not (directory / 'native-stderr.txt').exists() or (directory / 'native-stderr.txt').stat().st_size == 0
    return {'scope': name, 'acceptedMismatches': len(accepted), 'report': str(directory / 'report.json')}

results = []
try:
    for name, body in SCOPES.items():
        directory = root / name
        directory.mkdir()
        prelude = PRELUDE.replace('SUITE_API', json.dumps((sdk / 'native.js').as_uri())).replace('BROWSER', json.dumps(str(args.browser_executable.resolve()))).replace('DIAGNOSTIC', json.dumps(str(directory / 'native-stderr.txt'))).replace('HELPERS', json.dumps(str(directory / 'helpers.jsonl')))
        suite = directory / 'suite.mjs'
        suite.write_text(prelude + body)
        checked = subprocess.run([node, '--check', str(suite)], capture_output=True, timeout=10)
        assert checked.returncode == 0 and not checked.stderr, checked.stderr
        if args.prepare_only:
            continue
        command = [str(args.executable.resolve()), '--node', node, '--host-script', str(sdk / 'native-runner-host.js'), '--suite', str(suite), '--report', str(directory / 'report.json'), '--junit', str(directory / 'report.xml'), '--html', str(directory / 'report.html')]
        (directory / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        suite_hash = digest(suite)
        assert execute(command, directory) == 1
        assert digest(suite) == suite_hash, 'Actual fixture changed during execution'
        assert (directory / 'stderr.bin').stat().st_size == 0
        results.append(validate_report(name, directory))
finally:
    changed = [path for path, value in before.items() if digest(Path(path)) != value]
    (root / 'audit.json').write_text(json.dumps({'checked': len(before), 'changed': changed, 'preparedOnly': args.prepare_only}, indent=2) + '\n')
    (root / 'index.json').write_text(json.dumps({'passed': len(results), 'prepared': len(SCOPES), 'groups': results, 'preparedOnly': args.prepare_only}, indent=2) + '\n')
    assert not changed, changed
print('PREPARED' if args.prepare_only else 'PASS', len(SCOPES) if args.prepare_only else len(results), 'native soft matcher scopes', root)
