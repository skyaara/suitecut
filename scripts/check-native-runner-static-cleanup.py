# SPDX-License-Identifier: MIT
"""Real native expected-failure classification of actual fixture/afterEach errors."""
import argparse,pathlib,json,subprocess
p=argparse.ArgumentParser();p.add_argument('--before',type=pathlib.Path);p.add_argument('--executable',type=pathlib.Path,required=True);p.add_argument('--sdk-directory',type=pathlib.Path,required=True);p.add_argument('--directory',type=pathlib.Path,required=True);p.add_argument('--node',required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);sdk=a.sdk_directory.resolve();groups=[]
cases=[('afterEach',"hooks:{afterEach:[{run:()=>{throw Error('actual afterEach error')}}]},tests:[{title:'static actual cleanup',expectedFailure:true,run:()=>{}}]",True),('fixture',"fixtures:{resource:{automatic:true,setup:async(_,use)=>{await use(1);throw Error('actual fixture error')}}},tests:[{title:'static actual fixture',expectedFailure:true,run:()=>{}}]",True),('body',"tests:[{title:'static actual body',expectedFailure:true,run:()=>{throw Error('actual body error')}}]",False)]
for version,exe in [('before',a.before),('after',a.executable)]:
 if exe is None:continue
 for name,source,infra in cases:
  d=out/(version+'-'+name);d.mkdir(exist_ok=True);suite=d/'suite.mjs';suite.write_text('import {defineNativeSuite} from '+json.dumps((sdk/'native-suite.js').as_uri())+';export default defineNativeSuite({'+source+'});');report=d/'report.json';report.unlink(missing_ok=True)
  r=subprocess.run([str(exe.resolve()),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report)],capture_output=True,timeout=10);(d/'stdout.bin').write_bytes(r.stdout);(d/'stderr.bin').write_bytes(r.stderr);assert not r.stderr,r.stderr
  v=json.loads(report.read_text());failed=version=='after' and infra;assert r.returncode==(1 if failed else 0),(version,name,r.returncode,v)
  assert len(v['attempts'])==1 and v['attempts'][0]['status']=='failed' and v['attempts'][0]['expected']==(not failed),v
  assert ('actual '+('afterEach' if name=='afterEach' else name)+' error') in v['attempts'][0]['error']
  groups.append({'version':version,'name':name,'exitCode':r.returncode,'report':str(report),'actualErrorPreserved':True})
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Actual static expectedFailure cleanup classification; before falsely expected afterEach/fixture errors, after rejects them while genuine body failure remains expected','groups':groups},indent=2)+'\n');print('PASS',len(groups),'native static cleanup before/after groups')
