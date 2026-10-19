// SPDX-License-Identifier: MIT
#pragma once

// C++ owns this isolated-world implementation. The SDK sends tagged data, never
// executable event constructors or event-listener code. Handles are real nodes
// adopted by the native controller into this exact utility context.
inline constexpr char kSuiteCutDispatchEvent[] = R"JS(function(element,packet,handles){
  let visited=0;const references=new Map();
  const fail=()=>{throw new Error('Invalid native dispatchEvent argument');};
  const text=value=>{
    if(typeof value==='string')return value;
    if(!Array.isArray(value)||value.length!==2||value[0]!=='utf16'||
       typeof value[1]!=='string'||value[1].length>65536||value[1].length%4||!/^[0-9a-f]*$/.test(value[1]))return fail();
    let out='';for(let i=0;i<value[1].length;i+=4)out+=String.fromCharCode(parseInt(value[1].slice(i,i+4),16));return out;
  };
  const decode=(node,depth=0)=>{
    if(depth>100||++visited>10000||!Array.isArray(node))return fail();
    switch(node[0]){
      case 'undefined':return undefined;
      case 'null':return null;
      case 'primitive':if(typeof node[1]==='string'||typeof node[1]==='boolean'||(typeof node[1]==='number'&&Number.isFinite(node[1])))return node[1];return fail();
      case 'utf16':return text(node);
      case 'number':if(node[1]==='NaN')return NaN;if(node[1]==='-0')return -0;if(node[1]==='Infinity')return Infinity;if(node[1]==='-Infinity')return -Infinity;return fail();
      case 'bigint':return BigInt(text(node[1]));
      case 'date':return new Date(text(node[1]));
      case 'regexp':return new RegExp(text(node[1]),text(node[2]));
      case 'url':return new URL(text(node[1]));
      case 'error':{const error=new Error(text(node[2]));error.name=text(node[1]);error.stack=text(node[3]);return error;}
      case 'handle':if(Number.isInteger(node[1])&&node[1]>=0&&node[1]<handles.length)return handles[node[1]];return fail();
      case 'reference':if(references.has(node[1]))return references.get(node[1]);return fail();
      case 'empty':return {};
      case 'bytes':{
        const constructors={Int8Array,Uint8Array,Uint8ClampedArray,Int16Array,Uint16Array,Int32Array,Uint32Array,Float32Array,Float64Array,BigInt64Array,BigUint64Array,DataView};
        const name=text(node[1]);if(!Array.isArray(node[2])||node[2].length>16384||node[2].some(x=>!Number.isInteger(x)||x<0||x>255))return fail();
        const bytes=new Uint8Array(node[2]);if(name==='ArrayBuffer')return bytes.buffer;
        if(!Object.hasOwn(constructors,name))return fail();return new constructors[name](bytes.buffer);
      }
      case 'array':case 'object':{
        const id=node[1],fields=node[2];if(!Number.isInteger(id)||id<1||references.has(id)||!Array.isArray(fields))return fail();
        const out=node[0]==='array'?[]:{};references.set(id,out);
        for(let i=0;i<fields.length;++i){const field=fields[i];let key,value;
          if(node[0]==='array'){key=i;value=field;}else{if(!Array.isArray(field)||field.length!==2)return fail();key=text(field[0]);value=field[1];}
          Object.defineProperty(out,key,{value:decode(value,depth+1),enumerable:true,writable:true,configurable:true});
        }return out;
      }
      default:return fail();
    }
  };
  const data=decode(packet),type=data?.type,init=data?.eventInit;
  if(typeof type!=='string'||!type.length||type.length>256||!init||typeof init!=='object'||Array.isArray(init))return fail();
  if(!element||!element.isConnected)throw new Error('Element is not attached to the DOM');
  if(!Number.isInteger(element.nodeType)||element.nodeType<1||element.nodeType>12)throw new Error('Target is not a DOM node');
  const options={bubbles:true,cancelable:true,composed:true,...init};
  const groups=[
    [MouseEvent,/^(?:auxclick|click|dblclick|mousedown|mouseup|mousemove|mouseenter|mouseleave|mouseover|mouseout|contextmenu)$/],
    [PointerEvent,/^(?:pointerdown|pointerup|pointermove|pointerover|pointerout|pointerenter|pointerleave|pointercancel|gotpointercapture|lostpointercapture)$/],
    [KeyboardEvent,/^(?:keydown|keyup|keypress)$/],
    [FocusEvent,/^(?:focus|blur|focusin|focusout)$/],
    [InputEvent,/^(?:input|beforeinput)$/],
    [WheelEvent,/^wheel$/],
    [DragEvent,/^(?:drag|dragstart|dragend|dragenter|dragleave|dragover|drop)$/],
    [TouchEvent,/^(?:touchstart|touchend|touchmove|touchcancel)$/],
    [CompositionEvent,/^(?:compositionstart|compositionupdate|compositionend)$/],
    [ClipboardEvent,/^(?:copy|cut|paste)$/],
    [SubmitEvent,/^submit$/],
  ];
  let Constructor=Object.hasOwn(init,'detail')?CustomEvent:Event;
  for(const [candidate,pattern]of groups)if(pattern.test(type)){Constructor=candidate;break;}
  if(type==='deviceorientation'||type==='deviceorientationabsolute'){
    if(typeof DeviceOrientationEvent!=='function')throw new Error('DeviceOrientationEvent is unavailable in this browser context');
    Constructor=DeviceOrientationEvent;
  }
  if(type==='devicemotion'){
    if(typeof DeviceMotionEvent!=='function')throw new Error('DeviceMotionEvent is unavailable in this browser context');
    Constructor=DeviceMotionEvent;
  }
  const event=new Constructor(type,options);
  // dispatchEvent's native boolean records actual preventDefault(), while the
  // public method intentionally returns void. Listener exceptions retain the
  // browser's normal pageerror behavior; the helper never swallows them.
  return {ready:true,dispatched:EventTarget.prototype.dispatchEvent.call(element,event)};
})JS";
