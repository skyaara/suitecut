# SPDX-License-Identifier: MIT
"""Integrated native original-assertion-deadline proof, distinct from soft6.

No bridge installation, native fault injection or manufactured observation.
Renderer busy work is ordinary authored JavaScript. Diagnostics only observe
physical work; they never retry matchers or extend their original deadline.
Always launch real runners under an isolated native proof supervisor.
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
parser.add_argument('--supervisor', type=Path, required=True)
parser.add_argument('--node', default='node')
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--prepare-only', action='store_true')
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='assertion-deadline-', dir=args.directory.resolve()))
sdk = args.sdk_directory.resolve()
node = shutil.which(args.node)
assert node, 'Actual Node executable unavailable'

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

inputs = [Path(__file__).resolve(), Path(node).resolve()]
if not args.prepare_only:
    inputs += [args.executable.resolve(), args.browser_executable.resolve(), args.supervisor.resolve()]
inputs += [path for path in sdk.rglob('*') if path.is_file() and not path.is_symlink()]
before = {str(path): digest(path) for path in sorted(set(inputs))}
(root / 'inputs-before.json').write_text(json.dumps(before, indent=2)+'\n')

PRELUDE = "import assert from 'node:assert/strict';\nimport fs from 'node:fs';\nimport process from 'node:process';\nimport * as util from 'node:util';\nimport {defineNativeSuite,createNativeBrowserFixtures,expectNative,NativeAssertionError,createNativePage} from SUITE_API;\nconst emit=(event,info,extra={})=>process.stdout.write(JSON.stringify({event,pid:process.pid,testId:info.testId,retry:info.retry,...extra})+'\\n');\nconst fixtureAPI=createNativeBrowserFixtures({browser:{executablePath:BROWSER,headless:true,width:80,height:60,framesPerSecond:30,onDiagnostic:text=>fs.appendFileSync(DIAGNOSTIC,text)}});\nconst fixtures={...fixtureAPI,actualPage:{scope:'test',dependencies:['page'],setup:async({page},use,info)=>{\n  const children=[];\n  for(const entry of fs.readdirSync('/proc')){\n    if(!/^\\d+$/.test(entry))continue;\n    try{\n      const raw=fs.readFileSync('/proc/'+entry+'/stat','utf8');\n      const stat=raw.slice(raw.lastIndexOf(')')+1).trim().split(/\\s+/);\n      if(Number(stat[1])===process.pid)children.push({pid:Number(entry),start:stat[19]});\n    }catch(error){if(error?.code!=='ENOENT'&&error?.code!=='ESRCH')throw error}\n  }\n  assert.equal(children.length,1,'one genuine native browser helper');\n  const child=children[0];\n  assert.equal(fs.realpathSync('/proc/'+child.pid+'/exe'),fs.realpathSync(BROWSER),'direct child must be the pinned native browser');\n  const current=fs.readFileSync('/proc/'+child.pid+'/stat','utf8');\n  const identity=current.slice(current.lastIndexOf(')')+1).trim().split(/\\s+/);\n  assert.equal(Number(identity[1]),process.pid);assert.equal(identity[19],child.start);\n  fs.appendFileSync(HELPERS,JSON.stringify({...child,host:process.pid})+'\\n');\n  await page.setContent('<p id=status>actual</p>');\n  assert.equal(await page.locator('#status').textContent(),'actual');\n  emit('fixture-ready',info,{callsitePrimitive:typeof util.getCallSites==='function'});\n  await use(page);\n  emit('fixture-after-use',info);\n}}};\nconst mismatch=page=>expectNative.soft(page.locator('#status')).toHaveText('wanted',{timeout:40});\n"
SUPPORT = r'''
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const state=async(page)=>{
 const raw=await page.source.runAutomation('diagnostics');
 const names=['otherOperations','nativeSelectorWaits','nativeSelectorWaitPhysical','evaluationGroups','pendingDevTools','contextRetryQueued','pendingFirstObservations'];
 for(const key of names)assert(Number.isSafeInteger(raw[key])&&raw[key]>=0,key);
 return Object.fromEntries(names.map(key=>[key,raw[key]]));
};
// Proof-only sampling: this never invokes/retries an assertion or changes its clock.
const waitState=async(page,predicate)=>{
 const until=performance.now()+2500;let value;
 do{value=await state(page);if(predicate(value))return value;await delay(2)}while(performance.now()<until);
 throw new Error('Actual native physical state not observed: '+JSON.stringify(value));
};
const zero=async page=>waitState(page,value=>Object.values(value).every(count=>count===0));
const beginBusy=async(page,info,{nested=false}={})=>{
 if(nested)await waitState(page,value=>value.nativeSelectorWaits===0&&value.nativeSelectorWaitPhysical===0);
 else await zero(page);
 let began;const started=new Promise(resolve=>{began=resolve});
 const binding=await page.exposeFunction('actualAssertionBusyStarted',()=>began());
 // No await: the real binding event is sent while this same genuine renderer
 // turn is still synchronously busy. No script returns a fake matcher result.
 const done=page.evaluate(()=>{
   globalThis.actualAssertionBusyStarted();
   const end=performance.now()+600;while(performance.now()<end){}
   return document.querySelector('#status')?.textContent;
 });
 await started;emit('renderer-busy-started',info);
 return {done,dispose:()=>binding.dispose()};
};
const deadline=async(page,info,{negated=false}={})=>{
 await expectNative(page.locator('#status')).toHaveText('actual',{timeout:2000});
 const busy=await beginBusy(page,info);let settled=false;
 const match=expectNative.soft(page.locator('#status'),'Native original deadline context');
 const pending=(negated?match.not.toHaveText('actual',{timeout:40}):match.toHaveText('wanted',{timeout:40})).then(()=>{settled=true});
 const held=await waitState(page,value=>value.nativeSelectorWaitPhysical>0);
 assert(held.nativeSelectorWaits>0);assert.equal(settled,false);
 await delay(80);
 const expiredHeld=await state(page);assert(expiredHeld.nativeSelectorWaitPhysical>0);assert.equal(settled,false,'typed receipt must wait for actual renderer/group cleanup');
 await busy.done;await pending;
 const original=info.errors.at(-1);assert(original instanceof NativeAssertionError);
 assert.match(original.message,/^Native original deadline context\n\n/);
 assert.match(original.message,/without an admitted value/);assert.match(original.message,/Timeout: 40ms/);
 assert.doesNotMatch(original.message,/Received|late actual|count:|preview:/);
 if(negated)assert.match(original.message,/\.not\.toHaveText/);
 const identity={nativeDeadlineOriginal:true};Object.defineProperty(original,'actualDeadlineIdentity',{value:identity});
 assert.strictEqual(info.errors.at(-1),original);assert.strictEqual(info.errors.at(-1).actualDeadlineIdentity,identity);
 await busy.dispose();const final=await zero(page);
 emit('actual-deadline-settled',info,{held,expiredHeld,final,error:original.stack,negated});
 return original;
};
'''
SCOPES = {
'body-deadline-step-value': r'''
export default defineNativeSuite({fixtures,tests:[{title:'actual typed deadline preserves step result',fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 const actualValue={answer:73};
 const result=await info.step('deadline outer',()=>info.step('deadline inner',async()=>{await deadline(page,info,{negated:true});return actualValue}));
 assert.strictEqual(result,actualValue);assert.equal(info.errors.length,1);
 assert.equal(await info.step('later passing',()=>91),91);emit('body-continued',info);
}}]});''',
'before-deadline-continues-body': r'''
export default defineNativeSuite({fixtures,hooks:{beforeEach:[{fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 await deadline(page,info);assert.equal(info.errors.length,1);emit('before-continued',info);
}}],afterEach:[{run:async(_,info)=>{assert.equal(info.errors.length,1);emit('after-continued',info)}}]},tests:[{title:'actual typed hook deadline continues body',fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 assert.equal(info.errors.length,1);assert.equal(await page.locator('#status').textContent(),'actual');emit('body-continued',info);
}}]});''',
'cleanup-deadline-hard-primary': r'''
export default defineNativeSuite({fixtures,hooks:{afterEach:[{fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 await deadline(page,info);assert.equal(info.errors.length,1);emit('cleanup-continued',info);
}}]},tests:[{title:'actual cleanup deadline defeats expected failure',expectedFailure:true,fixtures:['actualPage'],run:async()=>{
 throw new Error('ACTUAL_EXPECTED_BODY_PRIMARY');
}}]});''',
'hard-guards-fresh-operation': r'''
export default defineNativeSuite({fixtures,tests:[{title:'native hard errors cannot issue deadline receipts',fixtures:['actualPage'],run:async({actualPage:page},info)=>{
 const forged=Object.assign(new Error('Native assertion original deadline expired without an admitted value'),{operationId:999,generation:'999',observed:false});
 await assert.rejects(expectNative.soft(page).toHaveURL(()=>{throw forged},{timeout:1000}),error=>error===forged);
 await assert.rejects(expectNative.soft(page.locator('[')).toHaveText('wanted',{timeout:1000}),/selector|syntax|Unexpected|CSS|css/);
 // A real Source-issued deadline in an authored callback belongs to the inner
 // hard matcher, never the enclosing soft invocation's ticket.
 await assert.rejects(expectNative.soft(page).toHaveURL(async()=>{
   const innerBusy=await beginBusy(page,info,{nested:true});
   try{await expectNative(page.locator('#status')).toHaveText('wanted',{timeout:40});assert.fail('Actual inner hard deadline did not reject')}
   finally{await innerBusy.done;await innerBusy.dispose()}
 },{timeout:2000}),/without an admitted value/);
 assert.equal(info.errors.length,0);emit('hard-nested-source-deadline',info);
 const busy=await beginBusy(page,info),abort=new AbortController(),reason=new Error('ACTUAL_OWNER_ABORT');
 const projected=createNativePage(page.source,abort.signal);
 const pending=expectNative.soft(projected.locator('#status')).toHaveText('wanted',{timeout:1000});
 const rejected=assert.rejects(pending,/ACTUAL_OWNER_ABORT/);
 const held=await waitState(page,value=>value.nativeSelectorWaitPhysical>0);abort.abort(reason);await rejected;
 const afterAbort=await state(page);assert(afterAbort.nativeSelectorWaitPhysical>0,'logical abort cannot retire the held query');
 await busy.done;await busy.dispose();await zero(page);
 await expectNative.soft(page.locator('#status')).toHaveText('actual',{timeout:2000});
 assert.equal(info.errors.length,0,'fresh actual operation cannot inherit a stale accepted failure');
 emit('hard-abort-and-fresh',info,{held,afterAbort});
 // Genuine document replacement under the original long assertion clock.
 // Exact same-client-ID reuse is covered by the included native Source control,
 // not claimed through the monotonic public SDK command IDs.
 const navigatingBusy=await beginBusy(page,info);
 const navigationBusySettled=navigatingBusy.done.catch(error=>assert.match(String(error),/document|context|navigat|closed|destroy|detach/i));
 const observed=expectNative.soft(page.locator('#status')).toHaveText('wanted',{timeout:3000});
 const navigationRejected=assert.rejects(observed,/document|context|navigat|closed|destroy|detach/i);
 await waitState(page,value=>value.nativeSelectorWaitPhysical>0);
 const navigation=page.goto('data:text/html,<p id=status>new document</p>',{timeout:3000});
 await Promise.all([navigationBusySettled,navigation,navigationRejected]);
 await navigatingBusy.dispose();const final=await zero(page);assert.equal(info.errors.length,0);
 emit('hard-navigation',info,{final});throw new Error('ACTUAL_HARD_CONTROLS_PRIMARY');
}}]});''',
}

def processes():
    result = {}
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
            state = {'state': fields[0], 'ppid': int(fields[1]), 'group': int(fields[2]), 'start': fields[19]}
        except (OSError, ValueError, IndexError):
            continue
        try:
            state['exe'] = os.readlink(entry / 'exe')
        except OSError:
            state['exe'] = None  # A zombie must not disappear from the inventory.
        result[int(entry.name)] = state
    return result


def execute(command, directory):
    launch = [str(args.supervisor.resolve()), '--report', str(directory / 'supervisor.json'),
              '--timeout-ms', '45000', '--cleanup-grace-ms', '1000', '--physical-ms', '6000', '--', *command]
    (directory / 'launch-command.json').write_text(json.dumps(launch, indent=2)+'\n')
    observed, timed_out = {}, False
    with (directory / 'stdout.bin').open('wb') as stdout, (directory / 'stderr.bin').open('wb') as stderr:
        process = subprocess.Popen(launch, stdout=stdout, stderr=stderr, start_new_session=True)
        began = time.monotonic()
        while process.poll() is None:
            current = processes()
            owned = {pid for pid, row in current.items() if row['group'] == process.pid}
            while True:
                descendants = {pid for pid, row in current.items() if row['ppid'] in owned}
                if descendants <= owned:
                    break
                owned |= descendants
            observed.update({pid: current[pid] for pid in owned})
            if time.monotonic()-began > 60:
                timed_out = True
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
                break
            time.sleep(.01)
    current = processes()
    remaining = [{'pid': pid, **row} for pid, row in current.items()
                 if pid in observed and row['start'] == observed[pid]['start']]
    helper_rows = [json.loads(line) for line in (directory / 'helpers.jsonl').read_text().splitlines()]
    assert helper_rows, 'No actual helper identity was captured'
    helpers_remaining = [row for row in helper_rows if row['pid'] in current and current[row['pid']]['start'] == row['start']]
    supervisor = json.loads((directory / 'supervisor.json').read_text())
    closure = {'supervisorExit': process.returncode, 'outerTimeout': timed_out,
               'observedOwned': observed, 'remainingOwned': remaining,
               'helpers': helper_rows, 'remainingHelpers': helpers_remaining,
               'nativeSupervisor': supervisor}
    (directory / 'physical-closure.json').write_text(json.dumps(closure, indent=2)+'\n')
    assert not timed_out and not remaining and not helpers_remaining, closure
    assert not supervisor['timeout'] and not supervisor['interruption'] and not supervisor['cleanupFailed'], supervisor
    assert supervisor['descendantsRetired'] and not supervisor['error'], supervisor
    assert not supervisor['childExit']['signaled'] and supervisor['childExit']['code'] == 1, supervisor
    assert process.returncode == 1 and (directory / 'stderr.bin').stat().st_size == 0, closure


def validate(name, directory):
    report = json.loads((directory / 'report.json').read_text())
    workers = {row['workerIndex']: row for row in report['workers']}
    assert workers and all(row['exitStatusKnown'] and row['exitCode'] == 0 and row['signal'] is None and row['stderr']['byteLength'] == 0 for row in workers.values()), workers
    assert len(report['attempts']) == 1
    attempt = report['attempts'][0]
    assert attempt['status'] == 'failed' and not attempt['expected'] and report['summary']['unexpected'] == 1, attempt
    rows = report.get('softAssertions', [])
    accepted = [row for row in rows if row.get('acceptedDeadline')]
    assert len(accepted) == (0 if name == 'hard-guards-fresh-operation' else 1), rows
    assert not any(row['acceptedMismatch'] for row in rows), rows
    for row in rows:
        assert row['testId'] == attempt['testId'] and row['generation'] == attempt['generation'], row
        assert workers[row['workerIndex']]['pid'] == row['pid'] == attempt['pid'], row
        assert row['receiptMonotonicMs'] >= row['admittedMonotonicMs'] > 0, row
        assert row['cleanup'] == (row['callbackKind'] == 'afterEach'), row
        if 'location' in row:
            assert row['location']['origin'] == 'callsite' and Path(row['location']['file']).name == 'suite.mjs' and row['location']['line'] > 0 and row['location']['column'] > 0, row
        assert all(isinstance(row[key], str) and row[key].isdecimal() for key in ['id','requestId','testId','generation','callbackToken','scopeId','scopeGeneration']), row
    events = [json.loads(line) for worker in report['workers'] for line in bytes.fromhex(worker['stdout']['data']).decode('utf-8').splitlines() if line.startswith('{')]
    names = [event.get('event') for event in events]
    if accepted:
        assert len(rows) == 1
        row = accepted[0]
        assert row['rawOutcome'] == 'recognizedDeadline' and row['receipt'] == 'acceptedDeadline', row
        assert not row['acceptedMismatch'] and row['acceptedDeadline'], row
        assert 'without an admitted value' in row['error'] and 'Received' not in row['error'], row
        observation = next(event for event in events if event.get('event') == 'actual-deadline-settled')
        assert observation['error'] == row['error'], (observation,row)
        assert observation['held']['nativeSelectorWaitPhysical'] > 0 and observation['expiredHeld']['nativeSelectorWaitPhysical'] > 0
        assert all(value == 0 for value in observation['final'].values()), observation
        if name != 'cleanup-deadline-hard-primary':
            assert attempt['softErrorId'] == row['id'], attempt
    if name == 'body-deadline-step-value':
        steps = {row['title']: row for row in report['steps']}
        for title in ['deadline outer','deadline inner']:
            assert steps[title]['status'] == 'failed' and steps[title]['callbackReceiptOK'] and steps[title]['receiptOK'], steps[title]
            assert steps[title]['softErrorId'] == accepted[0]['id'], steps[title]
        assert accepted[0]['stepId'] == steps['deadline inner']['id'] and steps['later passing']['status'] == 'passed'
        assert 'body-continued' in names
    elif name == 'before-deadline-continues-body':
        assert accepted[0]['callbackKind'] == 'beforeEach'
        assert names.index('before-continued') < names.index('body-continued') < names.index('after-continued')
    elif name == 'cleanup-deadline-hard-primary':
        assert accepted[0]['cleanup'] and accepted[0]['callbackKind'] == 'afterEach'
        assert 'ACTUAL_EXPECTED_BODY_PRIMARY' in attempt['error'] and 'softErrorId' not in attempt
        assert 'cleanup-continued' in names
    elif name == 'hard-guards-fresh-operation':
        assert len(rows) == 5 and all(row['rawOutcome'] == 'otherFailure' and row['receipt'] == 'otherFailure' and not row.get('acceptedDeadline', False) for row in rows), rows
        assert any('without an admitted value' in row['error'] and '.toHaveText(' in row['error'] for row in rows), rows
        assert 'ACTUAL_HARD_CONTROLS_PRIMARY' in attempt['error'] and 'softErrorId' not in attempt
        assert 'hard-abort-and-fresh' in names and 'hard-navigation' in names and 'hard-nested-source-deadline' in names
    assert not (directory / 'native-stderr.txt').exists() or (directory / 'native-stderr.txt').stat().st_size == 0
    return {'scope': name, 'acceptedDeadlines': len(accepted), 'acceptedMismatches': 0,
            'report': str(directory / 'report.json')}


results = []
try:
    for name, body in SCOPES.items():
        directory = root / name
        directory.mkdir()
        prelude = PRELUDE.replace('SUITE_API', json.dumps((sdk/'native.js').as_uri())).replace('BROWSER', json.dumps(str(args.browser_executable.resolve()))).replace('DIAGNOSTIC', json.dumps(str(directory/'native-stderr.txt'))).replace('HELPERS', json.dumps(str(directory/'helpers.jsonl')))
        suite = directory / 'suite.mjs'
        suite.write_text(prelude+SUPPORT+body)
        if args.prepare_only:
            continue
        checked = subprocess.run([node,'--check',str(suite)],capture_output=True,timeout=10)
        assert checked.returncode == 0 and not checked.stderr, checked.stderr
        command = [str(args.executable.resolve()),'--node',node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(directory/'report.json'),'--junit',str(directory/'report.xml'),'--html',str(directory/'report.html')]
        (directory / 'command.json').write_text(json.dumps(command,indent=2)+'\n')
        original = digest(suite)
        execute(command,directory)
        assert digest(suite) == original, 'Authored suite bytes changed during execution'
        results.append(validate(name,directory))
finally:
    changed = [path for path,value in before.items() if digest(Path(path)) != value]
    (root/'audit.json').write_text(json.dumps({'checked':len(before),'changed':changed,'preparedOnly':args.prepare_only},indent=2)+'\n')
    (root/'index.json').write_text(json.dumps({'passed':len(results),'prepared':len(SCOPES),'groups':results,'preparedOnly':args.prepare_only,'scope':'New typed Source assertion deadline capability; original soft6 remains unchanged'},indent=2)+'\n')
    assert not changed, changed
print('PREPARED' if args.prepare_only else 'PASS',len(SCOPES) if args.prepare_only else len(results),'native assertion deadline scopes',root)
