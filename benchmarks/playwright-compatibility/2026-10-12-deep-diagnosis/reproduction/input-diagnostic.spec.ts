import { playwrightTest as test } from '../config/browserTest';
test('diagnose cross-site input without retries', async ({ browser, server }) => {
  server.setRoute('/input-frame',(_,res)=>{res.setHeader('content-type','text/html');res.end(`<button onclick="this.textContent='clicked'">Click</button><script>window.events=[];for(const type of ['pointerdown','pointerup','mousedown','mouseup','click'])addEventListener(type,e=>events.push({type,x:e.clientX,y:e.clientY,target:e.target.tagName}));</script>`);});
  let missed=0;
  for(let i=0;i<35;i++){
    const context=await browser.newContext();const page=await context.newPage();
    await page.goto(server.EMPTY_PAGE);
    await page.evaluate(()=>{(window as any).events=[];for(const type of ['pointerdown','pointerup','click'])addEventListener(type,e=>(window as any).events.push({type,x:e.clientX,y:e.clientY,target:(e.target as Element).tagName}));});
    const session=await context.newCDPSession(page);const attachments:any[]=[];session.on('Target.attachedToTarget',e=>attachments.push(e));
    await session.send('Target.setAutoAttach',{autoAttach:true,waitForDebuggerOnStart:false,flatten:true});
    await page.evaluate(url=>{const f=document.createElement('iframe');f.src=url;document.body.append(f);},server.CROSS_PROCESS_PREFIX+'/input-frame');
    await page.frameLocator('iframe').getByRole('button').click();
    await new Promise(r=>setTimeout(r,100));
    const frame=page.frames().find(f=>f.url().includes('/input-frame'))!;
    const value=await frame.evaluate(()=>({text:document.querySelector('button')!.textContent,events:(window as any).events}));
    if(value.text!=='clicked')missed++;
    console.log(JSON.stringify({iteration:i,...value,parent:await page.evaluate(()=>(window as any).events),iframeAttachments:attachments.filter(e=>e.targetInfo.type==='iframe').length}));
    await context.close();
  }
  console.log(JSON.stringify({attempted:35,missed}));
});
