# SPDX-License-Identifier: MIT
"""Adversarial real Node Host ingress, not native-issued packets or public parity.

Run under the production native proof supervisor. Python owns each real child
and records actual waitpid status; no callback result/settlement is fabricated.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import socket
import tempfile
import time

p = argparse.ArgumentParser()
for field in ('sdk-directory', 'node', 'directory'):
    p.add_argument('--' + field, type=Path, required=True)
a = p.parse_args()
a.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='contexts-', dir=a.directory.resolve()))
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
# Resolve package self aliases and actual dependencies before any host spawn.
preflight = [str(a.node), '--input-type=module', '-e',
    "import {createRequire} from 'node:module';const r=createRequire(process.argv[1]);console.log(JSON.stringify(Object.fromEntries(['zod','suitecut','suitecut/native'].map(x=>[x,r.resolve(x)]))));",
    str(a.sdk_directory.resolve() / 'native-runner-host.js')]
resolved = subprocess.run(preflight, capture_output=True, timeout=10)
(root / 'runtime-resolution.json').write_text(json.dumps({'command':preflight,'exit':resolved.returncode,
    'stdout':resolved.stdout.decode(),'stderr':resolved.stderr.decode()}, indent=2)+'\n')
assert resolved.returncode == 0 and not resolved.stderr, resolved.stderr
resolution = json.loads(resolved.stdout)
assert Path(resolution['suitecut']).parent == a.sdk_directory.resolve()
assert Path(resolution['suitecut/native']).parent == a.sdk_directory.resolve()
entry = Path(resolution['zod']).resolve()
dependency = next(parent for parent in entry.parents if (parent / 'package.json').is_file() and json.loads((parent / 'package.json').read_text()).get('name') == 'zod')
files = [Path(__file__), a.node, *a.sdk_directory.rglob('*'), *dependency.rglob('*')]
pins = {str(f.resolve()):sha(f) for f in files if f.is_file()}

groups = []


def index():
    changed = [f for f, h in pins.items() if sha(f) != h]
    (root / 'index.json').write_text(json.dumps({'scope': 'Adversarial controlled fd3 packets to real emitted Node Host, not native-issued admission or public nativeRunner parity.',
        'inputs': pins, 'changed': changed, 'groups': groups}, indent=2) + '\n')
    assert not changed, changed


def run(name, command, expected):
    folder = root / name
    folder.mkdir()
    beacon = folder / 'FORBIDDEN'
    suite = folder / 'suite.mjs'
    suite.write_text('import {appendFileSync} from "node:fs";const beacon=' + json.dumps(str(beacon)) + ';export default {tests:[{title:"forbidden",expectedFailure:true,run(){appendFileSync(beacon,"BODY")}}],hooks:{beforeEach:[{run(){appendFileSync(beacon,"HOOK")}}]}};\n')
    parent, child = socket.socketpair()
    stdout_r, stdout_w = os.pipe()
    stderr_r, stderr_w = os.pipe()
    null = os.open('/dev/null', os.O_RDONLY)
    actions = [(os.POSIX_SPAWN_CLOSE, parent.fileno()), (os.POSIX_SPAWN_DUP2, child.fileno(), 3),
        (os.POSIX_SPAWN_DUP2, stdout_w, 1), (os.POSIX_SPAWN_DUP2, stderr_w, 2), (os.POSIX_SPAWN_DUP2, null, 0)]
    for fd in (child.fileno(), stdout_r, stdout_w, stderr_r, stderr_w, null):
        if fd not in (0, 1, 2, 3):
            actions.append((os.POSIX_SPAWN_CLOSE, fd))
    argv = [str(a.node), str(a.sdk_directory / 'native-runner-host.js'), str(suite), '1']
    pid = os.posix_spawn(str(a.node), argv, os.environ.copy(), file_actions=actions)
    child.close()
    for fd in (stdout_w, stderr_w, null):
        os.close(fd)
    kernel = Path('/proc', str(pid), 'stat').read_text()
    start_identity = int(kernel[kernel.rfind(')') + 2:].split()[19])
    row = {'name': name, 'command': argv, 'hostPID': pid, 'hostKernelStart': start_identity,
        'controlledInput': ['TEST_TIMEOUT_CONTEXT 1 1000', 'REPEAT_ACTOR_CONTEXT 11 2 0 1 3 2 2', command],
        'qualification': 'These are authored malicious/omitted mapping bytes, not native issuance receipts.', 'fixtureSHA256': sha(suite)}
    groups.append(row)
    streams = {'control': bytearray(), 'stdout': bytearray(), 'stderr': bytearray()}
    eof = set()
    seen = []
    framing = bytearray()
    waited = None
    sent = False
    timed_out = False
    with selectors.DefaultSelector() as selector:
        parent.setblocking(False)
        selector.register(parent, selectors.EVENT_READ, 'control')
        for fd, name_ in ((stdout_r, 'stdout'), (stderr_r, 'stderr')):
            os.set_blocking(fd, False)
            selector.register(fd, selectors.EVENT_READ, name_)
        deadline = time.monotonic() + 15
        try:
            while waited is None or len(eof) < 3:
                if time.monotonic() >= deadline:
                    timed_out = True
                    break
                if waited is None:
                    actual, status = os.waitpid(pid, os.WNOHANG)
                    if actual:
                        assert actual == pid
                        waited = status
                for key, _ in selector.select(min(.1, max(0, deadline - time.monotonic()))):
                    data = os.read(key.fd, 65536)
                    name_ = key.data
                    if not data:
                        eof.add(name_)
                        selector.unregister(key.fileobj)
                        continue
                    streams[name_].extend(data)
                    if len(streams[name_]) > 1048576:
                        raise AssertionError('Controlled host stream exceeds original observer bound')
                    if name_ != 'control':
                        continue
                    framing.extend(data)
                    while b'\n' in framing:
                        line, _, tail = framing.partition(b'\n')
                        framing[:] = tail
                        seen.append(line.decode('utf8'))
                        if line == b'LOADED' and not sent:
                            sent = True
                            parent.sendall(('\n'.join(row['controlledInput']) + '\n').encode())
        finally:
            if waited is None:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                actual, waited = os.waitpid(pid, 0)
                assert actual == pid
            parent.close()
            os.close(stdout_r)
            os.close(stderr_r)
    for name_, data in streams.items():
        (folder / (name_ + '.bin')).write_bytes(data)
    row.update(rawWaitpidStatus=waited, actualExitCode=os.waitstatus_to_exitcode(waited), actualWaitpidPID=pid,
        timedOut=timed_out, streamsReachedEOF=sorted(eof), actualLines=seen,
        forbiddenBeaconExists=beacon.exists(), hostPresentIncludingZombie=Path('/proc', str(pid)).exists())
    index()  # Preserve all original bytes and actual status before assertions.
    assert not timed_out and waited == 256 and os.WIFEXITED(waited) and os.WEXITSTATUS(waited) == 1, row
    assert len(eof) == 3 and not framing and not streams['stdout'] and not streams['stderr'], row
    assert sent and any(line.startswith('TEST 1 ') and line.split(' ')[4] == '1' for line in seen)
    errors = [line for line in seen if line.startswith('HOST_ERROR ')]
    assert len(errors) == 1
    fields = errors[0].split(' ')
    assert len(fields) == 2
    error = json.loads(bytes.fromhex(fields[1]).decode('utf8'))
    assert error.splitlines()[0] == 'Error: ' + expected, error
    assert not any(line.startswith(('BODY_RESULT', 'CALL_RESULT', 'STEP_CALLBACK_SETTLED', 'SOFT_BEGIN')) for line in seen), seen
    assert not beacon.exists() and not Path('/proc', str(pid)).exists(), row
    row['passed'] = True
    index()


try:
    missing = 'Native repeat actor has no required selected test mapping'
    run('missing-body-mapping', 'BODY 1 1 0 passed 0 failed 0', missing)
    run('missing-beforeEach-mapping', 'CALL 1 1 1 beforeEach 1 0 1 1000 passed 0 failed 0', missing)
    foreign = 'native-repeat:12:2:1:1'.encode().hex()
    run('foreign-mapping', 'TEST_REPEAT_CONTEXT 1 ' + foreign, 'Native repeat public identity differs from admitted actor/declaration')
    index()
    print(json.dumps({'passed': True, 'adversarialHostControls': len(groups), 'index': str(root / 'index.json')}))
except BaseException:
    index()
    print('Preserved actual first adversarial host negative:', root / 'index.json', flush=True)
    raise
