import {chromium} from '/workspace/suitecut/node_modules/playwright/index.mjs';
import {createServer} from 'node:http';
const stock=process.argv.includes('--stock');
const server=createServer((req,res)=>{res.setHeader('content-type','text/html');res.end(req.url==='/frame'?`<button onclick="this.textContent='clicked'">Click</button><script>window.events=[];for(const type of ['pointerdown','pointerup','mousedown','mouseup','click'])addEventListener(type,e=>events.push({type,x:e.clientX,y:e.clientY,target:e.target.tagName}));</script>`:'<h1>Main</h1>');});
await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;
const browser=stock?await chromium.launch({headless:true,args:['--site-per-process']}):await chromium.connectOverCDP('http://127.0.0.1:19523',{isLocal:true});
const page=stock?await browser.newPage():browser.contexts()[0].pages()[0];page.setDefaultTimeout(1500);
const session=await page.context().newCDPSession(page);const attachments=[];session.on('Target.attachedToTarget',e=>attachments.push(e));await session.send('Target.setAutoAttach',{autoAttach:true,waitForDebuggerOnStart:false,flatten:true});
const attempted=Number(process.env.SC_INPUT_ITERATIONS||30);let missed=0;
for(let i=0;i<attempted;i++){
 await page.goto(`http://127.0.0.1:${port}/main`);
 await page.evaluate(url=>{const f=document.createElement('iframe');f.src=url;document.body.append(f);},`http://localhost:${port}/frame`);
 let error='';try{await page.frameLocator('iframe').getByRole('button').click();}catch(e){error=String(e);}
 await new Promise(r=>setTimeout(r,50));
 const f=page.frames().find(f=>f.url().includes('/frame'));const value=f?await f.evaluate(()=>({text:document.querySelector('button').textContent,events:window.events})):{};
 if(value.text!=='clicked')missed++;
 console.log(JSON.stringify({backend:stock?'stock':'standalone-cef',iteration:i,error,...value,iframeAttachments:attachments.filter(e=>e.targetInfo.type==='iframe').length}));
}
console.log(JSON.stringify({backend:stock?'stock':'standalone-cef',attempted,missed}));
await browser.close();server.closeAllConnections();await new Promise(r=>server.close(r));
