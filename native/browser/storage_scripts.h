// SPDX-License-Identifier: MIT
#pragma once
#include <string>
namespace suitecut {
inline std::string StorageQuoteString(const std::string& input) {
  std::string output = "\""; const char* hex = "0123456789abcdef";
  for (const unsigned char c : input) {
    if (c == '\\' || c == '\"') { output += '\\'; output += static_cast<char>(c); }
    else if (c < 32) { output += "\\u00"; output += hex[c >> 4]; output += hex[c & 15]; }
    else output += static_cast<char>(c);
  }
  return output + "\"";
}
inline std::string StorageCaptureScript(const std::string& codec, bool indexed_db, bool opfs) {
  return "(async()=>{const codec=(" + codec + ")();const includeDB=" + (indexed_db ? "true" : "false") + ";const includeOPFS=" + (opfs ? "true" : "false") + R"JS(;
const output={origin:location.origin,localStorage:[]};let bytes=0;const budget=value=>{bytes+=new TextEncoder().encode(JSON.stringify(value)).length;if(bytes>524288)throw Error('Native origin storage bytes capacity exceeded');};
if(localStorage.length>4096)throw Error('Native localStorage entry capacity exceeded');
for(let i=0;i<localStorage.length;i++){const name=localStorage.key(i);const entry={name,value:localStorage.getItem(name)};budget(entry);output.localStorage.push(entry);}
if(includeOPFS){
const root=await navigator.storage.getDirectory();output.opfs=[];let entries=0;
const bytesToBase64=bytes=>{let raw='';for(let i=0;i<bytes.length;i+=8192)raw+=String.fromCharCode(...bytes.subarray(i,i+8192));return btoa(raw);};
const walk=async(directory,prefix,depth)=>{if(depth>64)throw Error('Native OPFS depth capacity exceeded');const children=[];for await(const[name,handle]of directory.entries()){if(++entries>4096||name.length>255)throw Error('Native OPFS entry capacity exceeded');const path=prefix?prefix+'/'+name:name;if(path.length>4096)throw Error('Native OPFS path capacity exceeded');budget(path);children.push([name,handle,path]);}children.sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0);
for(const[name,handle,path]of children){if(handle.kind==='directory'){const entry={path,type:'directory'};budget(entry);output.opfs.push(entry);await walk(handle,path,depth+1);}else{const file=await handle.getFile();if(file.size>524288||Math.ceil(file.size/3)*4+bytes>524288)throw Error('Native OPFS file bytes capacity exceeded');const entry={path,type:'file',base64:bytesToBase64(new Uint8Array(await file.arrayBuffer()))};budget(entry);output.opfs.push(entry);}}};
await walk(root,'',0);
}
const publish=()=>{const stateJSON=JSON.stringify(output);return /\\ud[89a-f][0-9a-f]{2}/i.test(stateJSON)?{origin:output.origin,stateJSON,empty:!output.localStorage.length&&!output.indexedDB?.length&&!output.opfs?.length}:output;};
if(!includeDB)return publish();
const request=r=>new Promise((resolve,reject)=>{r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);r.onblocked=()=>reject(Error('Native IndexedDB operation blocked'));});
const databases=await indexedDB.databases();if(databases.length>128)throw Error('Native IndexedDB database capacity exceeded');
output.indexedDB=[];let records=0;
for(const info of databases){const db=await request(indexedDB.open(info.name));try{
const database={name:db.name,version:db.version,stores:[]};budget({name:db.name,version:db.version});
if(db.objectStoreNames.length>128)throw Error('Native IndexedDB store capacity exceeded');
for(const name of db.objectStoreNames){const tx=db.transaction(name,'readonly'),store=tx.objectStore(name);
const entry={name:store.name,keyPath:store.keyPath,autoIncrement:store.autoIncrement,indexes:[],records:[]};
if(store.indexNames.length>128)throw Error('Native IndexedDB index capacity exceeded');
for(const name of store.indexNames){const index=store.index(name);entry.indexes.push({name:index.name,keyPath:index.keyPath,unique:index.unique,multiEntry:index.multiEntry});}
budget({name:entry.name,keyPath:entry.keyPath,autoIncrement:entry.autoIncrement,indexes:entry.indexes});
let previous,first=true;while(true){const reading=db.transaction(name,'readonly').objectStore(name);const cursor=await request(reading.openCursor(first?null:IDBKeyRange.lowerBound(previous,true)));if(!cursor)break;if(++records>10000)throw Error('Native IndexedDB record capacity exceeded');previous=cursor.key;first=false;const record={...(entry.keyPath===null?await codec.pack(cursor.key,'key'):{}),...await codec.pack(cursor.value,'value')};budget(record);entry.records.push(record);}
database.stores.push(entry);}
output.indexedDB.push(database);
}finally{db.close();}}
return publish();})())JS";
}
inline std::string StorageRestoreScript(const std::string& codec, const std::string& input, bool clear_idb) {
  return "(async()=>{const codec=(" + codec + ")();const wrapped=JSON.parse(" + StorageQuoteString(input) + ");const input=Object.hasOwn(wrapped,'stateJSON')?JSON.parse(wrapped.stateJSON):wrapped;const clearDB=" + (clear_idb ? "true" : "false") + R"JS(;
if(Object.hasOwn(wrapped,'stateJSON'))input.origin=wrapped.origin;
if(input.origin!==location.origin)throw Error('Native storage restore origin mismatch');
const unique=(values,label)=>{const seen=new Set();for(const value of values){if(typeof value!=='string'||seen.has(value))throw Error('Invalid native '+label+' names');seen.add(value);}};
unique(input.localStorage.map(entry=>entry.name),'localStorage');unique((input.indexedDB||[]).map(db=>db.name),'IndexedDB');
for(const db of input.indexedDB||[]){unique(db.stores.map(store=>store.name),'IndexedDB store');for(const store of db.stores)unique(store.indexes.map(index=>index.name),'IndexedDB index');}
localStorage.clear();for(const entry of input.localStorage)localStorage.setItem(entry.name,entry.value);
const request=r=>new Promise((resolve,reject)=>{r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);r.onblocked=()=>reject(Error('Native IndexedDB operation blocked'));});
if(clearDB){for(const db of await indexedDB.databases())await request(indexedDB.deleteDatabase(db.name));}
for(const database of input.indexedDB||[]){const opening=indexedDB.open(database.name,database.version);
opening.onupgradeneeded=()=>{const db=opening.result;for(const entry of database.stores){const options={autoIncrement:entry.autoIncrement};if(entry.keyPath!==null)options.keyPath=entry.keyPath;
const store=db.createObjectStore(entry.name,options);for(const index of entry.indexes)store.createIndex(index.name,index.keyPath,{unique:index.unique,multiEntry:index.multiEntry});}};
const db=await request(opening);try{if(!database.stores.length)continue;
const tx=db.transaction(database.stores.map(store=>store.name),'readwrite');
const completed=new Promise((resolve,reject)=>{tx.oncomplete=()=>resolve();tx.onerror=()=>reject(tx.error);tx.onabort=()=>reject(tx.error||Error('Native IndexedDB transaction aborted'));});
for(const entry of database.stores){const store=tx.objectStore(entry.name);for(const record of entry.records){const value=codec.unpack(record,'value');if(entry.keyPath===null)store.put(value,codec.unpack(record,'key'));else store.put(value);}}
await completed;
}finally{db.close();}}
if(Object.hasOwn(input,'opfs')||typeof navigator.storage?.getDirectory==='function'){
const root=await navigator.storage.getDirectory(),existing=[];for await(const name of root.keys()){if(existing.length>=4096)throw Error('Native OPFS existing entry capacity exceeded');existing.push(name);}for(const name of existing)await root.removeEntry(name,{recursive:true});
const entries=[...(input.opfs||[])].sort((a,b)=>a.path.split('/').length-b.path.split('/').length||(a.path<b.path?-1:a.path>b.path?1:0));
for(const entry of entries){const segments=entry.path.split('/');if(segments.length>64||segments.some(name=>!name||name==='.'||name==='..'||name.includes('\\')))throw Error('Invalid native OPFS path');let parent=root;for(const name of segments.slice(0,-1))parent=await parent.getDirectoryHandle(name,{create:true});const name=segments.at(-1);
if(entry.type==='directory')await parent.getDirectoryHandle(name,{create:true});else{const raw=atob(entry.base64),data=Uint8Array.from(raw,c=>c.charCodeAt(0));const handle=await parent.getFileHandle(name,{create:true}),writer=await handle.createWritable();let closed=false;try{await writer.write(data);await writer.close();closed=true;}finally{if(!closed)await writer.abort().catch(()=>{});}}}
}
return true;})())JS";
}
}
