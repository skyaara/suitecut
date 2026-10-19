// SPDX-License-Identifier: MIT
#include "bidi_privileged.hpp"
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace suitecut::detail {
namespace {
std::string field(const Json& value,const char* name,const std::string& fallback={}) {
  return value.contains(name)?value.at(name).as_string():fallback;
}
std::string lookup(const std::string& context) {
  return "const {NavigableManager}=ChromeUtils.importESModule('chrome://remote/content/shared/NavigableManager.sys.mjs');"
    "const bc=NavigableManager.getBrowsingContextById("+Json(context).dump()+");";
}
void choice(const std::string& value,std::initializer_list<const char*> allowed,const char* name) {
  for(const auto* item:allowed)if(value==item)return;
  throw std::invalid_argument(std::string("Unsupported Firefox native ")+name+": "+value);
}
Json media_properties(const Json& fields) {
  Json result=Json::object{};
  for(const auto&[key,value]:fields.as_object()) {
    if(key=="command"||key=="action"||key=="pageId"||key=="timeoutMs"||key=="id"||key=="version"||key=="api")continue;
    if(value.is_null()) {
      const std::map<std::string,std::string> names{{"media","mediumOverride"},{"colorScheme","prefersColorSchemeOverride"},
        {"reducedMotion","prefersReducedMotionOverride"},{"forcedColors","forcedColorsOverride"},{"contrast","prefersContrastOverride"}};
      const auto found=names.find(key);
      if(found==names.end())throw std::invalid_argument("Unsupported Firefox native media option: "+key);
      result[found->second]=Json{};continue;
    }
    if(key=="media") {const auto v=value.is_null()?std::string{}:value.as_string();choice(v,{"","screen","print"},"media");result["mediumOverride"]=v;}
    else if(key=="colorScheme") {const auto v=value.is_null()?"none":value.as_string();choice(v,{"none","light","dark","no-preference"},"color scheme");result["prefersColorSchemeOverride"]=v=="no-preference"?"none":v;}
    else if(key=="reducedMotion") {const auto v=value.is_null()?"none":value.as_string();choice(v,{"none","reduce","no-preference"},"reduced motion");result["prefersReducedMotionOverride"]=v;}
    else if(key=="forcedColors") {const auto v=value.is_null()?"none":value.as_string();choice(v,{"none","active"},"forced colors");result["forcedColorsOverride"]=v;}
    else if(key=="contrast") {const auto v=value.is_null()?"none":value.as_string();choice(v,{"none","no-preference","more","less","custom"},"contrast");result["prefersContrastOverride"]=v;}
    else throw std::invalid_argument("Unsupported Firefox native media option: "+key);
  }
  return result;
}
} // namespace

struct BidiPrivileged::Impl {
  std::timed_mutex mutex;
  std::map<std::string,Json> originals;
  bool closed=false;
};
BidiPrivileged::BidiPrivileged():impl_(std::make_unique<Impl>()){}
BidiPrivileged::~BidiPrivileged()=default;

Json BidiPrivileged::evaluate(BidiRuntime& runtime,const BidiPage& page,const std::string& expression,Deadline deadline) {
  bidi_check_active(deadline);
  if(expression.size()>49152)throw std::invalid_argument("Firefox privileged authored script exceeds 48 KiB");
  Json tree;
  try {tree=runtime.call("browsingContext.getTree",Json{{"moz:scope","chrome"},{"maxDepth",0}},deadline);}
  catch(const std::exception& error) {throw std::runtime_error("Firefox native privileged automation requires -remote-allow-system-access on the caller's browser: "+std::string(error.what()));}
  const auto& contexts=tree.at("contexts").as_array();
  if(contexts.empty()||contexts.size()>256)throw std::runtime_error("Firefox privileged chrome browsing context is unavailable or exceeds limits");
  auto chrome=page;
  chrome.browsing_context=contexts.front().at("context").as_string();chrome.sandbox.clear();
  if(!runtime.value(chrome,"typeof ChromeUtils!=='undefined'&&typeof ChromeUtils.importESModule==='function'",deadline).as_bool())
    throw std::runtime_error("Firefox protected realm is unavailable; caller must enable -remote-allow-system-access");
  return runtime.value(chrome,expression,deadline);
}

Json BidiPrivileged::command(BidiRuntime& runtime,BidiPage& page,const Json& fields,Deadline deadline) {
  std::unique_lock lock(impl_->mutex,std::defer_lock);
  if(!lock.try_lock_until(deadline))throw std::runtime_error("Firefox privileged command original deadline expired waiting for helper lock");
  bidi_check_active(deadline);
  if(impl_->closed)throw std::runtime_error("Firefox privileged helper is closed");
  const auto family=field(fields,"command"),action=field(fields,"action");
  if(family=="emulation"&&action=="media") {
    auto properties=media_properties(fields);
    if(properties.as_object().empty())return Json::object{};
    if(!impl_->originals.contains(page.browsing_context)&&impl_->originals.size()>=256)
      throw std::runtime_error("Firefox native media context budget exceeded");
    const auto before=evaluate(runtime,page,"(()=>{"+lookup(page.browsing_context)+
      "if(!bc)throw new Error('Firefox target browsing context is closed');const requested="+properties.dump()+";const before={};"
      "for(const key of Object.keys(requested)){if(!(key in bc))throw new Error('Firefox does not expose native per-context media property: '+key);before[key]=bc[key];}return before;})()",deadline);
    auto& original=impl_->originals[page.browsing_context];
    if(original.is_null())original=Json::object{};
    for(const auto&[key,value]:before.as_object())if(!original.contains(key))original[key]=value;
    for(auto&[key,value]:properties.as_object())if(value.is_null())value=original.at(key);
    // Save the baseline before dispatching setters. A timeout cannot discard
    // ownership of already-applied changes needed by close restoration.
    return evaluate(runtime,page,"(()=>{"+lookup(page.browsing_context)+
      "if(!bc)throw new Error('Firefox target browsing context is closed');const values="+properties.dump()+",before="+before.dump()+";"
      "try{for(const [key,value]of Object.entries(values))bc[key]=value;}catch(error){for(const [key,value]of Object.entries(before))try{bc[key]=value}catch(_){}throw error;}"
      "const applied={};for(const key of Object.keys(values))applied[key]=bc[key];return {native:true,context:"+Json(page.browsing_context).dump()+",applied};})()",deadline);
  }
  if(family=="download"&&action=="cancel") {
    const auto& record=fields.at("_suitecutDownload");
    const auto id=record.at("downloadId").as_string(),url=record.at("url").as_string();
    const auto context=field(record,"context",page.browsing_context),filename=record.at("suggestedFilename").as_string();
    if(id.empty()||id.size()>256||url.size()>16384||filename.size()>4096||!record.contains("timestamp")||!record.at("timestamp").is_number())
      throw std::invalid_argument("Firefox download cancellation requires a bounded native owning record and start timestamp");
    Json identity{{"downloadId",id},{"url",url},{"filename",filename},{"timestamp",record.at("timestamp")}};
    if(fields.contains("_suitecutNativePath"))identity["path"]=fields.at("_suitecutNativePath");
    return evaluate(runtime,page,"(async()=>{"+lookup(context)+
      "const owner=NavigableManager.getBrowsingContextById("+Json(page.browsing_context).dump()+");if(!bc||!owner)throw new Error('Firefox download owning context is closed');"
      "let owns=false;for(let n=bc;n;n=n.parent)if(n===owner){owns=true;break;}if(!owns)throw new Error('Firefox download does not belong to the requested page');"
      "const identity="+identity.dump()+";const {Downloads}=ChromeUtils.importESModule('resource://gre/modules/Downloads.sys.mjs');"
      "const list=await Downloads.getList(Downloads.ALL);const all=await list.getAll();if(all.length>4096)throw new Error('Firefox native download enumeration exceeds limits');"
      "const matches=all.filter(d=>d.source.browsingContextId===bc.id&&d.source.url===identity.url&&d.startTime?.getTime()===identity.timestamp&&"
      "(identity.path?d.target.path===identity.path:PathUtils.filename(d.target.path)===identity.filename));"
      "if(matches.length!==1)throw new Error(matches.length?'Ambiguous Firefox native download identity':'Native Firefox download identity is unavailable');"
      "const download=matches[0];if(download.succeeded||download.error)throw new Error('Firefox download is no longer in progress');"
      "await download.cancel();await download.removePartialData();if(!download.canceled||!download.stopped)throw new Error('Firefox did not cancel the selected native download');"
      "return {downloadId:identity.downloadId,canceled:download.canceled,stopped:download.stopped,receivedBytes:download.currentBytes};})()",deadline);
  }
  throw std::invalid_argument("Unsupported Firefox privileged native command: "+family+"/"+action);
}

void BidiPrivileged::forget(const std::string& context) {std::lock_guard lock(impl_->mutex);impl_->originals.erase(context);}
void BidiPrivileged::close(BidiRuntime& runtime,Deadline deadline) {
  std::unique_lock lock(impl_->mutex,std::defer_lock);
  if(!lock.try_lock_until(deadline))throw std::runtime_error("Firefox privileged cleanup deadline expired waiting for helper lock");
  impl_->closed=true;
  std::exception_ptr failure;
  for(auto it=impl_->originals.begin();it!=impl_->originals.end();) {
    try {
      BidiCleanupScope cleanup;
      BidiPage page;page.browsing_context=it->first;
      evaluate(runtime,page,"(()=>{"+lookup(it->first)+"if(!bc)return;const values="+it->second.dump()+";const errors=[];"
        "for(const [key,value]of Object.entries(values))try{bc[key]=value}catch(error){errors.push(String(error))}if(errors.length)throw new Error(errors.join('; '));})()",deadline);
      it=impl_->originals.erase(it);
    }catch(...){if(!failure)failure=std::current_exception();++it;}
  }
  if(failure)std::rethrow_exception(failure);
}
} // namespace suitecut::detail
