#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real native runner/Node ownership; controlled source-tagged mismatches, no DOM claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--sdk-directory', type=Path, required=True)
parser.add_argument('--node', default='node')
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--case', action='append')
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='soft-protocol-', dir=args.directory.resolve()))
sdk = args.sdk_directory.resolve()
node = shutil.which(args.node)
assert node
inputs = [Path(node).resolve(), args.executable.resolve(), Path(__file__).resolve()] + [p for p in sdk.rglob('*') if p.is_file()]
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
before = {str(p): digest(p) for p in inputs}
(root / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
prelude = '''import assert from 'node:assert/strict';
import process from 'node:process';
import {defineNativeSuite} from NATIVE;
import {runNativeSoftAssertion,nativeSoftOperationOptions} from SOFT;
import {nativeMatcherFailure} from PRODUCER;
const emit=event=>process.stdout.write(event+'\\n');
const mismatch=(error=new Error('CONTROLLED_SOURCE_MISMATCH'))=>runNativeSoftAssertion(async()=>{throw nativeMatcherFailure(error)});
'''.replace('NATIVE', json.dumps((sdk/'native.js').as_uri())).replace('SOFT;', json.dumps((sdk/'native-soft-assertions.js').as_uri())+';').replace('PRODUCER', json.dumps((sdk/'native-matcher-failure.js').as_uri()))
cases = {
'body': """export default defineNativeSuite({tests:[{title:'actual soft protocol',run:async(_,info)=>{const error=new Error('ACTUAL_OBJECT');await mismatch(error);assert.strictEqual(info.errors[0],error);emit('BODY_CONTINUED')}}]});""",
'before-each': """export default defineNativeSuite({hooks:{beforeEach:[{run:async(_,info)=>{await mismatch();assert.equal(info.errors.length,1);emit('BEFORE_CONTINUED')}}],afterEach:[{run:(_,info)=>{assert.equal(info.errors.length,1);emit('AFTER_CONTINUED')}}]},tests:[{title:'actual body after soft before',run:()=>emit('BODY_ADMITTED')}]});""",
'steps': """export default defineNativeSuite({tests:[{title:'native soft step resolution',run:async(_,info)=>{const actual={actual:17};const value=await info.step('outer',()=>info.step('inner',async()=>{await mismatch();return actual}));assert.strictEqual(value,actual);assert.equal(await info.step('later',()=>37),37);emit('STEP_VALUE_IDENTITY')}}]});""",
'hard-primary': """export default defineNativeSuite({tests:[{title:'native distinct raw primary',run:async(_,info)=>{await mismatch(new Error('EXACT_EQUAL_MESSAGE'));throw new Error('EXACT_EQUAL_MESSAGE')}}]});""",
'cleanup': """export default defineNativeSuite({hooks:{afterEach:[{run:async()=>{await mismatch();emit('CLEANUP_CONTINUED')}}]},tests:[{title:'native unexpected cleanup',expectedFailure:true,run:()=>{throw new Error('ACTUAL_BODY_PRIMARY')}}]});""",
'ignored': """export default defineNativeSuite({hooks:{afterEach:[{run:(_,info)=>{assert.equal(info.errors.length,1);emit('AFTER_ACTUAL_SOFT_ACK')}}]},tests:[{title:'actual ignored raw lease',run:()=>{void mismatch().then(()=>emit('IGNORED_SOFT_CONTINUED'));emit('BODY_RAW_RETURN')}}]});""",
'overflow': """export default defineNativeSuite({tests:[{title:'native bounded failure admission',expectedFailure:true,run:async(_,info)=>{const outcomes=await Promise.allSettled(Array.from({length:33},()=>mismatch()));assert.equal(outcomes.filter(r=>r.status==='rejected').length,1);assert.equal(info.errors.length,32);emit('OVERFLOW_HARD_RETAINED')}}]});""",
'actual-host-exit': """export default defineNativeSuite({tests:[{title:'actual host exit no fake raw receipt',run:async()=>{await runNativeSoftAssertion(async()=>{emit('ACTUAL_HOST_EXIT');process.exit(0)})}}]});""",
'original-deadline': """export default defineNativeSuite({tests:[{title:'actual admitted soft original deadline',timeout:1000,run:async(_,info)=>{let enter;const entered=new Promise(resolve=>enter=resolve);const operation=runNativeSoftAssertion(async()=>{const {signal}=nativeSoftOperationOptions(0);const raw=new Promise((_,reject)=>{signal.addEventListener('abort',()=>{emit('ACTUAL_SOFT_ABORT');reject(signal.reason)},{once:true})});emit('ACTUAL_SOFT_READY');enter();await raw});const observed=operation.catch(()=>emit('ACTUAL_RAW_REJECTION'));await entered;await info.setTimeout(25);await observed;}}]});""",
'other-failure': """export default defineNativeSuite({tests:[{title:'raw other remains hard',run:async()=>{try{await runNativeSoftAssertion(async()=>{throw new Error('ACTUAL_OTHER_HARD')})}catch{emit('AUTHOR_CAUGHT_RAW_OTHER')}}}]});""",
}
if args.case:
    assert len(set(args.case))==len(args.case) and all(name in cases for name in args.case), 'Unknown or duplicate controlled scope'
    cases = {name:cases[name] for name in args.case}
results = []
try:
    for name, body in cases.items():
        directory = root/name
        directory.mkdir()
        suite = directory/'suite.mjs'
        suite.write_text(prelude+body+'\n')
        report = directory/'report.json'
        command = [str(args.executable.resolve()), '--node', node, '--host-script', str(sdk/'native-runner-host.js'), '--suite', str(suite), '--report', str(report), '--junit', str(directory/'report.xml'), '--html', str(directory/'report.html')]
        (directory/'command.json').write_text(json.dumps(command, indent=2)+'\n')
        with (directory/'stdout.bin').open('wb') as stdout, (directory/'stderr.bin').open('wb') as stderr:
            actual = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=20)
        assert actual.returncode==1 and not (directory/'stderr.bin').stat().st_size, (name, actual.returncode, (directory/'stderr.bin').read_text())
        data=json.loads(report.read_text());attempt=data['attempts'][0]
        assert len(data['attempts'])==1 and not attempt['expected'], (name, data)
        assert all(w['exitStatusKnown'] and w['exitCode']==0 and w['signal'] is None and w['stderr']['byteLength']==0 for w in data['workers']), (name, data['workers'])
        for worker in data['workers']:
            assert not Path('/proc/'+str(worker['pid'])).exists(), (name,worker)
        stdout=''.join(bytes.fromhex(w['stdout']['data']).decode() for w in data['workers'])
        rows=data.get('softAssertions',[]);accepted=[r for r in rows if r['acceptedMismatch']]
        assert not data['globalErrors'], (name,data['globalErrors'])
        if name in ['actual-host-exit','original-deadline','other-failure']:
            assert not accepted and 'softErrorId' not in attempt
            assert rows and rows[0]['receipt']=={'actual-host-exit':'workerExited','original-deadline':'late','other-failure':'otherFailure'}[name],(name,rows)
        else:
            assert len(accepted)==(32 if name=='overflow' else 1),(name,rows)
        if name=='body':assert 'BODY_CONTINUED' in stdout and attempt['softErrorId']==rows[0]['id']
        if name=='before-each':assert stdout.index('BEFORE_CONTINUED')<stdout.index('BODY_ADMITTED')<stdout.index('AFTER_CONTINUED')
        if name=='steps':
            steps={s['title']:s for s in data['steps']}
            assert all(steps[t]['status']=='failed' and steps[t]['callbackReceiptOK'] and steps[t]['receiptOK'] and steps[t]['softErrorId']==rows[0]['id'] for t in ['outer','inner'])
            assert steps['later']['status']=='passed' and 'STEP_VALUE_IDENTITY' in stdout
        if name=='hard-primary':assert 'softErrorId' not in attempt and 'EXACT_EQUAL_MESSAGE' in attempt['error']
        if name=='cleanup':assert 'softErrorId' not in attempt and 'ACTUAL_BODY_PRIMARY' in attempt['error'] and rows[0]['cleanup']
        if name=='ignored':assert stdout.index('BODY_RAW_RETURN')<stdout.index('IGNORED_SOFT_CONTINUED')<stdout.index('AFTER_ACTUAL_SOFT_ACK')
        if name=='overflow':assert 'capacity exceeded' in attempt['error'] and 'softErrorId' not in attempt
        if name=='original-deadline':assert attempt['status']=='timedOut' and attempt['durationMs']>=25 and 'ACTUAL_SOFT_READY' in stdout and 'ACTUAL_SOFT_ABORT' in stdout
        else:assert attempt['status']=='failed', (name,attempt)
        results.append({'scope':name,'report':str(report),'exit':actual.returncode})
        (root/'index.json').write_text(json.dumps(results,indent=2)+'\n')
finally:
    changed=[p for p,value in before.items() if digest(Path(p))!=value]
    (root/'audit.json').write_text(json.dumps({'checked':len(before),'changed':changed},indent=2)+'\n')
    assert not changed, changed
print('PASS',len(results),'actual native soft protocol/phase/step/OS-retirement controls; controlled mismatch producers, no DOM matcher claim',root)
