// SPDX-License-Identifier: MIT
#include "bidi_permissions.hpp"
#include "bidi_privileged.hpp"
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace suitecut::detail {
namespace {
Json descriptor(const std::string& permission) {
  // Only aliases with the same native semantics are portable. Firefox itself
  // validates descriptor support before any grant is dispatched.
  if(permission=="midiSysex"||permission=="midi-sysex")return Json{{"name","midi"},{"sysex",true}};
  const std::map<std::string,std::string> aliases{{"audioCapture","microphone"},{"videoCapture","camera"},{"durableStorage","persistent-storage"}};
  const auto found=aliases.find(permission);const auto name=found==aliases.end()?permission:found->second;
  static const std::set<std::string> names{"geolocation","notifications","camera","microphone","midi","persistent-storage","clipboard-read","clipboard-write","push","screen-wake-lock"};
  if(!names.contains(name))throw std::invalid_argument("Unsupported portable Firefox permission: "+permission);
  return Json{{"name",name}};
}
Json privileged(BidiRuntime& runtime,const std::string& expression,Deadline deadline) {
  BidiPage scope;
  return BidiPrivileged::evaluate(runtime,scope,expression,deadline);
}
std::string manager() {
  return "const {UserContextManager}=ChromeUtils.importESModule('chrome://remote/content/shared/UserContextManager.sys.mjs');";
}
void restore(BidiRuntime& runtime,const Json::array& records,Deadline deadline) {
  if(records.empty())return;
  privileged(runtime,"(()=>{"+manager()+"const records="+Json(records).dump()+";const errors=[];for(const r of records){try{"
    "const id=UserContextManager.getInternalIdById(r.user);if(id===null)continue;"
    "let p=Services.scriptSecurityManager.createContentPrincipalFromOrigin(r.origin);"
    "if(id)p=Services.scriptSecurityManager.principalWithOA(p,{userContextId:id});"
    "if(r.entry===null)Services.perms.removeFromPrincipal(p,r.type);"
    "else Services.perms.addFromPrincipal(p,r.type,r.entry.capability,r.entry.expireType,r.entry.expireTime);"
    "}catch(e){errors.push(String(e))}}if(errors.length)throw new Error('Firefox permission restoration failed: '+errors.join('; '));return {};})()",deadline);
}
}
struct BidiPermissions::Impl {
  struct Entry {int context=0;Json value;};
  std::timed_mutex mutex;
  std::map<std::string,Entry> originals;
  std::size_t original_bytes=2;
  bool closed=false;
};
BidiPermissions::BidiPermissions():impl_(std::make_unique<Impl>()){}
BidiPermissions::~BidiPermissions()=default;
Json BidiPermissions::command(BidiRuntime& runtime,int context,const std::string& user,
                              const std::string& action,const Json& params,Deadline deadline) {
  bidi_check_active(deadline);std::unique_lock lock(impl_->mutex,std::defer_lock);
  if(!lock.try_lock_until(deadline))throw std::runtime_error("Firefox permission helper lock deadline expired");
  if(impl_->closed)throw std::runtime_error("Firefox permission helper is closed");
  if(action=="clearPermissions") {
    Json::array records;for(const auto&[key,entry]:impl_->originals){(void)key;if(entry.context==context)records.push_back(entry.value);}
    restore(runtime,records,deadline);
    std::erase_if(impl_->originals,[&](const auto& item){return item.second.context==context;});
    impl_->original_bytes=2;for(const auto&[key,entry]:impl_->originals){(void)key;impl_->original_bytes+=entry.value.dump().size()+1;}
    return Json{};
  }
  if(action!="grantPermissions")throw std::invalid_argument("Unsupported Firefox permission action: "+action);
  for(const auto&[key,value]:params.as_object()) {
    (void)value;
    if(key!="contextId"&&key!="origin"&&key!="permissions")throw std::invalid_argument("Unsupported Firefox permission field: "+key);
  }
  const auto& input=params.at("permissions");
  if(!input.is_array()||input.as_array().size()>64)throw std::invalid_argument("Firefox permissions must be an array with at most64 entries");
  Json::array requested;std::set<std::string> unique;
  for(const auto& value:input.as_array()) {
    const auto parsed=descriptor(value.as_string());if(unique.insert(parsed.dump()).second)requested.push_back(parsed);
  }
  if(requested.empty())return Json{};
  std::string origin;
  if(params.contains("origin"))origin=params.at("origin").as_string();
  else {
    std::set<std::string> origins;
    for(auto&[id,page]:runtime.pages){(void)id;if(page.context==context)origins.insert(runtime.value(page,"location.origin",deadline).as_string());}
    if(origins.size()!=1||origins.contains("null"))throw std::invalid_argument("Firefox permissions require an explicit origin when the context has no unique nonopaque page origin");
    origin=*origins.begin();
  }
  if(origin.empty()||origin.size()>8192||origin.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid Firefox permission origin");
  // Prevalidate the whole list and snapshot exact stored entries in one native
  // operation. Querying navigator.permissions alone loses expiry/inheritance.
  const auto snapshot=privileged(runtime,"(()=>{"+manager()+
    "const url=new URL("+Json(origin).dump()+");if(!['http:','https:'].includes(url.protocol)||url.username||url.password)throw new Error('Firefox permissions require an HTTP(S) origin without credentials');"
    "const origin=url.origin,user="+Json(user).dump()+",id=UserContextManager.getInternalIdById(user);if(id===null)throw new Error('Firefox permission user context is closed');"
    "let p=Services.scriptSecurityManager.createContentPrincipalFromOrigin(origin);if(id)p=Services.scriptSecurityManager.principalWithOA(p,{userContextId:id});"
    "const requested="+Json(requested).dump()+",records=[];for(const descriptor of requested){const typed=navigator.permissions.parseSetParameters({descriptor,state:'granted'});"
    "if(descriptor.name==='storage-access')throw new Error('Firefox storage-access grants are unsupported');"
    "const existing=Services.perms.getPermissionObject(p,typed.type,true);records.push({descriptor,type:typed.type,origin,user,entry:existing?{capability:existing.capability,expireType:existing.expireType,expireTime:existing.expireTime}:null});}return records;})()",deadline);
  if(!snapshot.is_array()||snapshot.as_array().size()!=requested.size())throw std::runtime_error("Invalid Firefox permission snapshot");
  std::size_t additions=0,added_bytes=0;
  for(const auto& entry:snapshot.as_array()) {
    const auto key=std::to_string(context)+"\n"+entry.at("origin").as_string()+"\n"+entry.at("type").as_string();
    if(!impl_->originals.contains(key)){++additions;added_bytes+=entry.dump().size()+1;}
  }
  if(additions>4096-impl_->originals.size())throw std::runtime_error("Firefox permission restoration capacity exceeded");
  // Keep the entire restoration batch below the helper's 48 KiB authored-script
  // budget, so shutdown needs one native restore rather than an unbounded loop.
  if(added_bytes>32768-impl_->original_bytes)throw std::runtime_error("Firefox permission restoration byte budget exceeded");
  for(const auto& entry:snapshot.as_array()) {
    const auto key=std::to_string(context)+"\n"+entry.at("origin").as_string()+"\n"+entry.at("type").as_string();
    if(impl_->originals.try_emplace(key,Impl::Entry{context,entry}).second)impl_->original_bytes+=entry.dump().size()+1;
  }
  for(const auto& entry:snapshot.as_array())runtime.call("permissions.setPermission",Json{{"descriptor",entry.at("descriptor")},{"state","granted"},{"origin",entry.at("origin")},{"userContext",user}},deadline);
  return Json{};
}
void BidiPermissions::forget(int context) {
  std::unique_lock lock(impl_->mutex,std::defer_lock);
  if(!lock.try_lock_for(std::chrono::milliseconds{100}))throw std::runtime_error("Firefox permission retirement lock deadline expired");
  std::erase_if(impl_->originals,[&](const auto& item){return item.second.context==context;});
  impl_->original_bytes=2;for(const auto&[key,entry]:impl_->originals){(void)key;impl_->original_bytes+=entry.value.dump().size()+1;}
}
void BidiPermissions::close(BidiRuntime& runtime,Deadline deadline) {
  std::unique_lock lock(impl_->mutex,std::defer_lock);
  if(!lock.try_lock_until(deadline))throw std::runtime_error("Firefox permission cleanup lock deadline expired");
  impl_->closed=true;
  Json::array records;for(const auto&[key,entry]:impl_->originals){(void)key;records.push_back(entry.value);}
  BidiCleanupScope cleanup;restore(runtime,records,deadline);impl_->originals.clear();impl_->original_bytes=2;
}
}
