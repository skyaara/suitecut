/** Bundled extension sources, written into an isolated temporary profile at launch. */
export const tabAudioWorker = `
chrome.runtime.onMessage.addListener((message, sender, reply) => { if(message.type === 'heartbeat') reply({}); });
chrome.action.onClicked.addListener(async tab => {
  const config = await fetch(__SUITECUT_AUDIO_URL__ + '/config').then(response => response.json());
  try {
    if (!await chrome.offscreen.hasDocument()) {
      await chrome.offscreen.createDocument({url:'offscreen.html', reasons:['USER_MEDIA'], justification:'Capture SuiteCut selected tab audio'});
    }
    await chrome.runtime.sendMessage({type:'stop'});
    const id = await chrome.tabCapture.getMediaStreamId({targetTabId:tab.id});
    const result = await chrome.runtime.sendMessage({type:'start', id, config});
    if (result?.error) throw new Error(result.error);
    await fetch(config.url + '/status/' + config.generation + '/ok', {method:'POST'});
  } catch (error) {
    await fetch(config.url + '/status/' + config.generation + '/error', {method:'POST'});
  }
});
`

export const tabAudioOffscreen = `
let context, media, node, socket, reconnectTimer, watchdog;
// An active capture owns this worker connection, including prolonged silence.
setInterval(() => chrome.runtime.sendMessage({type:'heartbeat'}).catch(() => {}), 10000);
async function stop() {
  clearTimeout(reconnectTimer);
  clearInterval(watchdog);
  if (socket) { socket.onclose = null; socket.close(); socket = undefined; }
  if (node) { node.port.onmessage = null; node.port.close(); node.disconnect(); }
  media?.getTracks().forEach(track => track.stop());
  if (context) await context.close();
  context = media = node = undefined;
}
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message.type !== 'start' && message.type !== 'stop') return;
  (async () => {
    await stop();
    if (message.type === 'stop') return {};
    const config = message.config;
    media = await navigator.mediaDevices.getUserMedia({audio:{mandatory:{chromeMediaSource:'tab',chromeMediaSourceId:message.id}},video:false});
    context = new AudioContext({sampleRate:48000});
    await context.audioWorklet.addModule('pcm.js');
    node = new AudioWorkletNode(context, 'suitecut-pcm', {numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[2]});
    const source = context.createMediaStreamSource(media);
    source.connect(node);
    node.connect(context.destination);
    const capturingContext = context;
    const capturingNode = node;
    let pending = 0, lastAck = performance.now();
    function connect() {
      if (context !== capturingContext) return;
      const current = new WebSocket(config.url.replace('http:', 'ws:') + '/pcm/' + config.generation);
      socket = current;
      current.binaryType = 'arraybuffer';
      current.onopen = () => { pending = 0; lastAck = performance.now(); };
      current.onmessage = () => {
        if (socket !== current) return;
        pending = Math.max(0, pending - 1); lastAck = performance.now();
      };
      current.onerror = () => current.close();
      current.onclose = () => {
        if (socket !== current || context !== capturingContext) return;
        reconnectTimer = setTimeout(connect, 250);
      };
    }
    connect();
    watchdog = setInterval(() => {
      if (pending && performance.now() - lastAck > 2000) socket?.close();
    }, 250);
    node.port.onmessage = ({data}) => {
      // Return worklet credit even when the network is stalled. Old sound is dropped.
      capturingNode.port.postMessage('credit');
      if (context !== capturingContext || socket?.readyState !== WebSocket.OPEN ||
          pending >= 4 || socket.bufferedAmount > 15392) return;
      // Drop MessagePort backlog after a blocked offscreen thread.
      if (capturingContext.currentTime - data.time > 0.1) return;
      const timestamp = performance.timeOrigin + performance.now() + (data.time - capturingContext.currentTime) * 1000;
      const packet = new ArrayBuffer(3848);
      new DataView(packet).setFloat64(0, timestamp, true);
      new Uint8Array(packet, 8).set(new Uint8Array(data.pcm));
      socket.send(packet);
      pending++;
    };
    media.getAudioTracks()[0].onended = () => {
      fetch(config.url + '/ended/' + config.generation, {method:'POST'}).catch(() => {});
    };
    await context.resume();
    return {};
  })().then(reply, error => reply({error:String(error)}));
  return true;
});
`

export const tabAudioWorklet = `
class PCM extends AudioWorkletProcessor {
  constructor() { super(); this.pcm = new Int16Array(1920); this.offset = 0; this.start = 0; this.credits = 4; this.port.onmessage = () => { this.credits = Math.min(4, this.credits + 1); }; }
  process(inputs) {
    const channels = inputs[0];
    for (let i = 0; i < 128; i++) {
      if (!this.offset) this.start = (currentFrame + i) / sampleRate;
      for (let channel = 0; channel < 2; channel++) {
        const value = channels[channel]?.[i] ?? channels[0]?.[i] ?? 0;
        this.pcm[this.offset++] = Math.round(Math.max(-1, Math.min(1, value)) * 32767);
      }
      if (this.offset === this.pcm.length) {
        if (this.credits > 0) {
          this.credits--;
          this.port.postMessage({time:this.start,pcm:this.pcm.buffer}, [this.pcm.buffer]);
          this.pcm = new Int16Array(1920);
        }
        this.offset = 0;
      }
    }
    return true;
  }
}
registerProcessor('suitecut-pcm', PCM);
`
