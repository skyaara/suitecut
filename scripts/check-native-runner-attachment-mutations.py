# SPDX-License-Identifier: MIT
"""Actual native mutable-array/file/callback proof; original author identities and immutable insertion snapshots are distinct."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from native_file_markers import read_native_file_marker

p = argparse.ArgumentParser()
p.add_argument('--executable', required=True, type=pathlib.Path)
p.add_argument('--sdk-directory', required=True, type=pathlib.Path)
p.add_argument('--library', required=True, type=pathlib.Path)
p.add_argument('--directory', required=True, type=pathlib.Path)
p.add_argument('--node', required=True)
a = p.parse_args()
out = a.directory.resolve()
out.mkdir(parents=True, exist_ok=True)
invocation = pathlib.Path(tempfile.mkdtemp(prefix='cases-', dir=out))
groups = []

def state(pid):
    try:
        return pathlib.Path('/proc/' + str(pid) + '/stat').read_text().rsplit(')', 1)[1].split()[0]
    except FileNotFoundError:
        return None

def until(fn, process, seconds=10):
    end = time.monotonic() + seconds
    while not fn():
        assert process.poll() is None, ('Native runner exited before actual readiness', process.returncode)
        assert time.monotonic() < end, 'Actual native readiness did not arrive'
        time.sleep(.002)

def run(name, source, code=0, hold=None, efbig=False):
    root = invocation / name
    root.mkdir()
    suite = root / 'suite.mjs'
    suite.write_text('import{defineNativeSuite}from ' + json.dumps((a.sdk_directory / 'native.js').resolve().as_uri()) + ';\n'
        "import{strict as assert}from'node:assert';import{writeFileSync,readFileSync,statSync}from'node:fs';import{access}from'node:fs/promises';import{setTimeout as pause}from'node:timers/promises';\n"
        'const root=' + json.dumps(str(root)) + ';\n'
        "async function ready(path){for(;;){try{await access(path);return}catch{await pause(1)}}}\n" + source)
    env = dict(os.environ)
    if hold:
        env.update(LD_PRELOAD=str(a.library), SUITECUT_ATTACHMENT_TEST_HOLD=hold,
            SUITECUT_ATTACHMENT_TEST_MARKER=str(root / 'syscall.marker'),
            SUITECUT_ATTACHMENT_TEST_RELEASE=str(root / 'release'))
    if efbig:
        env['SUITECUT_ATTACHMENT_TEST_EFBIG'] = '1'
    process = subprocess.Popen([str(a.executable), '--node', a.node, '--host-script',
        str((a.sdk_directory / 'native-runner-host.js').resolve()), '--suite', str(suite),
        '--report', str(root / 'actual.json'), '--junit', str(root / 'actual.xml'),
        '--html', str(root / 'actual.html')], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    syscall = None
    if hold:
        until(lambda: read_native_file_marker(root / 'syscall.marker'), process)
        syscall = read_native_file_marker(root / 'syscall.marker')
        assert syscall is not None and len(syscall) == 5
        assert state(int(syscall[0])) not in [None, 'Z']
        until(lambda: (root / 'author-returned.marker').exists(), process)
        assert not (root / 'after.marker').exists() and not (root / 'actual.json').exists()
        (root / 'release').write_text('Resume the actual native fsync syscall\n')
    stdout, stderr = process.communicate(timeout=30)
    (root / 'stdout.bin').write_bytes(stdout)
    (root / 'stderr.bin').write_bytes(stderr)
    assert process.returncode == code and not stderr, (name, process.returncode, stdout, stderr)
    report = json.loads((root / 'actual.json').read_text())
    assert not report['globalErrors'], (name, report)
    views = report.get('attachmentViews', [])
    assert len(views) == 1
    view = views[0]
    attempt = report['attempts'][0]
    assert (view['testId'], view['generation'], view['workerIndex'], view['pid']) == (attempt['testId'], attempt['generation'], attempt['workerIndex'], attempt['pid'])
    assert int(view['revision']) == len(view['changes'])
    assert [int(change['revision']) for change in view['changes']] == list(range(1, len(view['changes']) + 1))
    assert view['slots'] == view['changes'][-1]['slots']
    decls = {row['id']: row for row in report.get('attachmentDeclarations', [])}
    artifacts = {row['id']: row for row in report['attachments']}
    names = []
    for slot in view['slots']:
        names.append(None if slot is None else (decls if slot['kind'] == 'declaration' else artifacts)[slot['id']]['name'])
    for artifact in artifacts.values():
        file = pathlib.Path(artifact['path'])
        actual = file.stat()
        data = file.read_bytes()
        assert actual.st_ino == int(artifact['inode']) and actual.st_dev == int(artifact['device']) and len(data) == artifact['byteLength']
        assert artifact['writerExitConfirmed'] and artifact['writerExitCode'] == 0
        assert state(artifact['writerPid']) is None
        artifact['_data'] = data
        artifact['_sha256'] = hashlib.sha256(data).hexdigest()
    if syscall:
        assert state(int(syscall[0])) is None
        assert (root / 'after.marker').exists()
    xml = ET.parse(root / 'actual.xml')
    props = {row.attrib['name']: row.attrib['value'] for row in xml.findall('.//property')}
    assert json.loads(props['native.attachmentViewsJSON']) == views
    assert 'Accepted native attachment views' in (root / 'actual.html').read_text()
    groups.append({'name': name, 'report': str(root / 'actual.json'), 'actualExitCode': process.returncode,
        'acceptedRevision': view['revision'], 'finalNames': names, 'immutableArtifacts': len(artifacts),
        'actualSyscall': syscall, 'remainingSyscallPID': None})
    return report, view, names

report, view, names = run('array-identity-sparse-order-and-mutable-metadata', r"""
export default defineNativeSuite({tests:[{title:'native array view',run:async(_,i)=>{
 const held={name:'original',contentType:'x',path:'missing\\ud800'};
 i.attachments.push(held,{name:'bytes',contentType:'x',body:Buffer.from('real snapshot')},{name:'tail',contentType:'x'});
 const first=i.attachments[0],bytes=i.attachments[1];assert.equal(first,held);
 held.name='later';assert.equal(first.name,'later');first.contentType='author edited';
 await Promise.resolve();i.attachments.reverse();assert.equal(i.attachments[2],first);
 i.attachments.splice(0,1,{name:'replace',contentType:'x'});delete i.attachments[1];assert.equal(1 in i.attachments,false);
 i.attachments[1]=first;i.attachments.push(bytes);i.attachments.copyWithin(0,1,2);i.attachments.length=3;
 assert.equal(i.attachments[0],first);assert.equal(i.attachments[1],first);assert.equal(i.attachments[2],first);
}}]});
""")
assert names == ['original', 'original', 'original'] and len(report['attachments']) == 2 and all(row['_data'] == b'real snapshot' for row in report['attachments'])

report, view, names = run('same-original-push-occurrences-keep-native-history', r"""
export default defineNativeSuite({tests:[{title:'native insertion identity',run:async(_,i)=>{
 const bytes=Buffer.from([1,2]),held={name:'initial',contentType:'x',body:bytes};
 i.attachments.push(held,held);assert.equal(i.attachments[0],held);assert.equal(i.attachments[1],held);assert.equal(i.attachments[0].body,bytes);
 held.name='edited push';bytes[0]=9;i.attachments.push(held);assert.equal(i.attachments[2],held);
 i.attachments.unshift(held);i.attachments.splice(1,0,held);i.attachments[5]=held;i.attachments.reverse();
 assert(i.attachments.every(entry=>entry===held));assert(i.attachments.every(entry=>entry.body===bytes));
 delete held.body;held.path='author reference only';held.contentType='after snapshot';i.attachments.length=0;
}}]});
""")
assert names == [] and [row['name'] for row in report['attachmentDeclarations']] == ['initial','initial','edited push']
expected = {row['requestId']: (bytes([9,2]) if row['name'] == 'edited push' else bytes([1,2])) for row in report['attachmentDeclarations']}
assert len(report['attachments']) == 3 and all(row['_data'] == expected[row['requestId']] for row in report['attachments'])
assert len({row['id'] for row in report['attachmentDeclarations']}) == 3

report, view, names = run('direct-push-body-with-independent-authored-path', r"""
export default defineNativeSuite({tests:[{title:'body with reference metadata',run:async(_,i)=>{
 const path=root+'/genuine-input.bin';writeFileSync(path,Buffer.from([65,66,67]));const original=statSync(path);
 const bytes=Buffer.from([0,128,255]),held={name:'both initial',contentType:'application/x-original',body:bytes,path};
 i.attachments.push(held,{name:'empty authored path',contentType:'x',body:Buffer.alloc(0),path:''});assert.equal(i.attachments[0],held);assert.equal(i.attachments[0].body,bytes);
 held.name='edited author';held.path='missing author path';bytes.fill(9);
 const after=statSync(path);assert.equal(after.ino,original.ino);assert.equal(after.dev,original.dev);assert.equal(readFileSync(path).toString('hex'),'414243');
}}]});
""")
assert names == ['both initial','empty authored path'] and len(report['attachmentDeclarations']) == 2 and len(report['attachments']) == 2
first, empty = report['attachmentDeclarations']
assert first['sourceKind'] == empty['sourceKind'] == 'body' and first['declaredPath'].endswith('/genuine-input.bin') and empty['declaredPath'] == ''
assert pathlib.Path(first['declaredPath']).read_bytes() == bytes([65,66,67])
expected = {first['requestId']: bytes([0,128,255]), empty['requestId']: b''}
assert all(row['_data'] == expected[row['requestId']] for row in report['attachments'])

report, view, names = run('ordinary-step-publication-and-later-withdrawal', r"""
export default defineNativeSuite({tests:[{title:'native publication view',run:async(_,i)=>{
 await i.attach('ordinary',{body:'actual ordinary'});await i.step('real owner',async step=>{await step.attach('step owned',{body:'actual step'})});
 assert.deepEqual(i.attachments.map(x=>x.name),['ordinary','step owned']);const ordinary=i.attachments.shift();assert.equal(ordinary.name,'ordinary');
}}]});
""")
assert names == ['step owned'] and len(report['attachments']) == 2 and len(view['publicationReceipts']) == 2
assert all(row['accepted'] and row['end'] == 'acknowledged' for row in view['publicationReceipts'])
assert next(row for row in report['attachments'] if row['name'] == 'step owned')['stepId'] != '0'

report, view, names = run('ordinary-original-Buffer-alias-with-immutable-file-history', r"""
export default defineNativeSuite({tests:[{title:'ordinary original Buffer identity',run:async(_,i)=>{
 const original=Buffer.from([0,128,255]);await i.attach('ordinary Buffer',{body:original,contentType:'application/x-original'});
 const entry=i.attachments[0];assert.equal(entry.body,original);assert.equal(entry.body.toString('hex'),'0080ff');
 original[0]=17;assert.equal(entry.body[0],17);entry.body[1]=23;assert.equal(original.toString('hex'),'1117ff');
 entry.name='author changed';entry.contentType='author/type';assert.equal(i.attachments[0],entry);
}}]});
""")
assert names == ['ordinary Buffer'] and len(report['attachments']) == 1 and report['attachments'][0]['_data'] == bytes([0,128,255])
assert report['attachments'][0]['contentType'] == 'application/x-original' and report.get('attachmentDeclarations', []) == []
assert len(view['publicationReceipts']) == 1 and view['publicationReceipts'][0]['accepted'] and view['publicationReceipts'][0]['end'] == 'acknowledged'

report, view, names = run('same-attempt-body-afterEach-view', r"""
let saved;export default defineNativeSuite({hooks:{beforeEach:[{run:(_,i)=>{saved=i.attachments;saved.push({name:'before',contentType:'x'})}}],afterEach:[{run:(_,i)=>{assert.equal(i.attachments,saved);saved.reverse();saved.push({name:'after',contentType:'x'})}}]},tests:[{title:'real hook owner',run:async(_,i)=>{assert.equal(i.attachments,saved);await Promise.resolve();saved[0]={name:'body replacement',contentType:'x'}}}]});
""")
assert names == ['body replacement', 'after'] and len(report['attachmentDeclarations']) == 3
assert {row['callbackKind'] for row in view['changes']} == {'beforeEach', 'body', 'afterEach'}

report, view, names = run('real-held-file-withdrawal-before-callback-return', r"""
export default defineNativeSuite({hooks:{afterEach:[{run:()=>writeFileSync(root+'/after.marker','actual afterEach')}]},tests:[{title:'held writer',run:async(_,i)=>{
 i.attachments.push({name:'held',contentType:'x',body:Buffer.from('actual held file')});await ready(process.env.SUITECUT_ATTACHMENT_TEST_MARKER);
 i.attachments.pop();writeFileSync(root+'/author-returned.marker','actual author return');
}}]});
""", hold='file')
assert names == [] and len(report['attachments']) == 1 and report['attachments'][0]['_data'] == b'actual held file'
assert report['attachmentDeclarations'][0]['receipt'] == 'committed'

report, view, names = run('real-removed-EFBIG-held-rollback-preserves-original-boxed-error', r"""
export default defineNativeSuite({hooks:{afterEach:[{run:()=>writeFileSync(root+'/after.marker','actual afterEach')}]},tests:[{title:'real error identity',run:async(_,i)=>{
 await i.step('actual boxed owner',async()=>{i.attachments.push({name:'fails physically',contentType:'x',body:Buffer.from('actual bytes exceed native limit')});
 i.attachments.length=0;const original=Error('actual original author error');writeFileSync(root+'/original-stack.txt',original.stack);writeFileSync(root+'/author-returned.marker','actual author return');throw original},{box:true});
}}]});
""", code=1, hold='cleanup', efbig=True)
assert names == [] and report['attachments'] == []
assert 'actual original author error' in report['attempts'][0]['error'] and report['attempts'][0].get('originatingStepId') and report['attempts'][0].get('errorAttribution')
assert report['attachmentDeclarations'][0]['receipt'] == 'failed' and 'errno=27' in report['attachmentDeclarations'][0]['error']

(out / 'report.json').write_text(json.dumps({'passed': len(groups), 'scope': 'Actual native runner array-level mutation, original author/step attribution, immutable file history and publication-view ACK; author entry values remain mutable while accepted insertion/file snapshots stay immutable; direct push can retain simultaneous authored path metadata alongside actual body bytes', 'groups': groups}, indent=2) + '\n')
print('PASS', len(groups), 'actual native mutable attachment array/file/callback scopes')
