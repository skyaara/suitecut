import {createServer} from 'node:http';
import {chromium} from '/workspace/suitecut/node_modules/playwright/index.mjs';
import fs from 'node:fs/promises';
import path from 'node:path';
const browser=await chromium.connectOverCDP('http://127.0.0.1:19523',{isLocal:true});
const context=browser.contexts()[0]; const page=context.pages()[0];
page.setDefaultTimeout(5000);
const results=[];
async function probe(name,fn){try{const result={name,ok:true,value:await fn()};results.push(result);console.log(JSON.stringify(result));}catch(e){const result={name,ok:false,error:String(e)};results.push(result);console.log(JSON.stringify(result));}}
await probe('bulk-handles',async()=>{await page.goto('data:text/html,hello');await page.setContent('<div><span>hi</span></div>'.repeat(2000));return (await page.$$('div')).length;});
await probe('promise-gc',async()=>{let done=false;const promise=page.evaluate(()=>new Promise(()=>{})).then(()=>({resolved:true}),e=>({error:String(e)}));for(let i=0;i<20;i++)await page.requestGC();return await Promise.race([promise,new Promise(r=>setTimeout(()=>r({timedOut:true}),2000))]);});
await page.goto('data:text/html,reset');
await probe('folder-upload',async()=>{const dir='/tmp/suitecut-deep-audit/cef-control-folder';await fs.mkdir(path.join(dir,'sub'),{recursive:true});await fs.writeFile(path.join(dir,'a.txt'),'a');await fs.writeFile(path.join(dir,'sub','b.txt'),'b');await page.setContent('<input type=file webkitdirectory>');await page.locator('input').setInputFiles(dir);return await Promise.race([page.locator('input').evaluate(e=>[...e.files].map(f=>f.webkitRelativePath)),new Promise(r=>setTimeout(()=>r({timedOut:true}),2000))]);});
console.log(JSON.stringify(results,null,2));
await browser.close();
