# SPDX-License-Identifier: MIT
"""Actual native grep admission and run/list parity; no browser fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--executable', type=Path, required=True)
p.add_argument('--sdk-directory', type=Path, required=True)
p.add_argument('--directory', type=Path, required=True)
p.add_argument('--node', default=shutil.which('node'))
p.add_argument('--source-root', type=Path)
a = p.parse_args()
assert a.node
sdk = a.sdk_directory.resolve()
exe = a.executable.resolve()
a.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='run-', dir=a.directory.resolve()))
inputs = [exe, Path(a.node).resolve(), Path(__file__).resolve(), *sorted(x for x in sdk.rglob('*') if x.is_file())]
if a.source_root:
    inputs.extend(x for x in (a.source_root/'native/runner').rglob('*') if x.is_file())
    inputs.extend(a.source_root/x for x in ['src/native-runner.ts','src/native-runner-host.ts','src/native-runner-cli.ts','src/native-suite.ts'])
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
before = {str(x):digest(x) for x in sorted(set(inputs))}
(root/'inputs-before.json').write_text(json.dumps(before, indent=2)+'\n')
checks = []
pids = set()

def execute(name, declaration, options, selected, frontend='native', expected=0, error=None, parity=True):
    folder = root/name
    folder.mkdir()
    suite = folder/'suite.mjs'
    events = folder/'events.jsonl'
    suite.write_text('import fs from "node:fs";import process from "node:process";'
        'import {defineNativeSuite} from '+json.dumps((sdk/'native-suite.js').as_uri())+';'
        'const mark=(kind,info)=>fs.appendFileSync('+json.dumps(str(events))+',JSON.stringify({kind,id:info?.testId??null,pid:process.pid})+"\\n");mark("import");'
        'const callback=(_,info)=>mark("body",info);const test=(title,extra={})=>({title,run:callback,...extra});'
        'const hooks={beforeAll:[{run:()=>mark("hook")}],afterAll:[{run:()=>mark("hook")}]};'
        'const fixtures={automatic:{automatic:true,setup:async({},use)=>{mark("fixture");await use(1);mark("teardown")}}};'
        'export default defineNativeSuite('+declaration+');\n')
    options=[part.replace('{ROOT}',str(root)).replace('{FOLDER}',str(folder)) for part in options]
    syntax=subprocess.run([a.node,'--check',str(suite)],capture_output=True,timeout=10)
    (folder/'syntax-stdout').write_bytes(syntax.stdout)
    (folder/'syntax-stderr').write_bytes(syntax.stderr)
    assert syntax.returncode==0,(name,'authored fixture syntax',syntax.stderr)
    (folder/'suite-before.sha256').write_text(digest(suite)+'\n')
    command = [str(exe),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js')] if frontend=='native' else [a.node,str(sdk/'native-runner-cli.js'),'--executable',str(exe)]
    def launch(mode):
        output = folder/(mode+'.json')
        argv = [*command,'--suite',str(suite),'--report',str(output),*(['--list'] if mode=='list' else []),*options]
        (folder/(mode+'-argv.json')).write_text(json.dumps(argv,indent=2)+'\n')
        actual = subprocess.run(argv,capture_output=True,timeout=30)
        (folder/(mode+'-stdout')).write_bytes(actual.stdout)
        (folder/(mode+'-stderr')).write_bytes(actual.stderr)
        assert actual.returncode==expected,(name,mode,actual.returncode,actual.stderr)
        if expected==2:
            assert not output.exists() and error.encode() in actual.stderr,(name,actual.stderr)
            assert not events.exists(),(name,'invalid pattern imported author module')
            return None
        assert not actual.stderr,(name,actual.stderr)
        value=json.loads(output.read_bytes())
        workers=value['collectionWorkers'] if mode=='list' else value['workers']
        for worker in workers:
            pids.add(worker['pid'])
            assert not Path('/proc',str(worker['pid'])).exists(),('actual worker survives publication',worker)
        if expected:
            errors=value['errors'] if mode=='list' else value['globalErrors']
            assert any(error in item for item in errors),(name,value)
        elif mode=='list':
            assert value['selectedTestIds']==selected,(name,value)
            assert len(value['tests'])>=len(selected) and not value['errors']
        else:
            assert not value['globalErrors'] and not value.get('unrun')
            assert {x['testId'] for x in value['attempts']}==set(selected),(name,value)
            assert all(x['status']=='passed' for x in value['attempts'])
        if error=='No tests found':
            assert not value.get('attempts') and not value.get('skipped')
        return value
    listing=launch('list')
    observed=[json.loads(x) for x in events.read_text().splitlines()] if events.exists() else []
    assert all(x['kind']=='import' for x in observed),(name,'list admitted author callback',observed)
    if parity and expected!=2:
        run=launch('run')
        if expected==0:
            titles={x['testId']:x['title'] for x in listing['tests']}
            assert all(titles[x['testId']]==x['title'] for x in run['attempts'])
            observed=[json.loads(x) for x in events.read_text().splitlines()]
            assert {x['id'] for x in observed if x['kind']=='body'}==set(selected)
        elif error=='No tests found':
            assert all(x['kind']=='import' for x in (json.loads(line) for line in events.read_text().splitlines()))
    assert digest(suite)==(folder/'suite-before.sha256').read_text().strip()
    checks.append({'name':name,'passed':True,'listing':str(folder/'list.json') if listing else None})
    (root/'partial-report.json').write_text(json.dumps({'scope':'Completed cases only; final pins and physical audit pending','cases':checks},indent=2)+'\n')
    return listing

flat='{fixtures,hooks,tests:[test("test AA"),test("test BB"),test("test CC"),test("emoji 😀"),test("lone\\ud800"),test("slash a/b")]}'
execute('bare-case',flat,['-g','TesT Aa'],['1'])
execute('canonical-relative-file',flat,['--test-directory','{ROOT}','-g','/^canonical-relative-file\\/suite.mjs test AA$/'],['1'],frontend='cli')
execute('canonical-outside-root',flat,['--test-directory',str(sdk),'-g','AA'],None,expected=1,error='outside grep test-directory')
execute('literal-case',flat,['-g','/TesT Aa/','--pass-with-no-tests'],[])
execute('literal-i-cli',flat,['--grep','/TesT Aa/i'],['1'],frontend='cli')
execute('range-and-invert',flat,['-g','test [A-C]','-G','BB'],['1','3'])
execute('repeated-include-last',flat,['-g','AA','--grep','BB'],['2'])
execute('repeated-invert-last',flat,['-G','AA','--grep-invert','BB'],['1','3','4','5','6'])
execute('utf16-dot-units',flat,['-g','/emoji ..$/'],['4'])
execute('utf16-lone-unit',flat,['-g','/lone\\ud800$/'],['5'])
execute('literal-slash',flat,['-g','/slash a\\/b/'],['6'])
execute('unsupported-flag-suffix-is-bare',flat,['-g','/test AA/u','--pass-with-no-tests'],[])
execute('filter-before-shard',flat,['-g','test [A-C]','--shard','2/2'],['3'])
execute('ids-intersect',flat,['-g','test [AB]','--test-id','2','--test-id','3'],['2'])
execute('unknown-id-still-fails',flat,['-g','nothing','--test-id','999'],None,expected=1,error='selected test identity')
execute('empty-filter-no-fixture-hooks',flat,['-g','nothing','--pass-with-no-tests'],[])
execute('backreference-admitted-native',flat,['-g','/test (A)\\1/'],['1'])
execute('lookahead-admitted-native',flat,['-g','/test (?=AA)AA/'],['1'])
execute('nonascii-i-admitted-native','{tests:[test("π"),test("Π"),test("P")]}',['-g','π'],['1','2'])
execute('default-empty-selection-fails',flat,['-g','ABSENT'],None,expected=1,error='No tests found')
execute('default-empty-declarations-fails','{fixtures,hooks,tests:[]}',[],None,expected=1,error='No tests found')
execute('explicit-empty-declarations-pass','{fixtures,hooks,tests:[]}',['--pass-with-no-tests'],[],frontend='cli')
execute('empty-shard-succeeds',flat,['--shard','9/9'],[])
for name,pattern,message in [('bad-syntax','[','unterminated'),('duplicate-flags','/a/ii','duplicate')]:
    execute('reject-'+name,flat,['-g',pattern],None,expected=2,error=message,parity=False)
projects='''{fixtures,hooks,projects:[{name:'Project One',dependencies:['setup'],entries:[{title:'outer suite',tag:['@inherited','@same'],entries:[test('test AA',{tag:['@leaf','@same']}),test('test BB')]}]},{name:'setup',default:false,teardown:'cleanup',tests:[test('support one'),test('support two')]},{name:'cleanup',default:false,tests:[test('cleanup')]}]}'''
value=execute('project-tags-support-whole',projects,['-g','/^Project One suite.mjs outer suite @inherited @same test AA @leaf @same$/'],['1','3','4','5'])
assert value['tests'][0]['grepTitle']=='Project One suite.mjs outer suite @inherited @same test AA @leaf @same'
assert [x['selectionRole'] for x in value['projects']]==['primary','dependency','teardown']
execute('project-no-deps',projects,['-g','test AA','--no-deps'],['1'])
execute('project-parallel',projects,['-g','test AA','--workers','2'],['1','3','4','5'])
execute('project-empty-primary-no-support',projects,['-g','unmatched','--pass-with-no-tests'],[])
execute('focus-intersection','{fixtures,tests:[test("test AA"),test("test BB",{only:true})]}',['-g','test AA','--pass-with-no-tests'],[])
execute('forbid-focus-before-filter','{tests:[test("test AA",{only:true})]}',['-g','unmatched','--forbid-only'],None,expected=1,error='Focused')
config_projects="""{fixtures,hooks,grep:[/AA$/,/BB$/],grepInvert:[/setup two$/],projects:[{name:'body',dependencies:['setup'],grep:[/AA$/],tests:[test('body AA'),test('body BB')]},{name:'setup',default:false,teardown:'cleanup',grep:[/setup/],tests:[test('setup one'),test('setup two')]},{name:'cleanup',default:false,grep:[/cleanup/],tests:[test('cleanup')]}]}"""
execute('config-filters-support-catalog',config_projects,['--project','body','-g','body AA'],['1','3','5'])
execute('config-empty-array','{fixtures,hooks,grep:[],tests:[test("AA")]}',[],None,expected=1,error='No tests found')
execute('config-empty-array-explicit-pass','{fixtures,hooks,grep:[],tests:[test("AA")]}',['--pass-with-no-tests'],[])
execute('config-project-field-overrides','{grep:/AA$/,grepInvert:/AA$/,projects:[{name:"p1",tests:[test("AA"),test("BB")]},{name:"p2",grep:[/AA$/,/BB$/],grepInvert:[],tests:[test("AA"),test("BB")]}]}',[],['3','4'])
execute('config-unicode-property-and-v-set','{grep:/ [\\p{ASCII}&&\\p{Letter}]+$/v,tests:[test("AA"),test("π")]}',[],['1'])
execute('config-unicode-greek','{grep:/π$/iu,tests:[test("π"),test("Π"),test("P")]}',[],['1','2'])
execute('config-v-string','{grep:/[\\q{abc|xyxy}]$/v,tests:[test("abc"),test("xyxy"),test("a")]}',[],['1','2'])
execute('config-sticky-prefix','{grep:/suite.mjs/y,tests:[test("AA"),test("BB")]}',[],['1','2'])
execute('config-global-fresh','{grep:/AA$/g,tests:[test("first AA"),test("second AA")]}',[],['1','2'])
value=execute('config-global-tag-placement',"{tag:['@global','@same'],entries:[{title:'outer',tag:['@suite','@same'],entries:[test('case',{tag:['@leaf','@same']})]}]}",['-g','/^suite.mjs @global @same outer @suite @same case @leaf @same$/'],['1'])
assert value['tests'][0]['grepTitle']=='suite.mjs @global @same outer @suite @same case @leaf @same'
after={str(x):digest(x) for x in sorted(set(inputs))}
(root/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
assert before==after
assert not [pid for pid in pids if Path('/proc',str(pid)).exists()]
(root/'report.json').write_text(json.dumps({'passed':len(checks),'cases':checks,'inputsUnchanged':True,'exactWorkerPIDs':sorted(pids),'survivorsIncludingZombies':[]},indent=2)+'\n')
print(json.dumps({'passed':len(checks),'directory':str(root)}))
