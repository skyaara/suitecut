// SPDX-License-Identifier: MIT
#pragma once
// Runs only in the exact node's isolated renderer context. A native token owns
// one bounded RAF callback and one expiry timer; samples never await a Promise.
inline constexpr const char* kSuiteCutElementStateSampler=R"JS(function(element,observe,key,token,ttl,dispose=false){
  let sampleState=globalThis[key];
  if(!sampleState){
    if(dispose)return true;
    // Keep the physical registry in a closure. A user-supplied isolated-world
    // selector can see global property names, but cannot clear the private Map
    // and strand RAF/timer/node ownership outside the bounded registry.
    sampleState=(()=>{const store=new Map();return function(element,observe,token,ttl,dispose){
  const remove=()=>{const state=store.get(token);if(!state)return;store.delete(token);if(state.raf)cancelAnimationFrame(state.raf);clearTimeout(state.timer);state.node=null;};
  if(dispose){remove();return true;}
  const current=observe(element);let state=store.get(token);
  const equal=(a,b)=>a&&b&&a.every((value,index)=>value===b[index]);
  if(!state){
    if(store.size>=64)throw new Error('Native renderer element-state capacity exceeded');
    state={node:element,raf:0,timer:0,last:null,samples:0,stable:false};store.set(token,state);
    state.timer=setTimeout(remove,Math.max(1,Math.min(120000,ttl)));
  }
  if(state.node!==element)throw new Error('Native element-state token resolved to a different node');
  if(!current.visible){state.last=null;state.samples=0;state.stable=false;}
  else if(state.stable&&!equal(state.last,current.box)){state.last=null;state.samples=0;state.stable=false;}
  const sample=()=>{
    state.raf=0;if(store.get(token)!==state||!state.node)return;
    const value=observe(state.node);
    if(!value.visible){state.last=null;state.samples=0;state.stable=false;}
    else{const box=value.box;state.samples=equal(state.last,box)?state.samples+1:1;state.last=box;state.stable=state.samples>=2;}
    if(!state.stable)state.raf=requestAnimationFrame(sample);
  };
  if(!state.stable&&!state.raf)state.raf=requestAnimationFrame(sample);
  return {...current,stable:current.visible&&state.stable};
    };})();
    Object.defineProperty(globalThis,key,{value:sampleState,configurable:false,writable:false});
  }
  return sampleState(element,observe,token,ttl,dispose);

})JS";
