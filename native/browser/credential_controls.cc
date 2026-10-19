// SPDX-License-Identifier: MIT
#include "credential_controls.h"
#include "locator_handler_async_work.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include "include/cef_parser.h"
namespace suitecut {
struct CredentialControls::Work{
  NativeLocatorHandlerAsyncWork handler_receipt;
  int owner=0,context=0,observed_page=0;std::string action,error;Dict params,result;Reply reply;
  std::vector<VirtualCredential>input;CredentialStore before,proposed;bool installed=false,restore_installed=false,before_installed=false,canceled=false,started=false,rollback=false,rollback_failed=false;
  size_t bytes=0;std::set<int>pending;std::shared_ptr<const CredentialPage::Config>config;
};
namespace {
bool Count(CefRefPtr<CefDictionaryValue>value,uint32_t&output,std::string&error){
  const auto type=value->GetType("signCount");if(type==VTYPE_INVALID){output=0;return true;}
  const double number=type==VTYPE_INT?value->GetInt("signCount"):type==VTYPE_DOUBLE?value->GetDouble("signCount"):-1;
  if(!std::isfinite(number)||std::floor(number)!=number||number<0||number>4294967295.0){std::ostringstream text;text<<number;error="signCount must be between 0 and 4294967295, got "+text.str();return false;}output=static_cast<uint32_t>(number);return true;
}
}
CredentialControls::Dict CredentialControls::Record(const VirtualCredential&item){auto value=CefDictionaryValue::Create();value->SetString("id",item.id);value->SetString("rpId",item.rp_id);value->SetString("userHandle",item.user_handle);value->SetString("privateKey",item.private_key);value->SetString("publicKey",item.public_key);value->SetDouble("signCount",item.sign_count);return value;}
bool CredentialControls::Parse(Dict value,VirtualCredential&item,std::string&error){
  if(!value){error="Invalid native WebAuthn credential";return false;}
  for(const char*key:{"id","rpId","userHandle","privateKey","publicKey"})if(value->GetType(key)!=VTYPE_STRING){error="Invalid native WebAuthn credential field: "+std::string(key);return false;}
  item.id=value->GetString("id");item.rp_id=value->GetString("rpId");item.user_handle=value->GetString("userHandle");item.private_key=value->GetString("privateKey");item.public_key=value->GetString("publicKey");return Count(value,item.sign_count,error)&&ValidateVirtualCredential(item,error);
}
size_t CredentialControls::Bytes(const std::vector<VirtualCredential>&items){size_t bytes=0;for(const auto&item:items)bytes+=CredentialStore::Bytes(item);return bytes;}
void CredentialControls::ContextCreated(int context){if(context>0&&!profiles_.contains(context)&&profiles_.size()<32)profiles_.emplace(context,Profile{});}
void CredentialControls::ContextClosed(int context){
  const auto found=profiles_.find(context);if(found==profiles_.end())return;const auto queue=found->second.queue;profiles_.erase(found);
  for(const auto&item:queue){const auto active=work_.find(item->owner);if(active==work_.end())continue;retained_bytes_-=item->bytes;work_.erase(active);auto reply=std::move(item->reply);if(reply&&!item->canceled)reply(false,nullptr,"Native credential context closed");}
}
std::shared_ptr<const CredentialPage::Config>CredentialControls::Config(int context)const{
  const auto found=profiles_.find(context);if(found==profiles_.end())return nullptr;
  if(!found->second.queue.empty()&&found->second.queue.front()->started&&found->second.queue.front()->config)return found->second.queue.front()->config;
  auto config=std::make_shared<CredentialPage::Config>();config->version=found->second.version;config->installed=found->second.installed;config->credentials=found->second.store.Get();return config;
}
CredentialControls::Dict CredentialControls::Capture(int context)const{auto result=CefDictionaryValue::Create();auto items=CefListValue::Create();const auto found=profiles_.find(context);if(found!=profiles_.end())for(const auto&item:found->second.store.Get())items->SetDictionary(items->GetSize(),Record(item));result->SetList("credentials",items);return result;}
bool CredentialControls::ValidateState(CefRefPtr<CefListValue>input,std::string&error)const{
  if(!input||input->GetSize()>CredentialStore::kMaxCredentials){error="Invalid native credential storage state or capacity exceeded";return false;}CredentialStore proposed;
  for(size_t index=0;index<input->GetSize();++index){VirtualCredential item;if(!Parse(input->GetDictionary(index),item,error)||!proposed.Add(std::move(item),error))return false;}return true;
}
void CredentialControls::Restore(int owner,int context,CefRefPtr<CefListValue>input,bool installed,Reply reply){
  std::string error;if(!ValidateState(input,error)){reply(false,nullptr,error);return;}
  auto item=std::make_shared<Work>();item->owner=owner;item->context=context;item->action="restore";item->restore_installed=installed;item->reply=std::move(reply);
  for(size_t index=0;index<input->GetSize();++index){VirtualCredential credential;if(!Parse(input->GetDictionary(index),credential,error)){item->reply(false,nullptr,error);return;}item->input.push_back(std::move(credential));}item->bytes=Bytes(item->input);Enqueue(item);
}
void CredentialControls::Handle(int owner,int context,const std::string&action,Dict params,Reply reply){
  if(action!="create"&&action!="get"&&action!="delete"&&action!="install"){reply(false,nullptr,"Unknown native credentials action");return;}
  if(!params){reply(false,nullptr,"Invalid native credentials parameters");return;}
  auto value=CefValue::Create();value->SetDictionary(params);const size_t parameter_bytes=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString().size();
  if(parameter_bytes>1048576){reply(false,nullptr,"Native credential parameters capacity exceeded");return;}
  auto item=std::make_shared<Work>();item->owner=owner;item->context=context;item->action=action;item->params=params;item->bytes=2*parameter_bytes;item->reply=std::move(reply);Enqueue(item);
}
void CredentialControls::Enqueue(std::shared_ptr<Work>item){
  if(closed_||item->owner==0||!profiles_.contains(item->context)||!hooks_.open(item->context)){item->reply(false,nullptr,"Native credential context is closed");return;}
  if(work_.contains(item->owner)||work_.size()>=64||item->bytes>32*1048576-retained_bytes_){item->reply(false,nullptr,"Native credential operation capacity exceeded");return;}
  retained_bytes_+=item->bytes;work_[item->owner]=item;profiles_.at(item->context).queue.push_back(item);Pump(item->context);
}
void CredentialControls::Pump(int context){const auto found=profiles_.find(context);if(found==profiles_.end()||found->second.queue.empty()||found->second.queue.front()->started)return;Start(found->second.queue.front());}
void CredentialControls::Start(std::shared_ptr<Work>item){
  const NativeWorkOriginFrame handler_origin(item->handler_receipt.Origin());
  const auto found=profiles_.find(item->context);if(found==profiles_.end())return;auto&profile=found->second;item->started=true;item->before=profile.store;item->proposed=profile.store;item->before_installed=profile.installed;
  if(item->action=="get"){auto result=Capture(item->context);const auto input=result->GetList("credentials");auto filtered=CefListValue::Create();const auto rp=item->params->GetString("rpId"),id=item->params->GetString("id");for(size_t index=0;index<input->GetSize();++index){const auto value=input->GetDictionary(index);if((rp.empty()||value->GetString("rpId")==rp)&&(id.empty()||value->GetString("id")==id))filtered->SetDictionary(filtered->GetSize(),value);}result->SetList("credentials",filtered);Finish(item,true,result,"");return;}
  std::string error;item->installed=profile.installed;
  if(item->action=="create"){
    if(item->params->GetType("rpId")!=VTYPE_STRING){Finish(item,false,nullptr,"Native credential rpId must be a string");return;}
    VirtualCredential credential;credential.rp_id=item->params->GetString("rpId");if(!Count(item->params,credential.sign_count,error)){Finish(item,false,nullptr,error);return;}
    size_t imported=0;for(const char*key:{"id","userHandle","privateKey","publicKey"})if(item->params->HasKey(key))++imported;
    if(imported){auto value=item->params->Copy(false);if(imported!=4||!Parse(value,credential,error)){Finish(item,false,nullptr,error.empty()?"Native credential import requires id, userHandle, privateKey and publicKey":error);return;}}
    else if(!GenerateVirtualCredential(credential.rp_id,credential.sign_count,credential,error)){Finish(item,false,nullptr,error);return;}
    if(!item->proposed.Add(credential,error)){Finish(item,false,nullptr,error);return;}item->result=Record(credential);
  }else if(item->action=="delete"){
    if(item->params->GetType("id")!=VTYPE_STRING){Finish(item,false,nullptr,"Native credential id must be a string");return;}item->proposed.Remove(item->params->GetString("id"));
  }else if(item->action=="install")item->installed=true;
  else if(item->action=="restore"){
    if(!item->proposed.Replace(item->input,error)){Finish(item,false,nullptr,error);return;}
    // Restored credential state installs authenticators before future documents.
    item->installed=item->restore_installed||profile.installed;
  }else if(item->action=="observed"){
    bool changed=false;
    for(const auto&credential:item->input){
      if(const auto previous=item->proposed.Find(credential.id)){
        if(previous->rp_id!=credential.rp_id||previous->private_key!=credential.private_key||previous->user_handle!=credential.user_handle){Finish(item,false,nullptr,"Native credential observation identity changed");return;}
        if(credential.sign_count>previous->sign_count){item->proposed.Asserted(credential.id,credential.sign_count);changed=true;}
      }else{
        if(!item->proposed.Add(credential,error)){Finish(item,false,nullptr,error);return;}
        changed=true;
      }
    }
    // A genuine event can queue behind another observation which commits the
    // same counter first. Recheck the committed store at dequeue time: there is
    // no native change to publish, clear or import for that superseded event.
    if(!changed){Finish(item,true,nullptr,"");return;}
  }
  if(profile.version>=static_cast<uint64_t>(std::numeric_limits<int>::max()-1)){Finish(item,false,nullptr,"Native credential version exhausted");return;}
  const size_t extra=3*(item->before.Bytes()+item->proposed.Bytes());if(extra>32*1048576-retained_bytes_){Finish(item,false,nullptr,"Native credential retained bytes capacity exceeded");return;}item->bytes+=extra;retained_bytes_+=extra;
  Dispatch(item,false);
}
void CredentialControls::Dispatch(std::shared_ptr<Work>item,bool rollback){
  const NativeWorkOriginFrame handler_origin(item->handler_receipt.Origin());
  const auto found=profiles_.find(item->context);if(found==profiles_.end()||!work_.contains(item->owner))return;auto&profile=found->second;
  if(profile.version>=static_cast<uint64_t>(std::numeric_limits<int>::max())){Finish(item,false,nullptr,"Native credential version exhausted");return;}
  item->rollback=rollback;auto config=std::make_shared<CredentialPage::Config>();config->version=++profile.version;config->installed=rollback?item->before_installed:item->installed;config->credentials=(rollback?item->before:item->proposed).Get();item->config=config;
  if(!(hooks_.publish_owned?hooks_.publish_owned(item->context,item->rollback?0:item->owner,config):hooks_.publish(item->context,config))){if(!rollback){item->error="Native credential snapshot publication failed";Dispatch(item,true);}else {if(hooks_.fatal)hooks_.fatal(item->context,item->error);Finish(item,false,nullptr,item->error);}return;}
  item->pending.clear();for(const int page:hooks_.pages(item->context)){
    if(!rollback&&item->action=="observed"&&page==item->observed_page&&hooks_.adopt_observed(page,config))continue;
    item->pending.insert(page);
  }
  if(item->pending.empty()){
    if(!rollback&&!item->canceled){profile.store=item->proposed;profile.installed=item->installed;Finish(item,true,item->result,"");}else Finish(item,false,nullptr,item->error.empty()?"Native credential operation canceled":item->error);return;
  }
  const auto pages=item->pending;
  for(const int page:pages){if(!work_.contains(item->owner)||item->rollback!=rollback)break;hooks_.apply(page,item->owner,config,[weak=weak_from_this(),item,page,rollback](bool ok,const std::string&error){if(const auto self=weak.lock())if(item->rollback==rollback)self->Settled(item,page,ok,error);});}
}
void CredentialControls::Settled(std::shared_ptr<Work>item,int page,bool ok,const std::string&error){
  const NativeWorkOriginFrame handler_origin(item->handler_receipt.Origin());
  if(!work_.contains(item->owner)||!item->pending.erase(page))return;
  const auto current_pages=hooks_.pages(item->context);if(!ok&&std::find(current_pages.begin(),current_pages.end(),page)!=current_pages.end()){if(item->rollback)item->rollback_failed=true;if(item->error.empty())item->error=error;}
  if(!item->pending.empty())return;
  if(!item->rollback&&(item->canceled||!item->error.empty())){Dispatch(item,true);return;}
  const auto found=profiles_.find(item->context);if(found==profiles_.end())return;
  if(item->rollback){if(item->rollback_failed&&hooks_.fatal)hooks_.fatal(item->context,item->error);Finish(item,false,nullptr,item->error.empty()?"Native credential operation canceled":item->error);}
  else{found->second.store=item->proposed;found->second.installed=item->installed;Finish(item,true,item->result,"");}
}
void CredentialControls::Finish(std::shared_ptr<Work>item,bool ok,Dict result,const std::string&error){
  const NativeWorkOriginFrame handler_origin(item->handler_receipt.Origin());
  const auto active=work_.find(item->owner);if(active==work_.end())return;retained_bytes_-=item->bytes;work_.erase(active);
  const auto profile=profiles_.find(item->context);if(profile!=profiles_.end()){auto&queue=profile->second.queue;const auto found=std::find(queue.begin(),queue.end(),item);if(found!=queue.end())queue.erase(found);}
  auto reply=std::move(item->reply);if(reply&&!item->canceled)reply(ok,result,error);Pump(item->context);
}
void CredentialControls::Cancel(int owner){
  const auto found=work_.find(owner);if(found==work_.end())return;const auto item=found->second;item->canceled=true;item->reply=nullptr;
  if(!item->started){Finish(item,false,nullptr,"");return;}for(const int page:item->pending)hooks_.cancel(page,owner);
}
bool CredentialControls::Observed(int context,int page,const VirtualCredential&credential,std::string&error){
  const auto found=profiles_.find(context);if(found==profiles_.end()||!found->second.installed)return true;
  if(!ValidateVirtualCredential(credential,error))return false;
  if(const auto previous=found->second.store.Find(credential.id)){
    if(previous->rp_id!=credential.rp_id||previous->private_key!=credential.private_key||previous->user_handle!=credential.user_handle){error="Native credential observation identity changed";return false;}
    if(previous->sign_count>=credential.sign_count)return true;
  }
  if(work_.size()>=64||next_observation_==std::numeric_limits<int>::min()||Bytes({credential})>32*1048576-retained_bytes_){error="Native credential observation capacity exceeded";return false;}
  auto item=std::make_shared<Work>();item->owner=next_observation_--;item->context=context;item->observed_page=page;item->action="observed";item->input={credential};item->bytes=Bytes(item->input);
  item->reply=[weak=weak_from_this(),context](bool ok,Dict,const std::string&error){if(!ok)if(const auto self=weak.lock())if(self->hooks_.fatal)self->hooks_.fatal(context,error);};Enqueue(item);return true;
}
void CredentialControls::Clear(){closed_=true;std::vector<int>contexts;for(const auto&[id,profile]:profiles_)contexts.push_back(id);for(const int context:contexts)ContextClosed(context);}
void CredentialControls::Diagnostics(Dict output)const{size_t count=0,bytes=0;for(const auto&[id,profile]:profiles_){count+=profile.store.Size();bytes+=profile.store.Bytes();}output->SetInt("nativeCredentials",static_cast<int>(count));output->SetDouble("nativeCredentialBytes",static_cast<double>(bytes));output->SetInt("nativeCredentialOperations",static_cast<int>(work_.size()));output->SetDouble("nativeCredentialRetainedBytes",static_cast<double>(retained_bytes_));}
}
