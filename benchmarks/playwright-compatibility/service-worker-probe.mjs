import { writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'

const engine = await launchEngine({ executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE, width: 640, height: 360, framesPerSecond: 30 })
const events = [], commands = []
engine.source.onProtocolEvent(event => events.push(event))
const server = createServer((req,res)=>{
  if(req.url==='/sw.js'){res.setHeader('Content-Type','application/javascript');res.end("self.marker=42;self.addEventListener('install',e=>e.waitUntil(self.skipWaiting()));self.addEventListener('activate',e=>e.waitUntil(self.clients.claim()))")}
  else res.end('<h1>Service worker probe</h1>')
})
try {
  await new Promise(r=>server.listen(0,'127.0.0.1',r))
  const context=await engine.browser.newContext(),page=await context.newPage()
  await page.goto(`http://127.0.0.1:${server.address().port}`)
  try {
    const result=await engine.source.sendDevToolsCommand('Target.attachToBrowserTarget');commands.push({method:'Target.attachToBrowserTarget',result})
    const attachment=events.find(e=>e.method==='Target.attachedToTarget' && e.params.targetInfo.url==='about:blank')
    const main=attachment.params.sessionId
    for(const [id,method,params] of [[1000000,'Target.setDiscoverTargets',{discover:true}],[1000001,'Target.setAutoAttach',{autoAttach:true,waitForDebuggerOnStart:false,flatten:true}]]) {
      await engine.source.command('cdp',{message:{id,method,params,sessionId:main+'::'+result.sessionId}})
    }
  } catch(error){commands.push({error:String(error)})}
  await page.evaluate(async()=>{await navigator.serviceWorker.register('/sw.js');await navigator.serviceWorker.ready})
  await delay(1500)
  const worker=events.find(e=>e.method==='Target.attachedToTarget' && e.params.targetInfo.type==='service_worker')
  if(worker)try{commands.push({method:'Target.attachToTarget from stable root',result:await engine.source.sendDevToolsCommand('Target.attachToTarget',{targetId:worker.params.targetInfo.targetId,flatten:true})})}catch(error){commands.push({method:'Target.attachToTarget from stable root',error:String(error)})}
  await context.close()
} finally {
  await engine.close();server.closeAllConnections();await new Promise(r=>server.close(r))
  await writeFile(process.env.COMPAT_DIRECTORY+'/service-worker-probe.json',JSON.stringify({commands,events},null,2))
}
