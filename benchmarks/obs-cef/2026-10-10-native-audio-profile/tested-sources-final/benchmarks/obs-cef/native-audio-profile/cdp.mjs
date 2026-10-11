import WebSocket, {WebSocketServer} from 'ws'
import {once} from 'node:events'

// OBS CEF 127 does not implement browser-context download management.
// Ignore that Playwright initialization command only; browser input/evaluation
// commands are forwarded unchanged to the real embedded CEF browser.
export async function connectObs(chromium, directory) {
  const version=await (await fetch('http://127.0.0.1:9222/json/version')).json()
  const server=new WebSocketServer({host:'127.0.0.1',port:0})
  await once(server,'listening')
  const connections=[]
  server.on('connection',async client=>{
    const upstream=new WebSocket(version.webSocketDebuggerUrl)
    connections.push(client,upstream)
    const pending=[]
    client.on('message',raw=>{
      const command=JSON.parse(raw.toString())
      if(command.method==='Browser.setDownloadBehavior') {
        client.send(JSON.stringify({id:command.id,result:{}}))
        return
      }
      if(upstream.readyState===WebSocket.OPEN)upstream.send(raw.toString())
      else pending.push(raw.toString())
    })
    upstream.on('open',()=>{for(const raw of pending)upstream.send(raw)})
    upstream.on('message',raw=>{if(client.readyState===WebSocket.OPEN)client.send(raw.toString())})
    upstream.on('error',()=>client.close())
    client.on('close',()=>upstream.close())
  })
  const browser=await chromium.connectOverCDP(`ws://127.0.0.1:${server.address().port}`)
  return {browser,version,close:async()=>{await browser.close();for(const c of connections)c.terminate();await new Promise(r=>server.close(r))}}
}
