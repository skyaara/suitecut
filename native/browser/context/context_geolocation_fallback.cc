// SPDX-License-Identifier: MIT
#include "context/context_geolocation_fallback.h"
namespace suitecut {
namespace {
// The installer runs in every realm of the target, before page scripts for
// new documents and immediately for existing ones. A realm that already has
// the fallback only takes the new state. Patched members keep their property
// attributes, names, lengths and a native Function.prototype.toString answer;
// nothing new becomes enumerable. Errors from invalid calls come from the
// native members themselves.
constexpr std::string_view kNativeGeolocationFallbackSource = R"JS((()=>{'use strict';
const token=__SUITECUT_TOKEN__,incoming=__SUITECUT_STATE__,unavailable=__SUITECUT_UNAVAILABLE__;
const G=globalThis.Geolocation,N=globalThis.navigator;
if(typeof G!=='function'||!N)return;
const GP=G.prototype,geo=N.geolocation;
try{if(GP.getCurrentPosition.call(geo,token,incoming)===token)return;}catch(e){}
if(!incoming.active)return;
const GPos=globalThis.GeolocationPosition,GCoords=globalThis.GeolocationCoordinates,
GErr=globalThis.GeolocationPositionError;
if(!GPos||!GCoords||!GErr)return;
const O=Object,R=Reflect,apply=R.apply,define=O.defineProperty,describe=O.getOwnPropertyDescriptor,
create=O.create,WM=WeakMap.prototype,wmGet=WM.get,wmSet=WM.set,wmHas=WM.has,
later=globalThis.setTimeout,now=Date.now,isArray=Array.isArray,
FP=Function.prototype,nativeToString=FP.toString;
const masks=new WeakMap();
const mask=(fake,original)=>{apply(wmSet,masks,[fake,apply(nativeToString,original,[])]);
define(fake,'length',{value:original.length});define(fake,'name',{value:original.name});return fake;};
const toStringDescriptor=describe(FP,'toString');
const fakeToString={toString(){const m=apply(wmGet,masks,[this]);
return m!==undefined?m:apply(nativeToString,this,arguments);}}.toString;
mask(fakeToString,nativeToString);
define(FP,'toString',{...toStringDescriptor,value:fakeToString});
const replaceMethod=(proto,name,make)=>{const d=describe(proto,name);
if(!d||typeof d.value!=='function')return null;const original=d.value;
define(proto,name,{...d,value:mask(make(original),original)});return original;};
const NONE={};
const replaceGetter=(proto,name,read)=>{const d=describe(proto,name);if(!d||!d.get)return;
const original=d.get;const fake=describe({get[name](){const v=read(this);
return v!==NONE?v:apply(original,this,arguments);}},name).get;
define(proto,name,{...d,get:mask(fake,original)});};
let state=incoming;
const topOrigin=()=>{try{const a=location.ancestorOrigins;if(a&&a.length)return a[a.length-1];}catch(e){}
return location.origin;};
const policyGetter=globalThis.Document&&describe(Document.prototype,'featurePolicy');
const allows=globalThis.FeaturePolicy&&FeaturePolicy.prototype.allowsFeature;
const policyAllows=()=>{try{if(!policyGetter||!policyGetter.get||typeof allows!=='function')return true;
const policy=apply(policyGetter.get,document,[]);return !policy||apply(allows,policy,['geolocation']);}
catch(e){return true;}};
const originGranted=s=>{const origin=topOrigin();if(typeof origin!=='string')return false;
if(s.any)return origin.startsWith('https://')||origin.startsWith('http://');
return isArray(s.origins)&&s.origins.includes(origin);};
const granted=s=>!!(s&&s.active&&s.coords&&globalThis.isSecureContext&&originGranted(s));
const handled=()=>granted(state)&&policyAllows();
const positions=new WeakMap(),coordinates=new WeakMap(),errors=new WeakMap();
const fields=['accuracy','latitude','longitude','altitude','altitudeAccuracy','heading','speed'];
{for(const f of fields)replaceGetter(GCoords.prototype,f,o=>{const c=apply(wmGet,coordinates,[o]);
return c===undefined?NONE:(c[f]===undefined?null:c[f]);});
replaceMethod(GCoords.prototype,'toJSON',original=>({toJSON(){const c=apply(wmGet,coordinates,[this]);
if(c===undefined)return apply(original,this,arguments);const out={};for(const f of fields)out[f]=c[f]===undefined?null:c[f];
return out;}}).toJSON);}
{replaceGetter(GPos.prototype,'coords',o=>{const p=apply(wmGet,positions,[o]);return p===undefined?NONE:p.coords;});
replaceGetter(GPos.prototype,'timestamp',o=>{const p=apply(wmGet,positions,[o]);return p===undefined?NONE:p.timestamp;});
replaceMethod(GPos.prototype,'toJSON',original=>({toJSON(){const p=apply(wmGet,positions,[this]);
if(p===undefined)return apply(original,this,arguments);
return {timestamp:p.timestamp,coords:this.coords.toJSON()};}}).toJSON);}
{replaceGetter(GErr.prototype,'code',o=>{const e=apply(wmGet,errors,[o]);return e===undefined?NONE:e.code;});
replaceGetter(GErr.prototype,'message',o=>{const e=apply(wmGet,errors,[o]);return e===undefined?NONE:e.message;});}
let cached=null,cachedKey='';
const keyOf=s=>s.coords.latitude+','+s.coords.longitude+','+s.coords.accuracy;
const position=()=>{const key=keyOf(state);if(cached&&cachedKey===key)return cached;
const c=create(GCoords.prototype);apply(wmSet,coordinates,[c,{latitude:state.coords.latitude,
longitude:state.coords.longitude,accuracy:state.coords.accuracy}]);
const p=create(GPos.prototype);apply(wmSet,positions,[p,{coords:c,timestamp:now()}]);
cached=p;cachedKey=key;return p;};
const failure=(code,message)=>{const e=create(GErr.prototype);apply(wmSet,errors,[e,{code,message}]);return e;};
const clamp=v=>{v=+v;return v>0?(v>4294967295?4294967295:Math.floor(v)):0;};
const optionsOf=o=>{const r={maximumAge:0,timeout:4294967295};if(o==null)return r;
if(o.maximumAge!==undefined)r.maximumAge=clamp(o.maximumAge);if(o.timeout!==undefined)r.timeout=clamp(o.timeout);return r;};
const invalid=(success,error,options)=>typeof success!=='function'||(error!=null&&typeof error!=='function')||
(options!=null&&typeof options!=='object'&&typeof options!=='function');
const deliver=(success,error,options)=>{if(cached&&cachedKey===keyOf(state)&&options.maximumAge>0&&
now()-cached.timestamp<=options.maximumAge){const p=cached;later(()=>success(p),0);return;}
if(options.timeout===0){if(error){const e=failure(3,'Timeout expired');later(()=>error(e),0);}return;}
const p=position();later(()=>success(p),0);};
const watchers=new Map();let nextWatch=1;
replaceMethod(GP,'getCurrentPosition',original=>({getCurrentPosition(success){
if(success===token&&this===geo){update(arguments[1]);return token;}
if(this!==geo||!handled()||invalid(success,arguments[1],arguments[2]))return apply(original,this,arguments);
deliver(success,arguments[1],optionsOf(arguments[2]));}}).getCurrentPosition);
replaceMethod(GP,'watchPosition',original=>({watchPosition(success){
if(this!==geo||!handled()||invalid(success,arguments[1],arguments[2])){const id=apply(original,this,arguments);
if(typeof id==='number'&&id>=nextWatch)nextWatch=id+1;return id;}
const id=nextWatch++;const watcher={success,error:arguments[1],options:optionsOf(arguments[2])};
watchers.set(id,watcher);deliver(watcher.success,watcher.error,watcher.options);return id;}}).watchPosition);
replaceMethod(GP,'clearWatch',original=>({clearWatch(id){if(this===geo)watchers.delete(+id);
return apply(original,this,arguments);}}).clearWatch);
const PermP=globalThis.Permissions&&Permissions.prototype,PSP=globalThis.PermissionStatus&&PermissionStatus.prototype;
const statuses=new WeakMap();
if(PermP&&PSP){replaceMethod(PermP,'query',original=>({query(descriptor){const result=apply(original,this,arguments);
let geolocation=false;try{geolocation=descriptor!=null&&descriptor.name==='geolocation';}catch(e){}
if(!geolocation||!result||typeof result.then!=='function')return result;
return result.then(status=>{if(status&&typeof status==='object')apply(wmSet,statuses,[status,true]);return status;});}}).query);
replaceGetter(PSP,'state',o=>apply(wmHas,statuses,[o])&&handled()?'granted':NONE);}
const update=next=>{if(!next||typeof next!=='object')return;const before=handled(),beforeKey=before?keyOf(state):'';
state=next;if(handled()){if(keyOf(state)!==beforeKey){const p=position();for(const w of watchers.values()){const s=w.success;later(()=>s(p),0);}}return;}
if(!before)return;
const revoked=!originGranted(next);
for(const [id,w] of watchers){if(revoked)watchers.delete(id);if(!w.error)continue;
const e=revoked?failure(1,'User denied Geolocation'):failure(2,unavailable);const callback=w.error;later(()=>callback(e),0);}};
})())JS";

// Chromium reports a cleared emulated position as POSITION_UNAVAILABLE with no message.
constexpr std::string_view kNativeGeolocationUnavailable = "";

}  // namespace

std::string NativeGeolocationFallbackScript(const std::string& token,
                                            const std::string& state_json) {
  std::string out(kNativeGeolocationFallbackSource);
  const auto replace = [&out](std::string_view key, const std::string& value) {
    const auto at = out.find(key);
    if (at != std::string::npos) out.replace(at, key.size(), value);
  };
  replace("__SUITECUT_TOKEN__", NativeGeolocationJsonString(token));
  replace("__SUITECUT_STATE__", state_json);
  replace("__SUITECUT_UNAVAILABLE__", NativeGeolocationJsonString(kNativeGeolocationUnavailable));
  return out;
}
}  // namespace suitecut
