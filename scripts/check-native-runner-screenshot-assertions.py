# SPDX-License-Identifier: MIT
"""Real async screenshot matchers with the native runner's actual soft journal.

No bridge installation, mocked observations or report-derived author outcomes.
An outer native proof supervisor must own this entire checker/runner domain.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--runner', type=Path, required=True)
parser.add_argument('--sdk-directory', type=Path, required=True)
parser.add_argument('--browser', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
args = parser.parse_args()
out = args.directory.resolve()
out.mkdir(parents=True, exist_ok=True)
sdk = args.sdk_directory.resolve()

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

inputs = [Path(__file__).resolve(), args.runner.resolve(), args.browser.resolve()]
inputs += sorted(sdk.glob('*.js'))
pins = {str(path): digest(path) for path in inputs}
(out / 'inputs.json').write_text(json.dumps(pins, indent=2))
fixture = r'''
import assert from 'node:assert/strict';
import fs from 'node:fs';
import process from 'node:process';
import {defineNativeSuite,createNativeBrowserFixtures,expectNative,NativeAssertionError} from API;
const fixtures=createNativeBrowserFixtures({browser:{executablePath:BROWSER,headless:true,
width:80,height:60,framesPerSecond:60,onDiagnostic:text=>fs.appendFileSync(DIAGNOSTIC,text)}});
export default defineNativeSuite({fixtures,tests:[{title:'Actual screenshot soft identity and hard IO',
fixtures:['page'],run:async({page},info)=>{
  const children=[];
  for(const name of fs.readdirSync('/proc')){
    if(!/^\d+$/.test(name))continue;
    try{const raw=fs.readFileSync('/proc/'+name+'/stat','utf8');
      const row=raw.slice(raw.lastIndexOf(')')+1).trim().split(/\s+/);
      if(Number(row[1])===process.pid)children.push({pid:Number(name),start:row[19]});
    }catch(error){if(error.code!=='ENOENT'&&error.code!=='ESRCH')throw error}
  }
  assert.equal(children.length,1);const helper=children[0];
  assert.equal(fs.realpathSync('/proc/'+helper.pid+'/exe'),fs.realpathSync(BROWSER));
  fs.writeFileSync(HELPER,JSON.stringify(helper));
  await page.setContent('<style>html,body{margin:0;background:#0000ff}</style><div id="target"></div>');
  const before=await page.evaluate(()=>document.documentElement.outerHTML);
  const baseline=await page.screenshot({scale:'css'});
  await expectNative.soft(page).toHaveScreenshot(baseline,{timeout:2000,scale:'css'});
  assert.equal(info.errors.length,0);
  assert.equal(await page.evaluate(()=>document.documentElement.outerHTML),before);
  await page.evaluate(()=>{document.body.style.background='#ff0000'});
  const actualValue={actualCallbackValue:31};
  const returned=await info.step('native screenshot soft step',async()=>{
    await expectNative.soft(page,'Native screenshot authored context').toHaveScreenshot(baseline,
      {timeout:300,scale:'css',artifactsDir:ARTIFACTS});return actualValue;
  });
  assert.strictEqual(returned,actualValue);assert.equal(info.errors.length,1);
  const first=info.errors[0];assert(first instanceof NativeAssertionError);
  assert(first.message.startsWith('Native screenshot authored context\n\n'));
  assert(first.message.includes('Different pixels: 4800/4800'));
  const marker={genuineOriginalError:true};Object.defineProperty(first,'actualMarker',{value:marker});
  assert.strictEqual(info.errors[0],first);assert.strictEqual(info.errors[0].actualMarker,marker);
  await expectNative.soft(page).not.toHaveScreenshot(baseline,{timeout:2000,scale:'css'});
  assert.equal(info.errors.length,1);
  const red=await page.screenshot({scale:'css'});
  await expectNative.soft(page,{message:'Actual negation'}).not.toHaveScreenshot(red,{timeout:200,scale:'css'});
  assert.equal(info.errors.length,2);assert.strictEqual(info.errors[0],first);
  assert(info.errors[1].message.startsWith('Actual negation\n\nexpect(page).not.'));
  await assert.rejects(expectNative.soft(page).toHaveScreenshot(MISSING,{timeout:400}),
    error=>!(error instanceof NativeAssertionError));
  fs.writeFileSync(IOFILE,'Actual existing file');
  await assert.rejects(expectNative.soft(page).toHaveScreenshot(baseline,
    {timeout:200,scale:'css',artifactsDir:IOFILE}),error=>!(error instanceof NativeAssertionError));
  assert.equal(info.errors.length,2,'Real baseline/IO failures must not become assertion mismatches');
  const state=await page.runAutomation('diagnostics');
  for(const name of ['nativeScreenshotAssertionScopes','nativeScreenshotAssertionPhysical',
    'nativeScreenshotComparisonLogical','nativeScreenshotComparisonPhysical','nativeScreenshotComparisonBytes',
    'artifactPhysicalCapture','artifacts','nativeSelectorWaits','nativeSelectorWaitPhysical'])assert.equal(state[name],0,name);
  process.stdout.write(JSON.stringify({event:'actual-screenshot-soft-continued',errors:info.errors.length,state})+'\n');
}}]});
'''
for key, value in {
    'API': (sdk / 'native.js').as_uri(), 'BROWSER': str(args.browser.resolve()),
    'DIAGNOSTIC': str(out / 'native-stderr.log'), 'HELPER': str(out / 'helper.json'),
    'ARTIFACTS': str(out / 'artifacts'), 'MISSING': str(out / 'missing.png'),
    'IOFILE': str(out / 'actual-file'),
}.items():
    fixture = fixture.replace(key, json.dumps(value))
suite = out / 'suite.mjs'
suite.write_text(fixture)
node = shutil.which('node')
assert node
command = [str(args.runner.resolve()), '--node', node, '--host-script', str(sdk / 'native-runner-host.js'),
           '--suite', str(suite), '--report', str(out / 'report.json')]
(out / 'command.json').write_text(json.dumps(command, indent=2))
with (out / 'stdout').open('wb') as stdout, (out / 'stderr').open('wb') as stderr:
    result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=30)
changed = [path for path, expected in pins.items() if digest(Path(path)) != expected]
(out / 'audit.json').write_text(json.dumps({'exit': result.returncode, 'changed': changed}, indent=2))
assert result.returncode == 1 and not changed
assert (out / 'stderr').stat().st_size == 0
assert not (out / 'native-stderr.log').exists() or (out / 'native-stderr.log').stat().st_size == 0
report = json.loads((out / 'report.json').read_text())
rows = report.get('softAssertions', [])
accepted = [row for row in rows if row.get('acceptedMismatch')]
assert len(accepted) == 2 and not any(row.get('acceptedDeadline') for row in rows)
assert all(row['rawOutcome'] == 'recognizedMismatch' and row['receipt'] == 'mismatch' for row in accepted)
assert any(row.get('rawOutcome') == 'otherFailure' for row in rows)
assert all(not row.get('acceptedMismatch') for row in rows if row.get('rawOutcome') == 'otherFailure')
assert len(report['attempts']) == 1
attempt = report['attempts'][0]
assert attempt['status'] == 'failed' and 'softErrorId' not in attempt
# Actual hard I/O settlement stays primary even when author code observes its
# rejection. It must never be relabeled as the earlier accepted soft mismatch.
hard = next(row for row in rows if row.get('rawOutcome') == 'otherFailure')
assert attempt['error'] == hard['error'] and 'baseline open' in attempt['error']
step = next(row for row in report['steps'] if row.get('softErrorId') == accepted[0]['id'])
assert step['status'] == 'failed' and step['callbackReceiptOK'] and step['receiptOK']
events = [json.loads(line) for worker in report['workers'] for line in bytes.fromhex(worker['stdout']['data']).decode().splitlines()]
assert any(event.get('event') == 'actual-screenshot-soft-continued' for event in events)
helper = json.loads((out / 'helper.json').read_text())
try:
    raw = Path('/proc', str(helper['pid']), 'stat').read_text()
    assert raw[raw.rindex(')') + 1:].split()[19] != helper['start'], 'Actual native helper still exists, including Z'
except FileNotFoundError:
    pass
print('PASS genuine native screenshot soft mismatch, .not, custom Error identity and hard IO guards')
