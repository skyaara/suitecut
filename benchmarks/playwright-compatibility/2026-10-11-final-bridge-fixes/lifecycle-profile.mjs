import {launchEngine} from '../../../dist/engine.js';
import {spawn,execFileSync} from 'node:child_process';
import {writeFile,appendFile} from 'node:fs/promises';
import {setTimeout as delay} from 'node:timers/promises';
const out=process.env.MEMORY_DIRECTORY;
let rng=0x5eeda11,iteration=0;
const random=()=>{rng^=rng<<13;rng^=rng>>>17;rng^=rng<<5;return rng>>>0};
const engine=await launchEngine({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width:640,height:360,framesPerSecond:30});
const sampler=spawn('python3',['benchmarks/publication/sample.py',`${out}/processes.jsonl`,String(process.pid)],{env:{...process.env,BENCH_PROFILE:'memory'},stdio:'ignore'});
const session=await engine.browser.newBrowserCDPSession();
const baselineTargets=(await session.send('Target.getTargets')).targetInfos.map(t=>t.targetId).sort();
const result={seed:0x5eeda11,cycles:[],snapshots:[],status:'running',startedAt:new Date().toISOString()};
async function snapshot(label){global.gc?.();await delay(1200);const processes=JSON.parse(execFileSync('python3',['-c',`import psutil,json,sys
p=psutil.Process(int(sys.argv[1]));rows=[]
for c in [p,*p.children(recursive=True)]:
 try:
  if c.pid==psutil.Process().pid or c.status()=='zombie' or 'benchmarks/publication/sample.py' in ' '.join(c.cmdline()):continue
  m=c.memory_full_info();rows.append({'pid':c.pid,'name':c.name(),'rss':m.rss,'pss':m.pss,'uss':m.uss,'fds':c.num_fds(),'threads':c.num_threads(),'args':c.cmdline() if '--type=renderer' in c.cmdline() else []})
 except psutil.Error:pass
print(json.dumps(rows))`,String(process.pid)],{encoding:'utf8'}));result.snapshots.push({label,atMs:performance.now(),node:process.memoryUsage(),processes});}
try {
 for(let i=0;i<140;i++) {
  const start=performance.now(),choice=random()%3;
  const context=await engine.browser.newContext();
  try {
   await context.addCookies([{name:'cycle',value:String(i),domain:'example.com',path:'/',secure:true,sameSite:'None',partitionKey:'https://top.example'}]);
   if(choice){const page=await context.newPage();await page.setContent('<style>body{margin:0}</style><div style="width:700px;height:500px;border:1px solid blue"></div>');if(choice===2)await page.locator('div').screenshot();}
   const cookies=await context.cookies();if(cookies.length!==1||cookies[0].value!==String(i))throw Error('Cookie context leaked');
   await context.clearCookies();if((await context.cookies()).length)throw Error('Cookies survived clear');
  } finally {await context.close()}
  result.cycles.push({i,choice,durationMs:performance.now()-start});
  const targets=(await session.send('Target.getTargets')).targetInfos.map(t=>t.targetId).sort();if(JSON.stringify(targets)!==JSON.stringify(baselineTargets))throw Error('Target registrations leaked');
  if([19,59,99,139].includes(i))await snapshot(`after-${i+1}`);
 }
 const unclosed=await engine.browser.newContext();await unclosed.addCookies([{name:'close',value:'active-agent',url:'https://example.com'}]);
 await engine.close();if(engine.source.state!=='closed')throw Error('Native process did not close');
 await snapshot('after-engine-close');result.status='passed';
} catch(e){result.status='failed';result.error=e.stack;process.exitCode=1}
finally {await engine.close();sampler.kill('SIGTERM');await new Promise(r=>sampler.once('exit',r));result.finishedAt=new Date().toISOString();await writeFile(`${out}/result.json`,JSON.stringify(result,null,2));console.log(JSON.stringify({status:result.status,cycles:result.cycles.length,error:result.error,snapshots:result.snapshots.map(s=>({label:s.label,processes:s.processes.length,pss:s.processes.reduce((t,p)=>t+p.pss,0),uss:s.processes.reduce((t,p)=>t+p.uss,0)}))},null,2))}
