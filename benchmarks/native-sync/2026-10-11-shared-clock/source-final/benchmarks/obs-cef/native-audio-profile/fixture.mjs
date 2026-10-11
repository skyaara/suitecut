// Continuous deterministic low-bandwidth noise. Each sample is a pure function
// of AudioWorklet's frame index, so sequence loss can be measured without
// assuming every timestamp discontinuity means lost audible content.
export function audioFixture(html) {
  const processor=`
    function code(n) {
      let x=(n+0x9e3779b9)>>>0;
      x=Math.imul(x^(x>>>16),0x7feb352d);
      x=Math.imul(x^(x>>>15),0x846ca68b);
      x=(x^(x>>>16))>>>0;
      return (x/4294967296*2-1)*0.15;
    }
    class ProfileNoise extends AudioWorkletProcessor {
      first=true;
      process(inputs,outputs) {
        if(this.first){this.port.postMessage({firstFrame:currentFrame});this.first=false;}
        for(let i=0;i<outputs[0][0].length;i++) {
          const frame=currentFrame+i,n=Math.floor(frame/24),fraction=(frame%24)/24;
          const sample=code(n)*(1-fraction)+code(n+1)*fraction;
          for(const channel of outputs[0])channel[i]=sample;
        }
        return true;
      }
    }
    registerProcessor('profile-noise',ProfileNoise);
  `
  const script=`
    window.startAudioProfile=async()=>{
      const context=new AudioContext({sampleRate:48000});
      await context.resume();
      const url=URL.createObjectURL(new Blob([${JSON.stringify(processor)}],{type:'text/javascript'}));
      await context.audioWorklet.addModule(url);URL.revokeObjectURL(url);
      const node=new AudioWorkletNode(context,'profile-noise',{outputChannelCount:[2]});
      window.audioProfile={generator:'frame-coded interpolated hash noise',sampleRate:context.sampleRate,quantum:24,amplitude:0.15};
      const ready=new Promise(resolve=>{node.port.onmessage=event=>{Object.assign(window.audioProfile,event.data);resolve();};});
      node.connect(context.destination);await ready;
      const canvas=document.createElement('canvas');canvas.width=320;canvas.height=32;
      Object.assign(canvas.style,{position:'fixed',left:'480px',top:'0px',zIndex:1000});document.body.append(canvas);
      const draw=canvas.getContext('2d');
      function render(){
        const tick=Math.floor(context.currentTime*100);
        for(let bit=0;bit<20;bit++){draw.fillStyle=tick&(1<<bit)?'white':'black';draw.fillRect(bit*16,0,16,32);}
        window.audioProfile.currentFrame=Math.round(context.currentTime*48000);
        requestAnimationFrame(render);
      }
      render();return window.audioProfile;
    };
  `
  const addition=`<script>${script}</script>`
  return html.includes('</body>') ? html.replace('</body>',addition+'</body>') : html+addition
}
