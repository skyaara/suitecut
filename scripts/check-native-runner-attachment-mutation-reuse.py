# SPDX-License-Identifier: MIT
"""Following actual live-worker mutation/publication-idle proof, no fixture receipts."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--executable', required=True, type=pathlib.Path)
p.add_argument('--sdk-directory', required=True, type=pathlib.Path)
p.add_argument('--directory', required=True, type=pathlib.Path)
p.add_argument('--node', required=True)
a = p.parse_args()
out = a.directory.resolve()
out.mkdir(parents=True, exist_ok=True)
root = pathlib.Path(tempfile.mkdtemp(prefix='actual-', dir=out))
suite = root / 'suite.mjs'
suite.write_text('import{defineNativeSuite}from ' + json.dumps((a.sdk_directory / 'native.js').resolve().as_uri()) + ";import{strict as assert}from'node:assert';\n" + r'''
let setup=0;
export default defineNativeSuite({fullyParallel:true,fixtures:{actualWorker:{scope:'worker',setup:async(_,use)=>{
 const value={setup:++setup,calls:0};await use(value);
}}},tests:Array.from({length:4},(_,index)=>({title:'actual mutable reuse '+index,fixtures:['actualWorker'],run:async({actualWorker},info)=>{
 assert.equal(actualWorker.setup,1);++actualWorker.calls;assert.equal(info.attachments.length,0);
 info.attachments.push({name:'withdrawn '+index,contentType:'x',body:Buffer.from('original '+index)});
 const authored=info.attachments[0];await Promise.resolve();info.attachments.unshift(authored);
 delete info.attachments[0];assert.equal(0 in info.attachments,false);info.attachments.splice(0,1);
 await info.attach('published '+index,{body:'actual publication '+index});
 assert.equal(info.attachments[0],authored);info.attachments.shift();assert.equal(info.attachments.length,1);
 info.attachments.reverse();assert.equal(info.attachments[0].name,'published '+index);
}}))});
''')
command = [a.node, str((a.sdk_directory / 'native-runner-cli.js').resolve()), '--executable', str(a.executable.resolve()),
    '--suite', str(suite), '--report', str(root / 'actual.json'), '--junit', str(root / 'actual.xml'),
    '--html', str(root / 'actual.html'), '--workers', '2', '--global-timeout', '20000']
actual = subprocess.run(command, capture_output=True, timeout=40)
(root / 'stdout.bin').write_bytes(actual.stdout)
(root / 'stderr.bin').write_bytes(actual.stderr)
assert actual.returncode == 0 and not actual.stderr, (actual.returncode, actual.stdout, actual.stderr)
report = json.loads((root / 'actual.json').read_text())
assert not report['globalErrors'] and report['summary']['expected'] == 4
attempts = report['attempts']
views = report['attachmentViews']
assert len(attempts) == len(views) == 4 and len({row['pid'] for row in attempts}) == 2
assert len(report['workerReuse']['hosts']) == 2 and len(report['workerReuse']['borrows']) == 4
transfers = [row for row in report['workerReuse']['borrows'] if row['receipt'] == 'idleTransferred']
assert len(transfers) == 2
for row in transfers:
    host = next(host for host in report['workerReuse']['hosts'] if host['lease'] == row['hostLease'])
    assert row['idleRequest'] is not None and row['acknowledgedMs'] <= row['transferredMs'] <= host['exitedMs']
files = {row['id']: row for row in report['attachments']}
assert len(files) == 8 and len(report['attachmentDeclarations']) == 4
for view in views:
    attempt = next(row for row in attempts if row['generation'] == view['generation'])
    assert (view['testId'], view['workerIndex'], view['pid']) == (attempt['testId'], attempt['workerIndex'], attempt['pid'])
    assert len(view['publicationReceipts']) == 1
    receipt = view['publicationReceipts'][0]
    assert receipt['accepted'] and receipt['end'] == 'acknowledged'
    assert len(view['slots']) == 1 and view['slots'][0] == {'kind': 'artifact', 'id': receipt['artifactId']}
    assert files[receipt['artifactId']]['name'].startswith('published ')
    assert all(row['generation'] == attempt['generation'] for row in view['changes'])
for row in files.values():
    path = pathlib.Path(row['path'])
    data = path.read_bytes()
    info = path.stat()
    assert info.st_ino == int(row['inode']) and info.st_dev == int(row['device']) and len(data) == row['byteLength']
    assert row['writerExitConfirmed'] and row['writerExitCode'] == 0
    assert not pathlib.Path('/proc/' + str(row['writerPid'])).exists()
for host in report['workerReuse']['hosts']:
    assert host['receipt'] == 'physicalRetired' and host['retiredMs'] >= host['exitedMs']
    assert not pathlib.Path('/proc/' + str(host['pid'])).exists()
(root / 'receipt.json').write_text(json.dumps({'scope': 'Four actual authored mutation/publication callbacks in two retained live worker hosts; native idle transfer requires real file/callback/view ACK retirement',
    'command': command, 'actualExitCode': actual.returncode, 'attempts': len(attempts), 'nativeViews': len(views), 'idleTransfers': len(transfers),
    'reportSHA256': hashlib.sha256((root / 'actual.json').read_bytes()).hexdigest()}, indent=2) + '\n')
print('PASS actual native mutable attachment live-worker reuse/idle4', root)
