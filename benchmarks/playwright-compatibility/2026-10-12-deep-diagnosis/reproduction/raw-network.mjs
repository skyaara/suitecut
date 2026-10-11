import {createServer} from 'node:http';
import fs from 'node:fs/promises';
import WebSocket from '/workspace/suitecut/node_modules/ws/wrapper.mjs';
import {chromium} from '/workspace/suitecut/node_modules/playwright/index.mjs';
const stock=process.argv.includes('--stock');let browser;
let send,onEvent,close;
if(stock){browser=await chromium.launch({headless:true,args:['--disable-gpu']});const page=await browser.newPage();const session=await page.context().newCDPSession(page);send=(m,p={})=>session.send(m,p);onEvent=(m,f)=>session.on(m,f);close=()=>browser.close();}
else {const targets=await (await fetch('http://127.0.0.1:19523/json/list')).json();const target=targets.find(t=>t.type==='page');const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((r,j)=>{ws.once('open',r);ws.once('error',j)});let id=0;const pending=new Map();const handlers=new Map();send=(method,params={})=>new Promise((resolve,reject)=>{const n=++id;const timer=setTimeout(()=>{pending.delete(n);reject(new Error('CDP timeout '+method))},10000);pending.set(n,{resolve,reject,timer});ws.send(JSON.stringify({id:n,method,params}));});onEvent=(m,f)=>handlers.set(m,f);ws.on('message',data=>{const o=JSON.parse(data);if(o.id){const p=pending.get(o.id);if(p){clearTimeout(p.timer);pending.delete(o.id);o.error?p.reject(new Error(o.error.message)):p.resolve(o.result)}}else handlers.get(o.method)?.(o.params)});close=()=>{ws.close();};}
let resolveRequest;const got=new Promise(r=>resolveRequest=r);const events=[];const server=createServer((req,res)=>{events.push({url:req.url,method:req.method,headers:req.headers});if(req.url==='/redirect'){res.writeHead(302,{location:'/target'});res.end()}else if(req.url==='/target'){resolveRequest(events.at(-1));res.end('ok')}else{res.setHeader('Content-Type','text/html');res.end('<h1>empty</h1>')}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));const origin='http://127.0.0.1:'+server.address().port;
try{
 await send('Page.enable');await send('Runtime.enable');await send('Network.enable');
 await send('Page.navigate',{url:origin+'/empty'});
 for(let i=0;i<50;i++){const r=await send('Runtime.evaluate',{expression:'location.href',returnByValue:true});if(r.result.value===origin+'/empty')break;await new Promise(r=>setTimeout(r,20));}
 let override;const pauses=[];
 onEvent('Fetch.requestPaused',e=>{pauses.push({url:e.request.url,method:e.request.method,headers:e.request.headers});if(e.request.url.endsWith('/redirect'))override={...e.request.headers,custom:'value'};const headers=override?Object.entries(override).filter(([n])=>!['host','content-length','connection'].includes(n.toLowerCase())&&!n.toLowerCase().startsWith('sec-')).map(([name,value])=>({name,value})):undefined;void send('Fetch.continueRequest',{requestId:e.requestId,...headers?{headers}:{}}).catch(e=>console.error(String(e)));});
 await send('Fetch.enable',{patterns:[{urlPattern:'*',requestStage:'Request'}]});
 const evaluation=send('Runtime.evaluate',{expression:`fetch(${JSON.stringify(origin+'/redirect')},{method:'POST',body:'foo'}).then(r=>r.text())`,awaitPromise:true,returnByValue:true});void evaluation.catch(()=>{});
 const result=await Promise.race([got,new Promise(r=>setTimeout(()=>r({timedOut:true}),5000))]);console.log(JSON.stringify({backend:stock?'stock':'standalone-cef',result,events,pauses},null,2));
 await send('Fetch.disable');
}finally{await close();server.closeAllConnections();await new Promise(r=>server.close(r));}
