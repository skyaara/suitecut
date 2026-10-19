#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual native-owned layered fixture callbacks; no browser or external scheduler."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--sdk-directory', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--node', default='node')
parser.add_argument('--prepare-only', action='store_true')
parser.add_argument('--case', action='append')
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='fixture-layers-', dir=args.directory.resolve()))
sdk = args.sdk_directory.resolve()
node = shutil.which(args.node)
assert node
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = [args.executable.resolve(), Path(node).resolve(), Path(__file__).resolve()]
inputs += [p for p in sdk.rglob('*') if p.is_file()]
before = {str(p): digest(p) for p in inputs}
(root / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
prelude = """import assert from 'node:assert/strict';
import process from 'node:process';
import {defineNativeSuite,extendNativeFixtures} from NATIVE;
const emit=value=>process.stdout.write(value+'\\n');
const value=v=>({setup:async(_,use)=>{await use(v)}});
""".replace('NATIVE', json.dumps((sdk / 'native.js').as_uri()))
cases = {
    'previous-chain': """
let fixtures={dep:value('override'),foo:value('base'),bar:{dependencies:['foo'],setup:async({foo},use)=>use(foo+'-bar')}};
fixtures=extendNativeFixtures(fixtures,{foo:{dependencies:['foo','dep'],setup:async({foo,dep},use)=>use(foo+'-'+dep+'1')}});
fixtures=extendNativeFixtures(fixtures,{foo:{dependencies:['foo','dep'],setup:async({foo,dep},use)=>use(foo+'-'+dep+'2')}});
export default defineNativeSuite({fixtures,tests:[{title:'original548 binding semantics',fixtures:['bar'],run:({bar})=>{assert.equal(bar,'base-override1-override2-bar');emit('CHAIN_ACTUAL')}}]});
""",
    'final-sibling': """
const fixtures=extendNativeFixtures({foo:value('foo'),bar:value('bar'),baz:{dependencies:['foo','bar'],setup:async({foo,bar},use)=>use(foo+'-baz-'+bar)}},{foo:{dependencies:['foo','bar'],setup:async({foo,bar},use)=>use(foo+'-'+bar)},bar:{dependencies:['bar'],setup:async({bar},use)=>use(bar+'-override')}});
export default defineNativeSuite({fixtures,tests:[{title:'original603 binding semantics',fixtures:['baz'],run:({baz})=>{assert.equal(baz,'foo-bar-override-baz-bar-override');emit('SIBLING_ACTUAL')}}]});
""",
    'automatic-worker': """
const fixtures=extendNativeFixtures({state:{scope:'worker',automatic:true,setup:async(_,use,info)=>{assert.equal(info.scope,'worker');emit('BASE_SETUP');await use({calls:0});emit('BASE_TEARDOWN')}},second:{scope:'worker',automatic:true,setup:async(_,use)=>{emit('SECOND_AUTO_SETUP');await use('second');emit('SECOND_AUTO_TEARDOWN')}}},{state:{dependencies:['state'],setup:async({state},use,info)=>{assert.equal(info.scope,'worker');emit('OVERRIDE_SETUP');await use(state);emit('OVERRIDE_TEARDOWN')}}});
export default defineNativeSuite({fixtures,tests:[1,2].map(n=>({title:'worker '+n,fixtures:['state'],run:({state})=>{assert.equal(++state.calls,n);emit('BODY_'+n)}}))});
""",
    'shadowed': """
const fixtures=extendNativeFixtures({old:{automatic:true,dependencies:['missing'],setup:()=>{throw new Error('SHADOWED_CALLBACK_RAN')}}},{old:{automatic:false,setup:()=>{throw new Error('DISABLED_AUTOMATIC_RAN')}}});
export default defineNativeSuite({fixtures,tests:[{title:'unused callback is not executed',run:()=>emit('NO_SHADOWED_CALLBACK')} ]});
""",
    'hook-scope': """
const fixtures=extendNativeFixtures({item:{setup:async(_,use,info)=>{assert.equal(info.scope,'test');emit('BASE_TEST_SETUP');await use('base');emit('BASE_TEST_TEARDOWN')}}},{item:{dependencies:['item'],setup:async({item},use)=>{emit('OVERRIDE_TEST_SETUP');await use(item+'-override');emit('OVERRIDE_TEST_TEARDOWN')}}});
export default defineNativeSuite({fixtures,hooks:{beforeEach:[{fixtures:['item'],run:({item})=>{assert.equal(item,'base-override');emit('BEFORE')}}],afterEach:[{fixtures:['item'],run:({item})=>{assert.equal(item,'base-override');emit('AFTER')}}]},tests:[{title:'hook body aliases',fixtures:['item'],run:({item})=>{assert.equal(item,'base-override');emit('BODY')}}]});
""",
    'all-hook-scope': """
const fixtures=extendNativeFixtures({item:value('base')},{item:{dependencies:['item'],setup:async({item},use,info)=>{assert.equal(info.scope,'hook');emit('ALL_'+info.hook);await use(item+'-override')}}});
export default defineNativeSuite({fixtures,hooks:{beforeAll:[{fixtures:['item'],run:({item})=>assert.equal(item,'base-override')}],afterAll:[{fixtures:['item'],run:({item})=>assert.equal(item,'base-override')}]},tests:[{title:'all hook actual scopes',run:()=>emit('ALL_BODY')}]});
""",
    'retry': """
const fixtures=extendNativeFixtures({item:value('base')},{item:{dependencies:['item'],setup:async({item},use)=>{emit('RETRY_SETUP');await use(item+'-override');emit('RETRY_TEARDOWN')}}});
export default defineNativeSuite({fixtures,tests:[{title:'fresh actual retry host',retries:1,fixtures:['item'],run:({item},info)=>{assert.equal(item,'base-override');emit('RETRY_BODY_'+info.retry);assert.equal(info.retry,1)}}]});
""",
    'cycle-rejected': """
const fixtures=extendNativeFixtures({foo:value('base'),bar:{dependencies:['foo'],setup:()=>emit('INVALID_CALLBACK')}},{foo:{dependencies:['bar'],setup:()=>emit('INVALID_CALLBACK')}});
export default defineNativeSuite({fixtures,tests:[{title:'native cycle rejection',fixtures:['foo'],run:()=>emit('INVALID_BODY')}]});
""",
    'scope-rejected': """
const fixtures=extendNativeFixtures({foo:{scope:'worker',setup:()=>emit('INVALID_CALLBACK')}},{foo:{scope:'test',setup:()=>emit('INVALID_CALLBACK')}});
export default defineNativeSuite({fixtures,tests:[{title:'native scope rejection',fixtures:['foo'],run:()=>emit('INVALID_BODY')}]});
""",
    'flat-compatible': """
export default defineNativeSuite({fixtures:{plain:value('flat')},tests:[{title:'existing flat fixture protocol',fixtures:['plain'],run:({plain})=>{assert.equal(plain,'flat');emit('FLAT_ACTUAL')}}]});
""",
    'retained-version': """
import net from 'node:net';
const barrier=()=>new Promise((resolve,reject)=>{const socket=net.connect(PORT,'127.0.0.1');socket.once('error',reject);socket.once('connect',()=>socket.write(String(process.pid)+'\\n'));socket.once('data',data=>{assert.equal(data.toString(),'G');socket.end();resolve()})});
const fixtures=extendNativeFixtures({state:{scope:'worker',setup:async(_,use)=>{emit('RETAINED_BASE_SETUP');await use({calls:0});emit('RETAINED_BASE_TEARDOWN')}}},{state:{dependencies:['state'],setup:async({state},use,info)=>{assert.equal(info.scope,'worker');emit('RETAINED_OVERRIDE_SETUP');await use(state);emit('RETAINED_OVERRIDE_TEARDOWN')}}});
export default defineNativeSuite({fullyParallel:true,fixtures,tests:[1,2,3,4].map(n=>({title:'retained version '+n,fixtures:['state'],run:async({state})=>{const calls=++state.calls;if(calls===1)await barrier();assert.ok(calls===1||calls===2);emit('RETAINED_BODY_'+calls)}}))});
""",
}
if args.case:
    assert len(set(args.case)) == len(args.case) and all(name in cases for name in args.case), 'Unknown or duplicate fixture scope'
    cases = {name: cases[name] for name in args.case}
results = []
try:
    for name, source in cases.items():
        folder = root / name
        folder.mkdir()
        suite = folder / 'suite.mjs'
        listener = None
        thread = None
        barrier_errors = []
        barrier_pids = []
        if name == 'retained-version':
            listener = socket.socket()
            listener.bind(('127.0.0.1', 0))
            listener.listen(2)
            listener.settimeout(15)
            source = source.replace('PORT', str(listener.getsockname()[1]))
        suite.write_text(prelude + source)
        if args.prepare_only:
            syntax = subprocess.run([node, '--check', str(suite)], capture_output=True, timeout=10)
            assert syntax.returncode == 0 and syntax.stderr == b'', (name, syntax.stderr)
            if listener: listener.close()
            results.append(dict(name=name, prepared=True))
            continue
        command = [str(args.executable.resolve()), '--node', node, '--host-script', str(sdk / 'native-runner-host.js'), '--suite', str(suite), '--report', str(folder / 'report.json')]
        if listener:
            def barrier():
                connections = []
                try:
                    for _ in range(2):
                        connection, _ = listener.accept()
                        connection.settimeout(15)
                        connections.append(connection)
                        with connection.makefile('rb') as stream:
                            barrier_pids.append(int(stream.readline(64)))
                    assert len(set(barrier_pids)) == 2 and all(Path('/proc', str(pid)).exists() for pid in barrier_pids)
                    for connection in connections: connection.sendall(b'G')
                except BaseException as error: barrier_errors.append(repr(error))
                finally:
                    for connection in connections: connection.close()
                    listener.close()
            thread = threading.Thread(target=barrier)
            thread.start()
            command += ['--workers', '2', '--global-timeout', '15000']
        (folder / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        with (folder / 'stdout.bin').open('wb') as stdout, (folder / 'stderr.bin').open('wb') as stderr:
            actual = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=20)
        if thread:
            thread.join(timeout=1)
            assert not thread.is_alive() and not barrier_errors, barrier_errors
        report = json.loads((folder / 'report.json').read_bytes())
        (folder / 'exit.json').write_text(json.dumps(dict(returncode=actual.returncode)) + '\n')
        assert (folder / 'stderr.bin').read_bytes() == b'', (name, (folder / 'stderr.bin').read_bytes())
        workers = report['workers']
        assert workers and all(w['exitStatusKnown'] and w['stderr']['byteLength'] == 0 for w in workers), (name, workers)
        # Native catalog rejection force-closes the still-live metadata host.
        # Preserve that actual SIGKILL receipt rather than inventing exit0.
        if name.endswith('-rejected'):
            assert all(w['exitCode'] is None and w['signal'] == 9 for w in workers), (name, workers)
        else:
            assert all(w['exitCode'] == 0 and w['signal'] is None for w in workers), (name, workers)
        assert all(not Path('/proc', str(w['pid'])).exists() for w in workers), (name, workers)
        out = ''.join(bytes.fromhex(w['stdout']['data']).decode() for w in workers)
        attempts = report['attempts']
        if name.endswith('-rejected'):
            assert actual.returncode == 1 and report['globalErrors'] and not attempts and 'INVALID_' not in out, (name, report)
            assert any(('cycle' if name == 'cycle-rejected' else 'scope') in str(error) for error in report['globalErrors']), (name, report)
        else:
            assert actual.returncode == 0 and not report['globalErrors'], (name, report)
            assert attempts and all(a['expected'] and a['status'] == 'passed' for a in attempts if name != 'retry'), (name, report)
        if name == 'previous-chain': assert 'CHAIN_ACTUAL' in out
        if name == 'final-sibling': assert 'SIBLING_ACTUAL' in out
        if name == 'automatic-worker':
            assert len(workers) == 1 and len(attempts) == 2
            assert all(out.count(marker) == 1 for marker in ['BASE_SETUP', 'OVERRIDE_SETUP', 'SECOND_AUTO_SETUP', 'BODY_1', 'BODY_2', 'SECOND_AUTO_TEARDOWN', 'OVERRIDE_TEARDOWN', 'BASE_TEARDOWN']), out
            assert out.index('BASE_SETUP') < out.index('OVERRIDE_SETUP') < out.index('SECOND_AUTO_SETUP') < out.index('BODY_1') < out.index('BODY_2') < out.index('SECOND_AUTO_TEARDOWN') < out.index('OVERRIDE_TEARDOWN') < out.index('BASE_TEARDOWN'), out
        if name == 'shadowed': assert 'NO_SHADOWED_CALLBACK' in out
        if name == 'hook-scope':
            assert out.index('BASE_TEST_SETUP') < out.index('OVERRIDE_TEST_SETUP') < out.index('BEFORE') < out.index('BODY') < out.index('AFTER') < out.index('OVERRIDE_TEST_TEARDOWN') < out.index('BASE_TEST_TEARDOWN'), out
        if name == 'all-hook-scope': assert out.index('ALL_beforeAll') < out.index('ALL_BODY') < out.index('ALL_afterAll'), out
        if name == 'retry':
            assert [a['status'] for a in attempts] == ['failed', 'passed'] and len({w['pid'] for w in workers}) == 2, report
            assert out.count('RETRY_SETUP') == 2 and out.count('RETRY_TEARDOWN') == 2 and 'RETRY_BODY_0' in out and 'RETRY_BODY_1' in out, out
        if name == 'flat-compatible': assert 'FLAT_ACTUAL' in out
        if name == 'retained-version':
            assert len(workers) == 2 and len(attempts) == 4 and set(barrier_pids) == {w['pid'] for w in workers}, report
            assert out.count('RETAINED_BASE_SETUP') == 2 and out.count('RETAINED_OVERRIDE_SETUP') == 2 and out.count('RETAINED_BASE_TEARDOWN') == 2 and out.count('RETAINED_OVERRIDE_TEARDOWN') == 2, out
            assert out.count('RETAINED_BODY_1') == 2 and out.count('RETAINED_BODY_2') == 2, out
            assert len([b for b in report['workerReuse']['borrows'] if b['receipt'] == 'idleTransferred']) == 2, report
        results.append(dict(name=name, returncode=actual.returncode, attempts=len(attempts), actualWorkerPIDs=[w['pid'] for w in workers]))
finally:
    after = {str(p): digest(p) for p in inputs}
    (root / 'inputs-after.json').write_text(json.dumps(after, indent=2) + '\n')
    (root / 'summary.json').write_text(json.dumps(dict(results=results, inputsUnchanged=before == after, prepareOnly=args.prepare_only), indent=2) + '\n')
    assert before == after, 'Proof inputs changed during actual scope'
print(json.dumps(dict(directory=str(root), scopes=len(results), prepareOnly=args.prepare_only)))
