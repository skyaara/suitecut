# SPDX-License-Identifier: MIT
"""Actual native directory fsync/waitpid/rollback gates before callback admission."""
import argparse,atexit,json,os,pathlib,signal,subprocess,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--executable',required=True);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);p.add_argument('--library',required=True);p.add_argument('--directory',required=True,type=pathlib.Path);a=p.parse_args();receipt=a.directory.resolve();receipt.mkdir(parents=True,exist_ok=True);out=pathlib.Path(tempfile.mkdtemp(prefix='cases-',dir=receipt));sdk=a.sdk_directory.resolve();rows=[];live=[]
def until(fn,seconds=7):
 end=time.monotonic()+seconds
 while not fn():assert time.monotonic()<end,'actual native output marker/exit absent';time.sleep(.002)
def marker(path):
 # O_CREAT makes the path visible before the real observer write. A complete
 # newline-terminated record, not existence alone, proves that write finished.
 try:text=path.read_text()
 except FileNotFoundError:return None
 if not text.endswith('\n'):return None
 parts=text.split();assert len(parts)==5,'malformed complete native output marker'
 assert all(part.isdecimal() for part in parts[:4]) and parts[3]=='1','invalid native output marker identity'
 assert bytes.fromhex(parts[4]),'empty native output marker path'
 return parts
def state(pid):
 try:return pathlib.Path('/proc',str(pid),'stat').read_text().rsplit(')',1)[1].split()[0]
 except FileNotFoundError:return None
def cleanup():
 for proc in live:
  if proc.poll()is None:
   proc.send_signal(signal.SIGINT)
   try:proc.communicate(timeout=9)
   except subprocess.TimeoutExpired:proc.kill();proc.communicate(timeout=4)
atexit.register(cleanup)
def start(name,hold='directory',timeout=30000,exit_zero=False,deny_cleanup=False):
 root=out/name;root.mkdir();suite=root/'suite.mjs';suite.write_text('import{defineNativeSuite}from '+json.dumps((sdk/'native.js').as_uri())+";import{writeFileSync}from'node:fs';const mark=()=>writeFileSync(process.env.ACTUAL_OUTPUT_CALLBACK,'actual user callback');export default defineNativeSuite({hooks:{beforeAll:[{run:mark}],beforeEach:[{run:mark}],afterEach:[{run:mark}]},tests:[{title:'actual output ownership',timeout:"+str(timeout)+",expectedFailure:true,run:mark}]});")
 env={**os.environ,'LD_PRELOAD':a.library,'ACTUAL_OUTPUT_CALLBACK':str(root/'callback'),'SUITECUT_OUTPUT_TEST_HOLD':hold,'SUITECUT_OUTPUT_TEST_MARKER':str(root/'writer'),'SUITECUT_OUTPUT_TEST_RELEASE':str(root/'release'),'SUITECUT_OUTPUT_TEST_CLEANUP_MARKER':str(root/'cleanup'),'SUITECUT_OUTPUT_TEST_CLEANUP_RELEASE':str(root/'cleanup-release')}
 if exit_zero:env['SUITECUT_OUTPUT_TEST_EXIT_ZERO']='1'
 if deny_cleanup:env['SUITECUT_OUTPUT_TEST_CLEANUP_DENY']='1'
 proc=subprocess.Popen([a.executable,'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(root/'actual.json'),'--junit',str(root/'actual.xml'),'--html',str(root/'actual.html')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env);live.append(proc);until(lambda:marker(root/'writer')or proc.poll()is not None);assert proc.poll()is None,(name,proc.returncode);parts=marker(root/'writer');assert parts is not None;writer=int(parts[0]);directory=pathlib.Path(bytes.fromhex(parts[4]).decode());assert parts[3]=='1'and directory.is_dir()and int(parts[2])==directory.stat().st_ino;assert state(writer)not in[None,'Z']and not(root/'callback').exists()and not(root/'actual.json').exists();return root,proc,writer,directory
def finish(root,proc,writer,code):
 stdout,stderr=proc.communicate(timeout=12);(root/'stdout.bin').write_bytes(stdout);(root/'stderr.bin').write_bytes(stderr);assert proc.returncode==code and not stderr,(root.name,proc.returncode,stdout,stderr);r=json.loads((root/'actual.json').read_text());assert state(writer)is None and all(state(w['pid'])is None for w in r['workers']);o=r['outputs'][0];assert len(r['outputs'])==1 and o['writerPid']==writer and o['physicalReceiptMonotonicMs']>=o['writerExitReceiptMonotonicMs'];
 if 'rollbackPid'in o:assert state(o['rollbackPid'])is None
 rows.append({'scope':root.name,'actualExitCode':proc.returncode,'actualWriterPid':writer,'report':str(root/'actual.json')});return r,o
root,proc,writer,directory=start('real-writer-gates-all-callbacks');(root/'release').write_text('actual fsync may execute');r,o=finish(root,proc,writer,1);assert(root/'callback').exists()and o['status']=='ready'and o['writerExitCode']==0 and o['outputDir']==str(directory)and o['assignmentReceiptMonotonicMs']>=o['writerExitReceiptMonotonicMs']and r['attempts'][0]['status']=='passed'and not r['attempts'][0]['expected'] # actual expected-failure pass remains unexpected
root,proc,writer,directory=start('original-budget-includes-actual-directory-preparation',timeout=150);r,o=finish(root,proc,writer,1);assert not(root/'callback').exists()and not directory.exists()and r['attempts'][0]['status']=='timedOut'and not r['attempts'][0]['expected']and 'outputDir'not in o and o['writerSignaled']and o['writerExitCode']==9 and o['rollbackReceipt']
root,proc,writer,directory=start('actual-rollback-exit-gates-interrupt-publication',hold='both');proc.send_signal(signal.SIGINT);until(lambda:marker(root/'cleanup')or proc.poll()is not None);assert proc.poll()is None;cleanup_marker=marker(root/'cleanup');assert cleanup_marker is not None;cleanup_pid=int(cleanup_marker[0]);assert state(writer)is None and state(cleanup_pid)not in[None,'Z']and not directory.exists()and not(root/'callback').exists()and not(root/'actual.json').exists();(root/'cleanup-release').write_text('actual cleanup fsync may execute');r,o=finish(root,proc,writer,130);assert o['rollbackPid']==cleanup_pid and o['rollbackExitCode']==0 and o['rollbackReceipt']and r['attempts'][0]['status']=='interrupted'and not r['attempts'][0]['expected']and r['globalErrors']
root,proc,writer,directory=start('actual-foreign-replacement-is-preserved');moved=root/'moved-actual-root';directory.rename(moved);directory.mkdir();(directory/'foreign').write_text('actual foreign bytes');(root/'release').write_text('actual writer validates original inode');r,o=finish(root,proc,writer,1);assert not(root/'callback').exists()and r['attempts'][0]['status']=='failed'and not r['attempts'][0]['expected']and 'outputDir'not in o and o['foreignPreserved']and o['rollbackReceipt']and (directory/'foreign').read_text()=='actual foreign bytes'and moved.is_dir()
root,proc,writer,directory=start('real-output-rollback-permission-error',deny_cleanup=os.geteuid()==0);parent=directory.parent
if os.geteuid()!=0:parent.chmod(0)
try:
 proc.send_signal(signal.SIGINT);r,o=finish(root,proc,writer,130);assert not(root/'callback').exists()and 'errno=13'in o['cleanupError']and not o['rollbackReceipt']and o['rollbackExitCode']!=0 and r['globalErrors']and not r['attempts'][0]['expected']
finally:parent.chmod(0o700)
assert directory.is_dir()
root,proc,writer,directory=start('real-writer-exit-zero-is-not-readiness',exit_zero=True);(root/'release').write_text('actual writer exits zero before READY');r,o=finish(root,proc,writer,1);assert not(root/'callback').exists()and not directory.exists()and not o['readyReceipt']and o['writerExitCode']==0 and not o['writerSignaled']and 'outputDir'not in o and r['attempts'][0]['status']=='failed'and not r['attempts'][0]['expected']and o['rollbackReceipt']
(receipt/'report.json').write_text(json.dumps({'passed':len(rows),'scope':'Actual native per-attempt output preparation ownership: held fsync gates every callback; original timeout and SIGINT cancel the real writer; actual rollback exit gates final report/domain retirement; foreign inode and real EACCES preserve truthful error/physical outcomes. No CEF and no fabricated worker/file receipt.','groups':rows},indent=2)+'\n');print('PASS',len(rows),'actual output directory admission/cancel/rollback/waitpid groups')
