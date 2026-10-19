// SPDX-License-Identifier: MIT
#include "cookie_controls.h"
#include "locator_handler_async_work.h"
#include "http_cookie_projection.h"
#include "cookie_filter.h"
#include <cmath>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
namespace suitecut {
namespace {
using Dict=CefRefPtr<CefDictionaryValue>;
bool Site(const std::string& value){CefURLParts parts;if(value.size()>8192||!CefParseURL(value,parts))return false;const auto scheme=CefString(&parts.scheme).ToString();return (scheme=="http"||scheme=="https")&&!CefString(&parts.host).ToString().empty()&&CefString(&parts.username).ToString().empty()&&CefString(&parts.password).ToString().empty()&&CefString(&parts.query).ToString().empty()&&CefString(&parts.fragment).ToString().empty()&&(CefString(&parts.path).ToString().empty()||CefString(&parts.path).ToString()=="/");}
bool Matches(Dict cookie,Dict params){
  if(auto filter=params->GetDictionary("filter"))for(const auto* field:{"name","domain","path"})if(filter->GetType(field)==VTYPE_STRING&&filter->GetString(field)!=cookie->GetString(field))return false;
  const auto urls=params->GetList("urls");if(!urls||!urls->GetSize())return true;
  const auto domain=cookie->GetString("domain").ToString(),path=cookie->GetString("path").ToString();
  for(size_t i=0;i<urls->GetSize();++i){CefURLParts parts;if(!CefParseURL(urls->GetString(i),parts))continue;const auto host=CefString(&parts.host).ToString(),url_path=CefString(&parts.path).ToString(),scheme=CefString(&parts.scheme).ToString();const auto host_domain=!domain.empty()&&domain.front()=='.'?domain.substr(1):domain;const bool domain_match=host==host_domain||(!domain.empty()&&domain.front()=='.'&&host.size()>host_domain.size()&&host.ends_with("."+host_domain));const bool path_match=url_path.starts_with(path)&&(url_path.size()==path.size()||path.ends_with('/')||url_path[path.size()]=='/');if(domain_match&&path_match&&(!cookie->GetBool("secure")||scheme=="https"||HTTPLoopback(host)))return true;}
  return false;
}
bool Project(Dict raw,Dict& result,std::string& error){
  const auto cookies=raw?raw->GetList("cookies"):nullptr;if(!cookies||cookies->GetSize()>4096){error="Native cookie result entry capacity exceeded";return false;}auto output=CefListValue::Create();size_t bytes=0;
  for(size_t i=0;i<cookies->GetSize();++i){const auto source=cookies->GetDictionary(i);if(!source){error="Invalid actual browser cookie result";return false;}auto item=CefDictionaryValue::Create();for(const auto* field:{"name","value","domain","path"}){if(source->GetType(field)!=VTYPE_STRING){error="Invalid actual browser cookie string";return false;}item->SetString(field,source->GetString(field));bytes+=source->GetString(field).ToString().size();}for(const auto* field:{"secure","httpOnly"}){if(source->GetType(field)!=VTYPE_BOOL){error="Invalid actual browser cookie flag";return false;}item->SetBool(field,source->GetBool(field));}if(source->GetType("expires")!=VTYPE_DOUBLE&&source->GetType("expires")!=VTYPE_INT){error="Invalid actual browser cookie expiration";return false;}const double expires=source->GetType("expires")==VTYPE_INT?source->GetInt("expires"):source->GetDouble("expires");if(!std::isfinite(expires)){error="Invalid actual browser cookie expiration";return false;}item->SetDouble("expires",expires);const auto same=source->GetString("sameSite").ToString();if(!same.empty()&&same!="Strict"&&same!="Lax"&&same!="None"){error="Invalid actual browser cookie sameSite";return false;}item->SetString("sameSite",same.empty()?"Lax":same);
    if(source->GetBool("partitionKeyOpaque")){error="Opaque native cookie partition keys cannot be exported";return false;}
    if(source->HasKey("partitionKey")){const auto key=source->GetDictionary("partitionKey");if(!key||key->GetType("topLevelSite")!=VTYPE_STRING||key->GetType("hasCrossSiteAncestor")!=VTYPE_BOOL){error="Invalid actual browser cookie partition key";return false;}const auto site=key->GetString("topLevelSite").ToString();if(!Site(site)){error="Unsupported actual browser cookie partition site";return false;}item->SetString("partitionKey",site);item->SetBool("partitionKeyHasCrossSiteAncestor",key->GetBool("hasCrossSiteAncestor"));bytes+=site.size();}
    bytes+=256;if(bytes>1048576){error="Native cookie result byte capacity exceeded";return false;}output->SetDictionary(i,item);
  }result=CefDictionaryValue::Create();result->SetList("cookies",output);return true;
}
}
bool CookieControls::Partition(Dict input,std::string& error){
  if(!input)return false;
  if(input->HasKey("partitionKey")){if(input->GetType("partitionKey")!=VTYPE_STRING||!Site(input->GetString("partitionKey"))){error="Cookie partitionKey must be an HTTP(S) schemeful site";return false;}if(!input->GetBool("secure")){error="Partitioned cookies require secure:true";return false;}}
  if(input->HasKey("partitionKeyHasCrossSiteAncestor")&&(!input->HasKey("partitionKey")||input->GetType("partitionKeyHasCrossSiteAncestor")!=VTYPE_BOOL)){error="Cookie partition ancestor flag requires a partition key and boolean value";return false;}
  return true;
}
void CookieControls::Handle(int owner,int context,const std::string& action,Dict params,double deadline,Reply reply){
  if(!channel_||!owner||context<1||!params||jobs_.contains(owner)||jobs_.size()>=64){reply(false,nullptr,"Native cookie operation capacity or channel unavailable");return;}
  if(action!="cookies"&&action!="addCookies"&&action!="clearCookies"){reply(false,nullptr,"Unknown native cookie action");return;}
  bool regex=false;std::string filter_error;if(!ValidateCookieFilterParameters(params,action,regex,filter_error)){reply(false,nullptr,filter_error);return;}
  auto payload=CefDictionaryValue::Create();CookieMethod method=CookieMethod::Get;
  if(action=="addCookies"){
    const auto input=params->GetList("cookies");if(!input||input->GetSize()>(params->GetBool("storageInternal")?4096u:1024u)){reply(false,nullptr,"Native cookie input capacity exceeded");return;}auto cookies=CefListValue::Create();size_t bytes=0;
    for(size_t i=0;i<input->GetSize();++i){const auto original=input->GetDictionary(i);std::string error;if(!Partition(original,error)){reply(false,nullptr,error.empty()?"Invalid native cookie input":error);return;}auto cookie=original->Copy(false);if(cookie->HasKey("partitionKey")){auto key=CefDictionaryValue::Create();key->SetString("topLevelSite",cookie->GetString("partitionKey"));key->SetBool("hasCrossSiteAncestor",cookie->HasKey("partitionKeyHasCrossSiteAncestor")?cookie->GetBool("partitionKeyHasCrossSiteAncestor"):true);cookie->Remove("partitionKeyHasCrossSiteAncestor");cookie->SetDictionary("partitionKey",key);}if(cookie->HasKey("expires")&&(cookie->GetType("expires")==VTYPE_INT?cookie->GetInt("expires"):cookie->GetDouble("expires"))==-1)cookie->Remove("expires");if(!cookie->HasKey("sameSite"))cookie->SetString("sameSite","Lax");auto value=CefValue::Create();value->SetDictionary(cookie);bytes+=CefWriteJSON(value,JSON_WRITER_DEFAULT).length();if(bytes>1048576){reply(false,nullptr,"Native cookie input byte capacity exceeded");return;}cookies->SetDictionary(i,cookie);}payload->SetList("cookies",cookies);method=CookieMethod::Set;
  }else if(action=="clearCookies"){const auto filter=params->GetDictionary("filter");if(!filter||filter->GetSize()==0)method=CookieMethod::Clear;}
  jobs_.emplace(owner,Job{context,action,params->Copy(false),deadline,std::move(reply),nullptr});Send(owner,method,payload);
}
void CookieControls::MatchOwned(int owner,int context,Dict filter,CefRefPtr<CefListValue> cookies,double deadline,Reply reply){
  if(!channel_||owner<1||context<1||jobs_.contains(owner)||jobs_.size()>=64||!cookies||cookies->GetSize()>4096){reply(false,nullptr,"Native cookie matcher capacity or channel unavailable");return;}
  bool regex=false;std::string error;if(!ValidateCookieFilter(filter,regex,error)){reply(false,nullptr,error);return;}
  auto params=CefDictionaryValue::Create();if(filter)params->SetDictionary("filter",filter->Copy(false));auto selected=CefListValue::Create();size_t bytes=0;
  for(size_t i=0;i<cookies->GetSize();++i){const auto cookie=cookies->GetDictionary(i);if(!cookie||cookie->GetType("nativeIdentity")!=VTYPE_STRING){reply(false,nullptr,"Invalid native owned cookie identity");return;}for(const auto* field:{"name","domain","path"})if(cookie->GetType(field)!=VTYPE_STRING){reply(false,nullptr,"Invalid native owned cookie field");return;}if(!Matches(cookie,params))continue;auto item=CefDictionaryValue::Create();for(const auto* field:{"name","domain","path","nativeIdentity"})item->SetString(field,cookie->GetString(field));auto typed=CefValue::Create();typed->SetDictionary(item->Copy(false));const auto count=CefWriteJSON(typed,JSON_WRITER_DEFAULT).ToString().size();if(count>1048576-bytes){reply(false,nullptr,"Native owned cookie matcher byte capacity exceeded");return;}bytes+=count;selected->SetDictionary(selected->GetSize(),item);}
  jobs_.emplace(owner,Job{context,"matchOwned",params,deadline,std::move(reply),selected});FilterNext(owner);
}
void CookieControls::Send(int owner,CookieMethod method,Dict params){const auto found=jobs_.find(owner);if(found==jobs_.end())return;if(found->second.canceled){jobs_.erase(found);return;}channel_(owner,found->second.context,method,params,found->second.deadline,BindNativeLocatorHandlerAsync(Reply{[self=shared_from_this(),owner,method](bool ok,Dict result,const std::string& error){self->Received(owner,method,ok,result,error);}}));}
void CookieControls::Received(int owner,CookieMethod method,bool ok,Dict result,const std::string& error){const auto found=jobs_.find(owner);if(found==jobs_.end())return;if(found->second.canceled){jobs_.erase(found);return;}if(!ok){Complete(owner,false,nullptr,error);return;}auto& job=found->second;if(method==CookieMethod::Get){Dict projected;std::string failure;if(!Project(result,projected,failure)){Complete(owner,false,nullptr,failure);return;}const auto input=projected->GetList("cookies");auto matches=CefListValue::Create();for(size_t i=0;i<input->GetSize();++i){const auto item=input->GetDictionary(i);if(Matches(item,job.params))matches->SetDictionary(matches->GetSize(),item->Copy(false));}if(job.action=="clearCookies"){job.remove=matches;FilterNext(owner);return;}projected->SetList("cookies",matches);Complete(owner,true,projected,"");return;}if(method==CookieMethod::Filter){Filtered(owner,result);return;}if(method==CookieMethod::Delete){DeleteNext(owner);return;}Complete(owner,true,nullptr,"");}
void CookieControls::FilterNext(int owner){
  const auto found=jobs_.find(owner);if(found==jobs_.end())return;auto& job=found->second;
  bool regex=false;std::string error;if(!ValidateCookieFilter(job.params->GetDictionary("filter"),regex,error)){Complete(owner,false,nullptr,error);return;}
  if(!regex||!job.remove->GetSize()){SelectionReady(owner);return;}
  auto input=CefDictionaryValue::Create();input->SetDictionary("filter",job.params->GetDictionary("filter")->Copy(false));auto fields=CefListValue::Create();
  for(size_t i=0;i<job.remove->GetSize();++i){const auto cookie=job.remove->GetDictionary(i);auto item=CefDictionaryValue::Create();for(const auto*field:{"name","domain","path"})item->SetString(field,cookie->GetString(field));fields->SetDictionary(i,item);}
  input->SetList("cookies",fields);Send(owner,CookieMethod::Filter,input);
}
void CookieControls::Filtered(int owner,Dict result){
  const auto found=jobs_.find(owner);if(found==jobs_.end())return;auto& job=found->second;
  const auto remote=result?result->GetDictionary("result"):nullptr;const auto value=remote?remote->GetDictionary("value"):nullptr;const auto indices=value?value->GetList("matches"):nullptr;
  if(!result||result->HasKey("exceptionDetails")||!indices||!job.remove||indices->GetSize()>job.remove->GetSize()){Complete(owner,false,nullptr,"Native cookie regular expression evaluation failed");return;}
  auto selected=CefListValue::Create();int previous=-1;
  for(size_t i=0;i<indices->GetSize();++i){const int index=indices->GetInt(i);if(indices->GetType(i)!=VTYPE_INT||index<=previous||index<0||static_cast<size_t>(index)>=job.remove->GetSize()){Complete(owner,false,nullptr,"Invalid actual native cookie match result");return;}selected->SetDictionary(i,job.remove->GetDictionary(index)->Copy(false));previous=index;}
  job.remove=selected;SelectionReady(owner);
}
void CookieControls::SelectionReady(int owner){const auto found=jobs_.find(owner);if(found==jobs_.end())return;if(found->second.action!="matchOwned"){DeleteNext(owner);return;}auto output=CefDictionaryValue::Create();output->SetList("matches",found->second.remove->Copy());Complete(owner,true,output,"");}
void CookieControls::DeleteNext(int owner){const auto found=jobs_.find(owner);if(found==jobs_.end())return;auto& job=found->second;if(job.canceled){jobs_.erase(found);return;}if(!job.remove||job.next==job.remove->GetSize()){Complete(owner,true,nullptr,"");return;}const auto cookie=job.remove->GetDictionary(job.next++);auto params=CefDictionaryValue::Create();for(const auto* field:{"name","domain","path"})params->SetString(field,cookie->GetString(field));if(cookie->HasKey("partitionKey")){auto key=CefDictionaryValue::Create();key->SetString("topLevelSite",cookie->GetString("partitionKey"));key->SetBool("hasCrossSiteAncestor",cookie->GetBool("partitionKeyHasCrossSiteAncestor"));params->SetDictionary("partitionKey",key);}Send(owner,CookieMethod::Delete,params);}
void CookieControls::Complete(int owner,bool ok,Dict result,const std::string& error){const auto found=jobs_.find(owner);if(found==jobs_.end())return;auto reply=std::move(found->second.reply);jobs_.erase(found);if(reply)reply(ok,result,error);}
void CookieControls::Cancel(int owner){const auto found=jobs_.find(owner);if(found!=jobs_.end()){found->second.canceled=true;found->second.reply={};found->second.params=nullptr;found->second.remove=nullptr;}}
void CookieControls::Closed(int context){
  std::vector<int> owners;for(const auto&[owner,job]:jobs_)if(job.context==context)owners.push_back(owner);
  for(const int owner:owners){const auto found=jobs_.find(owner);if(found==jobs_.end())continue;auto& job=found->second;job.canceled=true;auto reply=std::move(job.reply);job.params=nullptr;job.remove=nullptr;if(reply)reply(false,nullptr,"Native cookie context closed");}
}
}
