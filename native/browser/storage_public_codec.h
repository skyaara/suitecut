// SPDX-License-Identifier: MIT
#pragma once
#include "storage_codec.h"
namespace suitecut {
// Observable IndexedDB records use JSON values when lossless, otherwise tagged
// values with explicit graph identities. Structured-clone ownership stays native.
inline std::string StoragePublicCodecSource() {
  return "function(){const internal=(" + StorageCodecSource() + R"JS()();
async function pack(value,name){
const graph=await internal.encode(value),refs=new Map();
for(const node of graph.nodes){const tags=node.fields?node.fields.map(entry=>entry[1]):node.type==='Map'?node.entries.flat():node.type==='Set'?node.values:node.buffer?[node.buffer]:node.cause?[node.cause]:[];for(const tag of tags)if(tag[0]===4)refs.set(tag[1],(refs.get(tag[1])||0)+1);}
if(graph.root[0]===4)refs.set(graph.root[1],(refs.get(graph.root[1])||0)+1);
let simple=true;const visited=new Set();
const raw=(tag,depth)=>{if(depth>150)throw Error('Native storage value depth capacity exceeded');if(tag[0]===0)return tag[1];if(tag[0]!==4){simple=false;return null;}const id=tag[1],node=graph.nodes[id];
if(visited.has(id)||!['Object','Array'].includes(node.type)){simple=false;return null;}visited.add(id);const out=node.type==='Array'?[]:{};
if(node.type==='Array'&&(node.fields.length!==node.length||node.fields.some((entry,index)=>entry[0]!==String(index)))){simple=false;return null;}
for(const[key,item]of node.fields)Object.defineProperty(out,key,{value:raw(item,depth+1),writable:true,configurable:true,enumerable:true});return out;};
const ordinary=raw(graph.root,0);if(simple)return{[name]:ordinary};
const ids=new Map();let next=0;
const wire=(tag,depth)=>{if(depth>150)throw Error('Native storage value depth capacity exceeded');
if(tag[0]===0)return tag[1];if(tag[0]===1)return{v:'undefined'};if(tag[0]===2)return{bi:tag[1]};if(tag[0]===3)return{v:tag[1]};
const index=tag[1],node=graph.nodes[index];if(ids.has(index))return{ref:ids.get(index)};
const owns=['Object','Array','Map','Set'].includes(node.type)||(refs.get(index)||0)>1;const id=owns?++next:undefined;if(owns)ids.set(index,id);
const attach=out=>id===undefined?out:{id,...out};
switch(node.type){
case'Object':return attach({o:node.fields.map(([k,v])=>({k,v:wire(v,depth+1)}))});
case'Array':{const fields=new Map(node.fields),a=[];for(let i=0;i<node.length;i++)a.push(fields.has(String(i))?wire(fields.get(String(i)),depth+1):{h:true});const extra=node.fields.filter(([key])=>String(Number(key))!==key||Number(key)>=node.length).map(([k,v])=>({k,v:wire(v,depth+1)}));return attach(extra.length?{a,props:extra}:{a});}
case'Map':return attach({m:node.entries.map(([k,v])=>({k:wire(k,depth+1),v:wire(v,depth+1)}))});
case'Set':return attach({s:node.values.map(v=>wire(v,depth+1))});
case'Date':return attach({d:node.value});
case'RegExp':return attach({r:{p:node.source,f:node.flags}});
case'ArrayBuffer':return attach({ab:{b:node.data}});
case'Blob':return attach({b:{b:node.data,t:node.mime}});
case'File':return attach({f:{b:node.data,t:node.mime,n:node.name,m:node.lastModified}});
case'DOMException':return attach({de:{n:node.name,m:node.message}});
case'Error':return attach({e:{n:node.name,m:node.message,s:node.stack,...(node.cause?{c:wire(node.cause,depth+1)}:{})}});
default:return attach({ta:{k:node.type,b:wire(node.buffer,depth+1),o:node.offset,l:node.length}});
}};
return{[name+'Encoded']:wire(graph.root,0)};
}
function unpack(record,name){
if(Object.hasOwn(record,name)&&Object.hasOwn(record,name+'Encoded'))throw Error('Ambiguous native IndexedDB value');
if(Object.hasOwn(record,name))return record[name];
if(!Object.hasOwn(record,name+'Encoded'))throw Error('Missing native IndexedDB value');
const input=record[name+'Encoded'],nodes=[],ids=new Map(),references=[];let work=0;
const visit=(value,depth)=>{if(depth>150||++work>20000)throw Error('Native storage value work capacity exceeded');
if(value===null||['string','boolean','number'].includes(typeof value))return[0,value];
if(!value||typeof value!=='object'||Array.isArray(value))throw Error('Invalid native IndexedDB tagged value');
if(Object.hasOwn(value,'ref')){if(!Number.isInteger(value.ref)||value.ref<1)throw Error('Invalid native IndexedDB reference');const tag=[4,-1];references.push([tag,value.ref]);return tag;}
if(Object.hasOwn(value,'v')){if(value.v==='undefined')return[1];if(['-0','NaN','Infinity','-Infinity'].includes(value.v))return[3,value.v];throw Error('Invalid native IndexedDB primitive');}
if(Object.hasOwn(value,'bi'))return[2,value.bi];
const index=nodes.length;nodes.push(null);if(Object.hasOwn(value,'id')){if(!Number.isInteger(value.id)||value.id<1||ids.has(value.id))throw Error('Invalid native IndexedDB identity');ids.set(value.id,index);}
let node;const fields=list=>{if(!Array.isArray(list)||list.length>20000)throw Error('Invalid native IndexedDB fields');const keys=new Set();return list.map(entry=>{if(!entry||typeof entry.k!=='string'||keys.has(entry.k))throw Error('Invalid native IndexedDB field');keys.add(entry.k);return[entry.k,visit(entry.v,depth+1)];});};
if(Object.hasOwn(value,'o'))node={type:'Object',fields:fields(value.o)};
else if(Object.hasOwn(value,'a')){if(!Array.isArray(value.a)||value.a.length>20000)throw Error('Invalid native IndexedDB array');node={type:'Array',length:value.a.length,fields:[]};value.a.forEach((item,i)=>{if(!(item&&typeof item==='object'&&item.h===true))node.fields.push([String(i),visit(item,depth+1)]);});if(value.props)node.fields.push(...fields(value.props));}
else if(Object.hasOwn(value,'m')){if(!Array.isArray(value.m)||value.m.length>20000)throw Error('Invalid native IndexedDB map');node={type:'Map',entries:value.m.map(entry=>[visit(entry.k,depth+1),visit(entry.v,depth+1)])};}
else if(Object.hasOwn(value,'s')){if(!Array.isArray(value.s)||value.s.length>20000)throw Error('Invalid native IndexedDB set');node={type:'Set',values:value.s.map(item=>visit(item,depth+1))};}
else if(Object.hasOwn(value,'d'))node={type:'Date',value:value.d};
else if(value.r)node={type:'RegExp',source:value.r.p,flags:value.r.f};
else if(value.ab)node={type:'ArrayBuffer',data:value.ab.b};
else if(value.b)node={type:'Blob',data:value.b.b,mime:value.b.t};
else if(value.f)node={type:'File',data:value.f.b,mime:value.f.t,name:value.f.n,lastModified:value.f.m};
else if(value.de)node={type:'DOMException',name:value.de.n,message:value.de.m};
else if(value.e)node={type:'Error',name:value.e.n,message:value.e.m,stack:value.e.s,...(Object.hasOwn(value.e,'c')?{cause:visit(value.e.c,depth+1)}:{})};
else if(value.ta)node={type:value.ta.k,buffer:visit(value.ta.b,depth+1),offset:value.ta.o,length:value.ta.l};
else throw Error('Unsupported native IndexedDB tagged value');
nodes[index]=node;return[4,index];};
const root=visit(input,0);for(const[tag,id]of references){if(!ids.has(id))throw Error('Unknown native IndexedDB reference');tag[1]=ids.get(id);}return internal.decode({version:1,root,nodes});
}
return{pack,unpack};})JS";
}
}
