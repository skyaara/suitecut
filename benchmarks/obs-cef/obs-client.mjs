import WebSocket from 'ws'

export class ObsClient {
  pending = new Map()
  sequence = 0
  async connect() {
    this.socket = new WebSocket('ws://127.0.0.1:4455')
    await new Promise((resolve, reject) => {
      this.socket.once('error', reject)
      this.socket.on('message', raw => {
        const {op, d} = JSON.parse(raw.toString())
        if (op === 0) this.socket.send(JSON.stringify({op:1,d:{rpcVersion:1}}))
        if (op === 2) resolve()
        if (op === 7) {
          const handler = this.pending.get(d.requestId)
          if (!handler) return
          clearTimeout(handler.timer)
          this.pending.delete(d.requestId)
          if(d.requestStatus.result) handler.resolve(d.responseData ?? {})
          else handler.reject(new Error(JSON.stringify(d.requestStatus)))
        }
      })
    })
  }
  request(requestType, requestData = {}) {
    const requestId = String(++this.sequence)
    return new Promise((resolve, reject) => {
      const timer=setTimeout(()=>{this.pending.delete(requestId);reject(new Error(`OBS ${requestType} timed out`))},60000)
      this.pending.set(requestId,{resolve,reject,timer})
      this.socket.send(JSON.stringify({op:6,d:{requestType,requestId,requestData}}))
    })
  }
  close() {this.socket?.close()}
}
