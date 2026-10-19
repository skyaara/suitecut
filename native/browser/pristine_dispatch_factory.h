// SPDX-License-Identifier: MIT
#pragma once
// Evaluated once in the actual pristine main context; the result is retained
// only by native Inspector object group, never on a public global.
inline constexpr char kSuiteCutPristineDispatchFactory[]=R"SCFACTORY((()=>{
  'use strict';
  const apply=globalThis.Reflect.apply,nativeDispatch=globalThis.EventTarget.prototype.dispatchEvent;
  const descriptors=globalThis.Object.getOwnPropertyDescriptor,prototype=globalThis.Object.getPrototypeOf;
  const connected=descriptors(Node.prototype,'isConnected').get,nodeType=descriptors(Node.prototype,'nodeType').get,ownerDocument=descriptors(Node.prototype,'ownerDocument').get;
  const actualDocument=globalThis.document;
  const typedBuffer=descriptors(prototype(globalThis.Uint8Array.prototype),'buffer').get;
  const nativeArray=globalThis.Array,nativeMap=globalThis.Map,mapHas=globalThis.Map.prototype.has,mapGet=globalThis.Map.prototype.get,mapSet=globalThis.Map.prototype.set;
  const nativeSome=globalThis.Array.prototype.some,nativeSlice=globalThis.String.prototype.slice,nativeExec=globalThis.RegExp.prototype.exec;
  const arraySome=(value,predicate)=>apply(nativeSome,value,[predicate]);
  const stringSlice=(value,start,end)=>apply(nativeSlice,value,[start,end]);
  const regexTest=(expression,value)=>apply(nativeExec,expression,[value])!==null;
  const Array={isArray:nativeArray.isArray},Number={isInteger:globalThis.Number.isInteger,isFinite:globalThis.Number.isFinite};
  const Object={hasOwn:globalThis.Object.hasOwn,defineProperty:globalThis.Object.defineProperty};
  const String={fromCharCode:globalThis.String.fromCharCode},parseInt=globalThis.parseInt;
  const Map=function(){const value=new nativeMap();return {has:key=>apply(mapHas,value,[key]),get:key=>apply(mapGet,value,[key]),set:(key,item)=>apply(mapSet,value,[key,item])}};
  const {Error,BigInt,Date,RegExp,URL,Int8Array,Uint8Array,Uint8ClampedArray,Int16Array,Uint16Array,Int32Array,Uint32Array,Float32Array,Float64Array,BigInt64Array,BigUint64Array,DataView,Event,CustomEvent,MouseEvent,PointerEvent,KeyboardEvent,FocusEvent,InputEvent,WheelEvent,DragEvent,TouchEvent,CompositionEvent,ClipboardEvent,SubmitEvent,DeviceOrientationEvent,DeviceMotionEvent}=globalThis;
  // This private immutable bundle is passed only to the strict native query
  // callback. No property is installed on an author global or prototype.
  const freeze=globalThis.Object.freeze,regexPrototype=globalThis.RegExp.prototype;
  const regexReplaceMethod=regexPrototype[globalThis.Symbol.replace],regexSplitMethod=regexPrototype[globalThis.Symbol.split];
  const regexSpecies=globalThis.Symbol.species,regexSource=descriptors(regexPrototype,'source').get;
  const regexGetters=[];
  for(const key of ['dotAll','global','hasIndices','ignoreCase','multiline','source','sticky','unicode','unicodeSets','flags']){
    const descriptor=descriptors(regexPrototype,key);if(descriptor&&descriptor.get)regexGetters[regexGetters.length]=[key,descriptor.get];
  }
  const regexConstructor=freeze({[regexSpecies]:function(pattern,flags){return ownRegex(new RegExp(apply(regexSource,pattern,[]),flags));}});
  function ownRegex(expression){
    // Only native query literals/constructed regexes enter here. Brand-check
    // before defining own methods; no authored regex/prototype is rewritten.
    apply(regexSource,expression,[]);
    Object.defineProperty(expression,'exec',{value:nativeExec});
    Object.defineProperty(expression,'constructor',{value:regexConstructor});
    for(let i=0;i<regexGetters.length;++i){const entry=regexGetters[i];Object.defineProperty(expression,entry[0],{get:entry[1]});}
    return expression;
  }
  const queryIntrinsics=freeze({
    regexExec:(expression,value)=>apply(nativeExec,expression,[value]),
    regexReplace:(value,expression,replacement)=>apply(regexReplaceMethod,ownRegex(expression),[value,replacement]),
    regexSplit:(value,expression)=>apply(regexSplitMethod,ownRegex(expression),[value]),
    regexCreate:(pattern,flags)=>new RegExp(pattern,flags),
    nodeType:node=>apply(nodeType,node,[]),
    ownerDocument:node=>apply(ownerDocument,node,[]),
    validateCustomNode(node,root){
      if(!node||apply(nodeType,node,[])!==1)throw new Error('Native selector returned an element outside its frame');
      const rootDocument=apply(nodeType,root,[])===9?root:apply(ownerDocument,root,[]);
      if(rootDocument!==actualDocument||apply(ownerDocument,node,[])!==actualDocument)throw new Error('Native main dispatch target belongs to another document');
    },
  });
  const dispatch=function(element,packet,handles){
  let visited=0;const references=new Map();
  const fail=()=>{throw new Error('Invalid native dispatchEvent argument');};
  const text=value=>{
    if(typeof value==='string')return value;
    if(!Array.isArray(value)||value.length!==2||value[0]!=='utf16'||
       typeof value[1]!=='string'||value[1].length>65536||value[1].length%4||!regexTest(/^[0-9a-f]*$/,value[1]))return fail();
    let out='';for(let i=0;i<value[1].length;i+=4)out+=String.fromCharCode(parseInt(stringSlice(value[1],i,i+4),16));return out;
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
        const name=text(node[1]);if(!Array.isArray(node[2])||node[2].length>16384||arraySome(node[2],x=>!Number.isInteger(x)||x<0||x>255))return fail();
        const bytes=new Uint8Array(node[2].length);for(let i=0;i<node[2].length;++i)bytes[i]=node[2][i];const buffer=apply(typedBuffer,bytes,[]);if(name==='ArrayBuffer')return buffer;
        if(!Object.hasOwn(constructors,name))return fail();return new constructors[name](buffer);
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
  if(!element)return {ready:false,detached:true};
  const actualType=apply(nodeType,element,[]);if(!Number.isInteger(actualType)||actualType<1||actualType>12)throw new Error('Target is not a DOM node');
  if(actualType===9?element!==actualDocument:apply(ownerDocument,element,[])!==actualDocument)throw new Error('Native main dispatch target belongs to another document');
  if(!apply(connected,element,[]))return {ready:false,detached:true};
  const options={__proto__:null,bubbles:true,cancelable:true,composed:true,...init};
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
  for(let i=0;i<groups.length;++i){const pair=groups[i];if(regexTest(pair[1],type)){Constructor=pair[0];break;}}
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
  return {ready:true,dispatched:apply(nativeDispatch,element,[event])};
};
  return function(mode,element,packet,handles,query,strict,wrappedMask=0){
    if(!Array.isArray(handles)||handles.length>32||!Number.isInteger(wrappedMask)||wrappedMask<0||wrappedMask>4294967295||(handles.length<32&&(wrappedMask>>>handles.length)!==0))throw new Error('Invalid native main dispatch handles');
    if(wrappedMask){const actual=[];for(let i=0;i<handles.length;++i){let value=handles[i];if((wrappedMask>>>i)&1){const field=descriptors(value,'value');if(!field||!Object.hasOwn(field,'value'))throw new Error('Native opaque handle wrapper is unavailable');value=field.value;}Object.defineProperty(actual,i,{value,enumerable:true,configurable:true,writable:true});}handles=actual;}
    if(mode==='dispatch')return dispatch(element,packet,handles);
    if(mode!=='select'||typeof query!=='function')throw new Error('Invalid native main dispatch operation');
    const nodes=apply(query,undefined,[queryIntrinsics]);
    if(!Array.isArray(nodes)||nodes.length>10000)throw new Error('Invalid native main selector result');
    if(nodes.length>1&&strict)return {strictNodes:nodes};
    if(!nodes.length)return {ready:false};
    return dispatch(nodes[0],packet,handles);
  };
})())SCFACTORY";
