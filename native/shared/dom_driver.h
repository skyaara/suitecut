// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include "dom_query.h"
#include "dom_semantic_snapshot.h"
#include "../browser/native_aria_json.h"
#include "../browser/native_aria_formatter.h"

inline constexpr char kSuiteCutDOMDriverStateKey[] = "__suitecutNativeChromeDOM1";
// Protocol-independent authored DOM helpers; install only in a private realm.
inline std::string SuiteCutDOMDriverSource() {
  return std::string("(()=>{const query=(")+kSuiteCutDomQuery+");const snapshot=("+kSuiteCutDOMSemanticSnapshot+");const jsonFormat=("+kSuiteCutNativeAriaJSON+");const yamlFormat=("+kSuiteCutNativeAriaFormatter+R"JS();
  let owner=document, generation=0, refs=new Map(), identities=new WeakMap(), nextIdentity=0, dragSource=null;
  const nonce=()=>Array.from(crypto.getRandomValues(new Uint8Array(16)),n=>n.toString(16).padStart(2,'0')).join('');
  let prefix=nonce();
  const reset=()=>{if(owner!==document){owner=document;generation=0;refs.clear();identities=new WeakMap();nextIdentity=0;prefix=nonce();}};
  const parent=e=>e.assignedSlot||e.parentElement||e.getRootNode().host||null;
  const reference=(root,token)=>{
    reset();if(typeof token!=='string'||!/^r[A-Za-z0-9]{1,64}_[1-9][0-9]*_(?:0|[1-9][0-9]*)$/.test(token))throw new Error('Invalid native snapshot reference token');
    const e=refs.get(token)?.deref();if(!e||!e.isConnected||e.ownerDocument!==document)throw new Error('Native snapshot reference is stale or unavailable');
    if(root.nodeType!==9){let contained=false;for(let n=e;n;n=parent(n))if(n===root){contained=true;break;}if(!contained)return [];}
    return [e];
  };
  const referenceFactory='function(){return {query:(root,token)=>globalThis.__suitecutNativeChromeDOM1.reference(root,token)[0]||null,queryAll:(root,token)=>globalThis.__suitecutNativeChromeDOM1.reference(root,token)}}';
  const options={registryKey:'__suitecutNativeChromeRefEngines1',engines:{__suitecut_reference:{source:referenceFactory}}};
  const translate=(steps,depth=0)=>{if(depth>64)throw new Error('Native reference query nesting limit exceeded');return steps.map(raw=>{
    const step={...raw};if(step.engine==='reference')return {engine:'custom',name:'__suitecut_reference',selector:step.ref};
    for(const key of ['query','has','hasNot','descendants'])if(Array.isArray(step[key]))step[key]=translate(step[key],depth+1);return step;
  });};
  const guardOrigin=request=>{
    if(request.expectedOrigin===undefined)return;
    if(typeof request.expectedOrigin!=='string'||!request.expectedOrigin.length)throw new Error('Invalid native guarded action origin');
    if(location.origin!==request.expectedOrigin)throw new Error('Native guarded action origin mismatch');
  };
  const resolve=request=>{reset();guardOrigin(request);const raw=request.query||[{engine:'css',selector:request.selector||'body, frameset'}];const plan=query(raw,document,{planOnly:true});return query(translate(plan),document,options);};
  const one=request=>{const list=resolve(request);if(request.strict!==false&&list.length>1)throw new Error('Strict locator matched multiple elements');return list[0]||null;};
  const id=e=>{if(!identities.has(e))identities.set(e,++nextIdentity);return identities.get(e);};
  const checked=e=>{if('checked' in e)return e.checked;const v=e.getAttribute('aria-checked');if(v==='true')return true;if(v==='false')return false;throw new Error('Element is not a checkbox or radio button');};
  const editable=e=>{if(e.localName==='label'&&e.control)return e.control;return e;};
  const guardedFill=(e,value,request)=>{
    if(!(e instanceof HTMLInputElement||e instanceof HTMLTextAreaElement))throw new Error('Native guarded fill requires an input or textarea');
    if(typeof value!=='string'||value.length>65536)throw new Error('Invalid or oversized native guarded fill value');
    guardOrigin(request);
    const prototype=e instanceof HTMLInputElement?HTMLInputElement.prototype:HTMLTextAreaElement.prototype;
    const setter=Object.getOwnPropertyDescriptor(prototype,'value')?.set;
    if(typeof setter!=='function')throw new Error('Native guarded fill value setter is unavailable');
    setter.call(e,value);
    e.dispatchEvent(new Event('input',{bubbles:true,composed:true}));e.dispatchEvent(new Event('change',{bubbles:true}));
    return {ready:true,done:true};
  };
  const inspect=request=>{
    let e=one(request);if(!e)return {ready:false,reason:'element to be attached'};
    const action=request.action||'', force=request.force===true;
    if(['fill','clear','setChecked'].includes(action))e=editable(e);
    if(!e.isConnected)return {ready:false,reason:'element to be attached'};
    if(action==='inputValue'){if(!('value' in e))throw new Error('Element is not an input, textarea or select');return {ready:true,value:e.value};}
    if(action==='focus'||action==='press'){e.focus();return {ready:true,identity:id(e)};}
    if(action==='setChecked'&&checked(e)===request.checked)return {ready:true,done:true};
    let state=query([],e,{elementStateOnly:true});
    if(!state.visible&&['click','tap','setChecked'].includes(action)&&e instanceof HTMLInputElement&&e.labels?.length){const inputState=state;e=e.labels[0];state=query([],e,{elementStateOnly:true});state.enabled=inputState.enabled;}
    if(!state.visible)return {ready:false,reason:'element to be visible'};
    if(!force&&action!=='hover'&&!state.enabled)return {ready:false,reason:'element to be enabled'};
    if(['fill','clear','type'].includes(action)){
      if(e.readOnly||e.getAttribute('aria-readonly')==='true')return {ready:false,reason:'element to be editable'};
      if(!(e instanceof HTMLInputElement||e instanceof HTMLTextAreaElement||e.isContentEditable))throw new Error('Element is not editable');
      if(e instanceof HTMLInputElement&&!['text','search','url','tel','email','password','number','date','time','datetime-local','month','range','week','color'].includes(e.type))throw new Error('Input type cannot be filled');
      if(action==='fill'||action==='clear'){
        const value=action==='clear'?'':request.value;
        if(typeof value!=='string'||value.length>65536)throw new Error('Invalid or oversized native fill value');
        if(request.expectedOrigin!==undefined&&!(e instanceof HTMLInputElement||e instanceof HTMLTextAreaElement))throw new Error('Native guarded fill requires an input or textarea');
        if(e instanceof HTMLInputElement&&['date','time','datetime-local','month','range','week','color'].includes(e.type)){
          const probe=e.cloneNode();probe.value=value;if(value&&(!probe.value||(e.type==='range'&&Number(probe.value)!==Number(value))||(e.type==='color'&&probe.value.toLowerCase()!==value.toLowerCase())))throw new Error('Malformed value');
          if(request.expectedOrigin!==undefined)return guardedFill(e,probe.value,request);
          e.focus();e.value=probe.value;e.dispatchEvent(new Event('input',{bubbles:true,composed:true}));e.dispatchEvent(new Event('change',{bubbles:true}));return {ready:true,done:true};
        }
        if(e instanceof HTMLInputElement&&e.type==='number'&&value!==''&&!Number.isFinite(Number(value)))throw new Error('Cannot type text into input[type=number]');
        if(request.expectedOrigin!==undefined)return guardedFill(e,value,request);
      }
      e.focus();if(action!=='type'){if(typeof e.select==='function')e.select();else {const range=document.createRange();range.selectNodeContents(e);const selection=getSelection();selection.removeAllRanges();selection.addRange(range);}}
      return {ready:true,identity:id(e)};
    }
    e.scrollIntoView({block:'center',inline:'center',behavior:'instant'});
    if(action==='scrollIntoViewIfNeeded')return {ready:true,identity:id(e)};
    if(action==='selectOption'){
      if(!(e instanceof HTMLSelectElement))throw new Error('Element is not a select');
      const choices=request.options;if(!Array.isArray(choices)||choices.length>4096)throw new Error('Invalid native select options');
      const selected=[];for(const choice of choices){if(!choice||typeof choice!=='object'||(!['valueOrLabel','value','label','index'].some(key=>Object.hasOwn(choice,key)))||(choice.index!==undefined&&(!Number.isInteger(choice.index)||choice.index<0)))throw new Error('Invalid native select option');const match=[...e.options].find((o,i)=>choice.valueOrLabel!==undefined?(o.value===choice.valueOrLabel||o.label===choice.valueOrLabel):(choice.value===undefined||o.value===choice.value)&&(choice.label===undefined||o.label===choice.label)&&(choice.index===undefined||i===choice.index));if(!match)return {ready:false,reason:'select options to be present'};selected.push(match);}
      if(!e.multiple&&selected.length>1)throw new Error('Cannot select multiple options in a single select');
      for(const o of e.options)o.selected=selected.includes(o);if(!selected.length)e.selectedIndex=-1;
      e.dispatchEvent(new Event('input',{bubbles:true,composed:true}));e.dispatchEvent(new Event('change',{bubbles:true}));return {ready:true,done:true,value:[...e.selectedOptions].map(o=>o.value)};
    }
    if(['click','tap'].includes(action)&&e instanceof HTMLInputElement&&(!e.getBoundingClientRect().width||!e.getBoundingClientRect().height)&&e.labels?.length)e=e.labels[0];
    const r=e.getBoundingClientRect(), position=request.position;
    const x=position?r.left+parseFloat(getComputedStyle(e).borderLeftWidth)+position.x:(Math.max(0,r.left)+Math.min(innerWidth,r.right))/2;
    const y=position?r.top+parseFloat(getComputedStyle(e).borderTopWidth)+position.y:(Math.max(0,r.top)+Math.min(innerHeight,r.bottom))/2;
    if(!(Number.isFinite(x)&&Number.isFinite(y))||x<0||y<0||x>=innerWidth||y>=innerHeight)return {ready:false,reason:'element inside viewport'};
    if(!force){let hit=document.elementFromPoint(x,y);for(let depth=0;hit?.shadowRoot&&depth<64;depth++){const inner=hit.shadowRoot.elementFromPoint(x,y);if(!inner||inner===hit)break;hit=inner;}let receives=false;for(let n=hit;n;n=parent(n))if(n===e){receives=true;break;}if(!receives&&request.dragging===true){const source=dragSource?.deref();if(source&&source.isConnected)for(let n=hit;n;n=parent(n))if(n===source){receives=true;break;}}if(!receives)return {ready:false,reason:'element to receive pointer events'};}
    return {ready:true,identity:id(e),x,y,box:[r.x,r.y,r.width,r.height]};
  };
  const observe=request=>{
    if(request.referenceFormat!==undefined&&!['json','yaml'].includes(request.referenceFormat))throw new Error('Invalid native reference snapshot format');
    const scope=one(request);if(!scope)return {ready:false};
    if(request.referenceDepth!==undefined&&(!Number.isInteger(request.referenceDepth)||request.referenceDepth<0||request.referenceDepth>64))throw new Error('Invalid native reference snapshot depth');
    const captured=snapshot(scope,query,{ai:true,boxes:request.accessibilityBoxes===true,captureReferences:true});
    const newRefs=new Map();if(++generation>Number.MAX_SAFE_INTEGER)throw new Error('Native snapshot generation limit exceeded');
    for(const [node,index] of captured.entries){const token='r'+prefix+'_'+generation+'_'+index;node.ref=token;newRefs.set(token,new WeakRef(captured.elements[index]));}
    if(newRefs.size>4096)throw new Error('Native snapshot reference limit exceeded');refs=newRefs;
    const opts={mode:'ai',boxes:request.accessibilityBoxes===true};if(request.referenceDepth!==undefined)opts.depth=request.referenceDepth;
    if(request.referenceFormat==='json')return {ready:true,value:{snapshotJSON:JSON.stringify(jsonFormat(captured.forest,opts))}};
    return {ready:true,value:{yamlJSON:JSON.stringify(yamlFormat(captured.forest,opts))}};
  };
  const elementState=request=>{
    const e=one(request),state=request.state;
    if(!['attached','detached','visible','hidden','enabled','disabled','checked','editable'].includes(state))throw new Error('Invalid native element state');
    if(!e)return ['detached','hidden','disabled'].includes(state);
    if(state==='attached')return true;if(state==='detached')return false;
    const actual=query([],e,{elementStateOnly:true});
    if(state==='hidden')return !actual.visible;if(state==='disabled')return !actual.enabled;
    if(state==='checked')return checked(editable(e));return actual[state]===true;
  };
  const read=request=>{if(request.elementState)return {ready:true,value:elementState(request)};if(request.count)return {ready:true,value:['primitive',resolve(request).length]};const e=one(request);if(!e)return {ready:false};if(typeof request.function!=='string'||request.function.length>65536)throw new Error('Invalid native DOM getter');return {ready:true,value:(0,eval)('('+request.function+')')(e)};};
  const verifyChecked=request=>{const e=one(request);return !!e&&checked(editable(e))===request.checked;};
  Object.defineProperty(globalThis,'__suitecutNativeChromeDOM1',{value:{inspect,observe,read,reference,verifyChecked,element:one,all:resolve,elementState,markDragSource:request=>{const e=one(request);dragSource=e?new WeakRef(e):null;return true;}}});return true;
})())JS";
}
