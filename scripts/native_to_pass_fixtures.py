# SPDX-License-Identifier: MIT
"""Authored public callbacks only; native owns every retry and final verdict."""

PRELUDE = r'''
import assert from 'node:assert/strict';
import process from 'node:process';
import {appendFileSync,existsSync} from 'node:fs';
import {defineNativeSuite,expectNative} from API;
const mark=(phase,info,extra={})=>appendFileSync(EVENTS,JSON.stringify({phase,pid:process.pid,testId:info?.testId,retry:info?.retry,workerIndex:info?.workerIndex,hook:info?.hook,...extra})+'\n');
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const afterAbort=async signal=>{if(!signal.aborted)await new Promise(resolve=>signal.addEventListener('abort',resolve,{once:true}));};
'''

# These are genuine author conditions, not a JavaScript retry loop: each callback
# is invoked only by the new native RUN packet. Reported dispatch owns counts.
SCOPES = {
'callbacks-hooks-retry-and-stale': {
    'exit': 0,
    'options': ['--max-failures', '1'],
    'body': r'''
let saved,liveInfo,resumeStale,staleDone,staleRuns=0;
const hook=async(_,info)=>{
 let calls=0;
 await expectNative(()=>{mark('invocation',info,{label:info.hook});if(++calls===1)throw new Error('ACTUAL_HOOK_CONDITION');}).toPass({timeout:2000,intervals:[10]});
 assert.equal(calls,2);mark('hook-done',info);
};
export default defineNativeSuite({hooks:{beforeEach:[{run:hook}],afterEach:[{run:hook}]},tests:[
 {title:'native retry callbacks stay inside one actual attempt',retries:1,timeout:5000,run:async(_,info)=>{
  mark('body',info,{label:'retry'});let calls=0;
  await info.step('authored outer',()=>info.step('authored inner',async()=>{
   const value=await expectNative(()=>{mark('invocation',info,{label:'body'});if(++calls<4)throw new Error('ACTUAL_AUTHOR_CONDITION_'+calls);return false;}).toPass({timeout:2000,intervals:[10,20]});
   assert.equal(value,undefined);assert.equal(calls,4);
  }));
  if(!info.retry)throw new Error('ACTUAL_OUTER_ATTEMPT_FAILURE');
  saved=expectNative(()=>{mark('invocation',liveInfo,{label:'saved-later-owner'});return false;});
  staleDone=(async()=>{await new Promise(resolve=>{resumeStale=resolve});let rejected=false;
   try{await expectNative(()=>{++staleRuns;mark('MUST_NOT_RUN',info);}).toPass({timeout:500,intervals:[0]});}
   catch(error){rejected=true;assert(error instanceof Error);mark('stale-rejected',info);}
   assert(rejected);
  })();
 }},
 {title:'later live owner accepts saved assertion but rejects stale async context',timeout:5000,run:async(_,info)=>{
  mark('body',info,{label:'saved'});liveInfo=info;await saved.toPass({timeout:1000,intervals:[0]});
  resumeStale();await staleDone;assert.equal(staleRuns,0);mark('saved-and-stale-checked',info);
 }},
 {title:'unawaited ordinary attachment failure retries after actual file settlement',timeout:5000,run:async(_,info)=>{
  mark('body',info,{label:'attachment'});let calls=0;
  await info.step('public captured attachment owner',async step=>{
   await expectNative(async()=>{mark('invocation',info,{label:'attachment'});if(++calls===1){
    await info.step('nested raw-P attachment owner',async nested=>{
     mark('nested-attachment-raw',info,{stepId:nested.id});
     void nested.attach('actual nested missing input',{path:MISSING}).catch(()=>{});
    });return;
   }
   await info.attach('actual test-owned file',{body:'ACTUAL_TEST_OWNED_BYTES',contentType:'text/plain'});
   await step.attach('actual captured-step file',{body:'ACTUAL_CAPTURED_STEP_BYTES',contentType:'text/plain'});
   }).toPass({timeout:2000,intervals:[10]});
  });
  assert.equal(calls,2);mark('attachment-recovered',info);
 }},
 {title:'actual append admission recovery permits later local and test skip',timeout:5000,run:async(_,info)=>{
  mark('body',info,{label:'append-skip'});let calls=0;
  await expectNative(()=>{mark('invocation',info,{label:'append-skip'});if(++calls===1){
   info.attachments.push(...Array.from({length:5},(_,index)=>({name:'append-slot-'+index,contentType:'text/plain',body:Buffer.from('x')})));
  }}).toPass({timeout:2000,intervals:[10]});
  assert.equal(calls,2);mark('append-recovered',info);
  await info.step('local skip after append recovery',step=>{mark('local-skip-enter',info,{stepId:step.id});step.skip('actual local skip after recovery');mark('MUST_NOT_RUN',info);});
  mark('after-local-skip',info);info.skip('actual test skip after recovery');mark('MUST_NOT_RUN',info);
 }}
]});
''',
},
'fixed-explicit-deadline': {
    'exit': 0,
    'body': r'''
export default defineNativeSuite({tests:[{title:'fixed native assertion deadline never starts an overdue retry',timeout:5000,run:async(_,info)=>{
 mark('body',info);let calls=0;const original=new Error('ACTUAL_FIXED_DEADLINE_FAILURE');let caught;
 try{await expectNative(()=>{++calls;mark('invocation',info,{label:'fixed'});throw original;}).toPass({timeout:120,intervals:[1000]});}catch(error){caught=error;}
 assert.strictEqual(caught,original);assert.equal(calls,1);mark('fixed-caught',info,{message:caught.message,sameObject:caught===original});
}}]});
''',
},
'raw-held-logical-timeout': {
    'exit': 0,
    'release': 'held-caught',
    'body': r'''
let active=false;
export default defineNativeSuite({hooks:{afterEach:[{run:(_,info)=>{assert.equal(active,false);mark('after-raw-fence',info);}}]},tests:[
 {title:'logical assertion rejection retains actual raw callback',timeout:5000,run:async(_,info)=>{
  mark('body',info,{label:'held'});let caught;
  try{await expectNative(async()=>{active=true;mark('raw-enter',info);try{while(!existsSync(RELEASE))await delay(5);mark('raw-settled',info);}finally{active=false;}}).toPass({timeout:120,intervals:[0]});}
  catch(error){caught=error;mark('held-caught',info,{active});}
  assert(caught instanceof Error);assert.equal(active,true);
 }},
 {title:'next real test starts after raw physical settlement',run:(_,info)=>{assert.equal(active,false);mark('body',info,{label:'after-held'});}}
]});
''',
},
'original-author-budget-default-and-zero': {
    'exit': 1,
    'body': r'''
const run=options=>async(_,info)=>{
 mark('body',info,{label:options?'explicit-zero':'default-zero'});
 try{await expectNative(async()=>{mark('raw-enter',info);await afterAbort(info.signal);await delay(25);mark('raw-settled',info);}).toPass(options);}
 catch(error){assert(error instanceof Error);mark('owner-caught',info);}
};
export default defineNativeSuite({hooks:{afterEach:[{run:(_,info)=>mark('after-raw-fence',info)}]},tests:[
 {title:'omitted assertion timeout keeps original author deadline',timeout:1000,run:run(undefined)},
 {title:'explicit zero keeps original author deadline',timeout:1000,run:run({timeout:0,intervals:[0]})}
]});
''',
},
'original-global-budget-and-cleanup': {
    'exit': 1,
    'options': ['--global-timeout', '1000'],
    'body': r'''
export default defineNativeSuite({hooks:{afterEach:[{run:async(_,info)=>{
 mark('after-raw-fence',info);await expectNative(()=>mark('invocation',info,{label:'cleanup'})).toPass({timeout:0,intervals:[0]});mark('cleanup-done',info);
}}]},tests:[{title:'zero assertion timeout cannot escape original global cancellation',timeout:0,expectedFailure:true,run:async(_,info)=>{
 mark('body',info);
 try{await expectNative(async()=>{mark('raw-enter',info);await afterAbort(info.signal);await delay(25);mark('raw-settled',info);}).toPass({timeout:0});}
 catch(error){assert(error instanceof Error);mark('owner-caught',info);}
}}]});
''',
},
'never-raw-actual-worker-retirement': {
    'exit': 1,
    'signal': 9,
    'body': r'''
export default defineNativeSuite({tests:[{title:'expected failure cannot hide missing raw callback retirement',timeout:5000,expectedFailure:true,run:async(_,info)=>{
 mark('body',info);try{await expectNative(()=>{mark('raw-enter',info);return new Promise(()=>{});}).toPass({timeout:120,intervals:[0]});}
 catch(error){assert(error instanceof Error);mark('never-logical-caught',info);}
}}]});
''',
},
'actual-host-death-is-infrastructure': {
    'exit': 1,
    'hostExit': 17,
    'body': r'''
export default defineNativeSuite({tests:[{title:'genuine host death cannot become assertion success',timeout:5000,expectedFailure:true,run:async(_,info)=>{
 mark('body',info);await expectNative(()=>{mark('actual-host-exit',info);process.exit(17);}).toPass({timeout:2000,intervals:[0]});
}}]});
''',
},
}
