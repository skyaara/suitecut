// SPDX-License-Identifier: MIT
#pragma once
#include <string>
namespace suitecut {
// IndexedDB stores structured-clone values. Preserve their graph and platform
// types independently of ordinary page-evaluation JSON serialization.
inline const std::string& StorageCodecSource() {
  static const std::string source = R"JS(function(){
const constructors={Int8Array,Uint8Array,Uint8ClampedArray,Int16Array,Uint16Array,Int32Array,Uint32Array,Float32Array,Float64Array,BigInt64Array,BigUint64Array,DataView};
const bytesToString=bytes=>{let result='';for(let i=0;i<bytes.length;i+=8192)result+=String.fromCharCode(...bytes.subarray(i,i+8192));return btoa(result);};
const fromString=text=>{const raw=atob(text);if(raw.length>1048576)throw Error('Native storage byte value capacity exceeded');return Uint8Array.from(raw,c=>c.charCodeAt(0)).buffer;};
const byteBudget=()=>{let bytes=0;const charge=count=>{bytes+=count;if(bytes>524288)throw Error('Native storage value bytes capacity exceeded');};
const text=value=>{if(typeof value!=='string')throw Error('Invalid native storage text');for(let i=0;i<value.length;i++){const code=value.charCodeAt(i);if(code<32)charge(6);else if(code===34||code===92)charge(2);else if(code<128)charge(1);else if(code<2048)charge(2);else if(code>=55296&&code<=56319&&i+1<value.length&&value.charCodeAt(i+1)>=56320&&value.charCodeAt(i+1)<=57343){charge(4);i++;}else charge(code>=55296&&code<=57343?6:3);}return value;};return{charge,text};};
async function encode(value){const seen=new Map(),nodes=[],budget=byteBudget();let work=0;
const visit=async(value,depth)=>{if(depth>150||++work>20000)throw Error('Native storage value work capacity exceeded');
budget.charge(32);if(value===undefined)return[1];if(typeof value==='string')budget.text(value);if(value===null||typeof value==='boolean'||typeof value==='string')return[0,value];
if(typeof value==='number')return Number.isFinite(value)&&!Object.is(value,-0)?[0,value]:[3,Object.is(value,-0)?'-0':String(value)];
if(typeof value==='bigint'){const text=String(value);budget.text(text);return[2,text];}if(typeof value!=='object')throw Error('Unsupported native storage value');
if(seen.has(value))return[4,seen.get(value)];const id=nodes.length;seen.set(value,id);nodes.push(null);budget.charge(128);let node;
if(Array.isArray(value)){if(value.length>20000)throw Error('Native storage array capacity exceeded');node={type:'Array',length:value.length,fields:[]};for(const key of Object.keys(value))node.fields.push([budget.text(key),await visit(value[key],depth+1)]);}
else if(value instanceof Date)node={type:'Date',value:Number.isNaN(value.getTime())?'invalid':value.toISOString()};
else if(value instanceof RegExp)node={type:'RegExp',source:budget.text(value.source),flags:budget.text(value.flags)};
else if(value instanceof ArrayBuffer){if(value.byteLength>1048576)throw Error('Native storage byte value capacity exceeded');budget.charge(Math.ceil(value.byteLength/3)*4);node={type:'ArrayBuffer',data:bytesToString(new Uint8Array(value))};}
else if(ArrayBuffer.isView(value)){node={type:value.constructor.name,buffer:await visit(value.buffer,depth+1),offset:value.byteOffset,length:value instanceof DataView?value.byteLength:value.length};}
else if(value instanceof Map){node={type:'Map',entries:[]};for(const [key,item]of value)node.entries.push([await visit(key,depth+1),await visit(item,depth+1)]);}
else if(value instanceof Set){node={type:'Set',values:[]};for(const item of value)node.values.push(await visit(item,depth+1));}
else if(value instanceof Blob){if(value.size>1048576)throw Error('Native storage blob capacity exceeded');budget.charge(Math.ceil(value.size/3)*4);node={type:value instanceof File?'File':'Blob',mime:budget.text(value.type),data:bytesToString(new Uint8Array(await value.arrayBuffer()))};if(value instanceof File){node.name=budget.text(value.name);node.lastModified=value.lastModified;}}
else if(value instanceof DOMException)node={type:'DOMException',name:budget.text(value.name),message:budget.text(value.message)};
else if(value instanceof Error){node={type:'Error',name:budget.text(value.name),message:budget.text(value.message),stack:budget.text(value.stack||'')};if(Object.hasOwn(value,'cause'))node.cause=await visit(value.cause,depth+1);}
else{node={type:'Object',fields:[]};for(const key of Object.keys(value))node.fields.push([budget.text(key),await visit(value[key],depth+1)]);}
nodes[id]=node;return[4,id];};return{version:1,root:await visit(value,0),nodes};}
function decode(input){if(!input||input.version!==1||!Array.isArray(input.nodes)||input.nodes.length>20000)throw Error('Invalid native storage value graph');
const budget=byteBudget(),pending=[[input,0]],seen=new Set();let checked=0;
while(pending.length){const[value,depth]=pending.pop();if(depth>150||++checked>200000)throw Error('Native storage value work capacity exceeded');budget.charge(8);if(typeof value==='string')budget.text(value);else if(value&&typeof value==='object'){if(seen.has(value))throw Error('Invalid native storage graph cycle');seen.add(value);for(const key of Object.keys(value)){budget.text(key);if(pending.length+checked>=200000)throw Error('Native storage value work capacity exceeded');pending.push([value[key],depth+1]);}}}
const nodes=input.nodes,values=new Array(nodes.length);let work=0;
const read=tag=>{if(++work>200000||!Array.isArray(tag))throw Error('Invalid native storage value tag');
switch(tag[0]){case 0:if(tag[1]===null||['boolean','string'].includes(typeof tag[1])||typeof tag[1]==='number'&&Number.isFinite(tag[1]))return tag[1];break;
case 1:return undefined;case 2:if(typeof tag[1]==='string'&&/^-?[0-9]+$/.test(tag[1]))return BigInt(tag[1]);break;
case 3:if(tag[1]==='-0')return-0;if(tag[1]==='NaN')return NaN;if(tag[1]==='Infinity')return Infinity;if(tag[1]==='-Infinity')return-Infinity;break;
case 4:if(Number.isInteger(tag[1])&&tag[1]>=0&&tag[1]<values.length)return values[tag[1]];break;}throw Error('Invalid native storage value tag');};
for(let i=0;i<nodes.length;i++){const node=nodes[i];if(!node||typeof node.type!=='string')throw Error('Invalid native storage graph node');
switch(node.type){case'Array':if(Object.hasOwn(node,'fields')){if(!Number.isInteger(node.length)||node.length<0||node.length>20000)throw Error('Invalid native storage array length');values[i]=new Array(node.length);}else values[i]=[];break;case'Object':values[i]={};break;case'Map':values[i]=new Map();break;case'Set':values[i]=new Set();break;
case'Date':values[i]=new Date(node.value==='invalid'?NaN:node.value);if(node.value!=='invalid'&&!Number.isFinite(values[i].getTime()))throw Error('Invalid native storage date');break;
case'RegExp':values[i]=new RegExp(node.source,node.flags);break;case'ArrayBuffer':values[i]=fromString(node.data);break;
case'Blob':values[i]=new Blob([fromString(node.data)],{type:node.mime});break;
case'File':values[i]=new File([fromString(node.data)],node.name,{type:node.mime,lastModified:node.lastModified});break;
case'DOMException':values[i]=new DOMException(node.message,node.name);break;
case'Error':values[i]=new Error(node.message);values[i].name=node.name;values[i].stack=node.stack;break;
default:if(!Object.hasOwn(constructors,node.type))throw Error('Unsupported native storage graph node');}}
for(let i=0;i<nodes.length;i++){const node=nodes[i];if(Object.hasOwn(constructors,node.type)){const buffer=read(node.buffer);if(!(buffer instanceof ArrayBuffer)||!Number.isInteger(node.offset)||!Number.isInteger(node.length)||node.offset<0||node.length<0)throw Error('Invalid native storage view');values[i]=new constructors[node.type](buffer,node.offset,node.length);}}
for(let i=0;i<nodes.length;i++){const node=nodes[i],target=values[i];
if(node.type==='Array'&&!Object.hasOwn(node,'fields')||node.type==='Set'){if(!Array.isArray(node.values)||node.values.length>20000)throw Error('Invalid native storage sequence');for(const value of node.values)if(node.type==='Array')target.push(read(value));else target.add(read(value));}
else if(node.type==='Map'){if(!Array.isArray(node.entries)||node.entries.length>20000)throw Error('Invalid native storage map');for(const entry of node.entries){if(!Array.isArray(entry)||entry.length!==2)throw Error('Invalid native storage map entry');target.set(read(entry[0]),read(entry[1]));}}
else if(node.type==='Error'&&Object.hasOwn(node,'cause')){Object.defineProperty(target,'cause',{value:read(node.cause),writable:true,configurable:true});}
else if(node.type==='Object'||node.type==='Array'&&Object.hasOwn(node,'fields')){if(!Array.isArray(node.fields)||node.fields.length>20000)throw Error('Invalid native storage object');const keys=new Set();for(const entry of node.fields){if(!Array.isArray(entry)||entry.length!==2||typeof entry[0]!=='string'||keys.has(entry[0]))throw Error('Invalid native storage field');keys.add(entry[0]);Object.defineProperty(target,entry[0],{value:read(entry[1]),writable:true,enumerable:true,configurable:true});}}}
return read(input.root);}
return{encode,decode};})JS";
  return source;
}
}
