import {createServer} from 'node:http'
import {readFile,writeFile} from 'node:fs/promises'
import {once} from 'node:events'
import {resolve,join} from 'node:path'
import {setTimeout as delay} from 'node:timers/promises'
import {chromium} from 'playwright'
import {ObsClient} from '../obs-client.mjs'
import {connectObs} from './cdp.mjs'
const directory=resolve(process.env.PROFILE_DIRECTORY)
const html=await readFile('benchmarks/obs-cef/fixture.html','utf8')
const server=createServer((_req,res)=>{res.setHeader('content-type','text/html');res.end(html)})
server.listen(0,'127.0.0.1');await once(server,'listening')
let obs,browser,connection
try {
 for(let i=0;i<80;i++) {
  obs=new ObsClient()
  try {await obs.connect();break} catch(error) {obs.close();obs=undefined;await delay(500)}
 }
 if(!obs)throw new Error('OBS WebSocket did not become ready')
 const version=await obs.request('GetVersion')
 const kinds=await obs.request('GetInputKindList')
 if(!kinds.inputKinds.includes('browser_source'))throw new Error('CEF browser_source is not loaded')
 await obs.request('SetVideoSettings',{baseWidth:1920,baseHeight:1080,outputWidth:1920,outputHeight:1080,fpsNumerator:30,fpsDenominator:1})
 const scene='CEF Audio Profile'
 const sourceName='Profile browser '+Date.now()
 const scenes=await obs.request('GetSceneList')
 if(!scenes.scenes.some(x=>x.sceneName===scene))await obs.request('CreateScene',{sceneName:scene})
 const inputs=await obs.request('GetInputList')
 for(const input of inputs.inputs)await obs.request('RemoveInput',{inputName:input.inputName})
 await obs.request('CreateInput',{sceneName:scene,inputName:sourceName,inputKind:'browser_source',inputSettings:{url:`http://127.0.0.1:${server.address().port}`,width:1920,height:1080,fps:30,fps_custom:true,reroute_audio:true,shutdown:false,css:'body { margin: 0; }'},sceneItemEnabled:true})
 await obs.request('SetCurrentProgramScene',{sceneName:scene})
 await delay(2000)
 connection=await connectObs(chromium,directory);browser=connection.browser
 const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().startsWith(`http://127.0.0.1:${server.address().port}`))
 if(!page)throw new Error('Did not find the OBS CEF fixture target')
 await page.evaluate(()=>window.activate('canvas'))
 await page.mouse.wheel(0,180)
 await obs.request('SaveSourceScreenshot',{sourceName,imageFormat:'png',imageFilePath:join(directory,'browser-source.png')})
 const state=await page.evaluate(()=>window.snapshot())
 await writeFile(join(directory,'probe.json'),JSON.stringify({version,kinds,state,browserVersion:browser.version(),url:page.url(),embeddedCEF:true},null,2))
 console.log('Verified OBS embedded CEF and Playwright wheel input',browser.version(),state)
} finally {
 // connectOverCDP's public close disconnects its transport from OBS's browser.
 await connection?.close();obs?.close();server.closeAllConnections();server.close()
}
