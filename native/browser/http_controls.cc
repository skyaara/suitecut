// SPDX-License-Identifier: MIT
#include "http_controls.h"
#include "route_fetch_policy.h"
#include "accepted_task_work.h"
#include "locator_handler_async_work.h"
#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstring>
#include "include/cef_task.h"
#include "include/cef_request_context_handler.h"
#include "include/cef_cookie.h"
#include "http_cookie.h"
#include "api_cookie_jar_cef.h"
#include "cookie_filter.h"
#include "http_header_text.h"
#include "http_cookie_projection.h"
#include "http_cookie_creation.h"
#include "http_transport_error_cef.h"
#include "http_retry_policy.h"
#include <set>
#include <openssl/rand.h>
#include "http_multipart.h"

namespace {
using suitecut::HTTPHeaderText;
SuiteCutHTTPControls::Dict HTTPTraceHeaders(const CefRequest::HeaderMap& pairs){auto headers=CefDictionaryValue::Create();for(const auto& [name,value]:pairs){if(!headers->HasKey(name))headers->SetString(name,value);else{auto values=headers->GetList(name);if(!values){values=CefListValue::Create();values->SetString(0,headers->GetString(name));headers->SetList(name,values);}values->SetString(values->GetSize(),value);}}return headers;}
class HTTPTask : public CefTask {
 public:explicit HTTPTask(std::function<void()> callback):callback_(std::move(callback)){}void Execute() override{const suitecut::AcceptedTaskFrame native_frame(native_work_);callback_();}
 private:suitecut::AcceptedTaskReceipt native_work_;std::function<void()> callback_;IMPLEMENT_REFCOUNTING(HTTPTask);
};
class HTTPVisitor : public CefCookieVisitor {
 public:
  using Done=std::function<void(std::vector<std::string>,std::string)>;
  explicit HTTPVisitor(Done done):done_(suitecut::BindNativeLocatorHandlerAsync(std::move(done))){}
  ~HTTPVisitor() override {const auto done=std::move(done_);CefPostTask(TID_UI,new HTTPTask([done,cookies=std::move(cookies_),error=std::move(error_)]() mutable {done(std::move(cookies),std::move(error));}));}
  bool Visit(const CefCookie& cookie,int,int,bool&) override {
    const auto domain=CefString(&cookie.domain).ToString(),path=CefString(&cookie.path).ToString(),name=CefString(&cookie.name).ToString(),value=CefString(&cookie.value).ToString();
    double expires=0;if(cookie.has_expires){cef_time_t time{};cef_time_from_basetime(cookie.expires,&time);cef_time_to_doublet(&time,&expires);}
    auto record=std::string(cookie.httponly?"#HttpOnly_":"")+domain+"\t"+(!domain.empty() && domain.front()=='.'?"TRUE":"FALSE")+"\t"+path+"\t"+(cookie.secure?"TRUE":"FALSE")+"\t"+std::to_string(static_cast<int64_t>(expires))+"\t"+name+"\t"+value;
    if(cookies_.size()>=4096 || record.size()>1048576-bytes_){error_="Native HTTP cookie snapshot limit exceeded";return false;}
    bytes_+=record.size();cookies_.push_back(std::move(record));return true;
  }
  void Reject(){error_="CEF cookie storage is unavailable";}
 private:Done done_;std::vector<std::string> cookies_;size_t bytes_=0;std::string error_;IMPLEMENT_REFCOUNTING(HTTPVisitor);
};
class HTTPSaveCallback : public CefSetCookieCallback {
 public:explicit HTTPSaveCallback(std::function<void()> callback):callback_(suitecut::BindNativeLocatorHandlerAsync(std::move(callback))){}void OnComplete(bool) override {if(!complete_.exchange(true))callback_();}
 private:std::function<void()> callback_;std::atomic<bool> complete_{false};IMPLEMENT_REFCOUNTING(HTTPSaveCallback);
};
}
std::string SuiteCutHTTPControls::Lower(std::string value) {for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
bool SuiteCutHTTPControls::URL(const std::string& url) {CefURLParts parts;return url.size()<=8192 && CefParseURL(url,parts) && (CefString(&parts.scheme)=="http" || CefString(&parts.scheme)=="https") && parts.host.length;}
std::string SuiteCutHTTPControls::Origin(const std::string& url) {CefURLParts parts;if(!CefParseURL(url,parts))return "";return CefString(&parts.scheme).ToString()+"://"+CefString(&parts.host).ToString()+":"+(parts.port.length?CefString(&parts.port).ToString():CefString(&parts.scheme)=="https"?"443":"80");}
bool SuiteCutHTTPControls::Headers(CefRefPtr<CefListValue> input,CefRequest::HeaderMap& output) {
  if(!input)return true;
  if(input->GetSize()>1024)return false;
  size_t bytes=0;
  for(size_t i=0;i<input->GetSize();++i) {
    auto entry=input->GetDictionary(i);if(!entry || entry->GetType("name")!=VTYPE_STRING || entry->GetType("value")!=VTYPE_STRING)return false;
    const auto name=entry->GetString("name").ToString(),value=entry->GetString("value").ToString();bytes+=name.size()+value.size();
    if(name.empty() || bytes>65536 || value.find_first_of("\r\n")!=std::string::npos || value.find('\0')!=std::string::npos)return false;
    for(const unsigned char c:name)if(c<=32 || c>=127 || std::string("()<>@,;:\\\"/[]?={}").find(static_cast<char>(c))!=std::string::npos)return false;
    std::string wire_value;if(!suitecut::HTTPEncodeHeaderValue(entry->GetString("value").ToString16(),wire_value))return false;
    output.emplace(name,value);
  }
  return true;
}
void SuiteCutHTTPControls::Remove(CefRequest::HeaderMap& target,const std::string& name) {for(auto it=target.begin();it!=target.end();)if(Lower(it->first.ToString())==name)it=target.erase(it);else ++it;}
void SuiteCutHTTPControls::Merge(CefRequest::HeaderMap& target,const CefRequest::HeaderMap& extra) {std::set<std::string> names;for(const auto& [name,value]:extra)names.insert(Lower(name.ToString()));for(const auto& name:names)Remove(target,name);target.insert(extra.begin(),extra.end());}
void SuiteCutHTTPControls::RouteFetch(int owner,int page,Dict request,Dict options,RouteFetchReply reply,double deadline,std::shared_ptr<void> physical_lease) {
  if(closed_){reply(false,nullptr,"Native HTTP engine is closed");return;}
  const auto browser=hooks_.page_context?hooks_.page_context(page):BrowserContext{};
  RouteFetchContext(owner,browser,request,options,std::move(reply),deadline,std::move(physical_lease));
}
void SuiteCutHTTPControls::RouteFetchContext(int owner,BrowserContext browser,Dict request,Dict options,RouteFetchReply reply,double deadline,std::shared_ptr<void> physical_lease) {
  if(closed_){reply(false,nullptr,"Native HTTP engine is closed");return;}
  const auto actual=browser.id>0&&hooks_.browser_context?hooks_.browser_context(browser.id):BrowserContext{};
  if(!suitecut::NativeRouteFetchContextMatches(browser,actual)){reply(false,nullptr,"Native route fetch actual browser context is unavailable");return;}
  std::string error;const auto cap=suitecut::NativeRouteFetchDeadline(options,hooks_.now(),deadline,deadline,error);
  if(!cap){reply(false,nullptr,error);return;}deadline=*cap;
  auto params=suitecut::NativeRouteFetchRequest(request,options,hooks_.now(),deadline,deadline,error);
  if(!params){reply(false,nullptr,error);return;}
  auto dispatch=[weak=weak_from_this(),owner,params,reply=std::move(reply),deadline,physical_lease=std::move(physical_lease),browser_context=browser.id](bool ok,Dict created,const std::string& failure)mutable{
    const auto self=weak.lock();if(!self)return;
    if(!ok||!created){reply(false,nullptr,failure);return;}
    const int id=created->GetInt("apiContextId");self->route_contexts_[browser_context]=id;params->SetInt("apiContextId",id);
    self->Handle(owner,params,[weak,context=id,reply=std::move(reply),browser_context](bool accepted,Dict result,const std::string& error){
      if(accepted&&result){result=result->Copy(false);result->SetInt("browserContextId",browser_context);}
      const bool owned=reply(accepted,result,error);
      // The actual HTTP response exists, but an expired/canceled native pause
      // cannot expose or dispose it. Reclaim only this exact native cache row.
      if(accepted&&result&&!owned)if(const auto engine=weak.lock()){const auto found=engine->responses_.find(result->GetString("responseId"));if(found!=engine->responses_.end()&&found->second.context==context){engine->body_bytes_-=found->second.body.size();engine->responses_.erase(found);}}
    },deadline,std::move(physical_lease));
  };
  const auto cached=route_contexts_.find(browser.id);
  const int context_id=cached==route_contexts_.end()?0:cached->second;
  const auto context=contexts_.find(context_id);
  if(context!=contexts_.end()&&!context->second.closing&&context->second.cef==browser.cef){auto ready=CefDictionaryValue::Create();ready->SetInt("apiContextId",context_id);dispatch(true,ready,"");return;}
  route_contexts_.erase(browser.id);
  // Browser-linked creation is synchronous and uses the genuine native CEF
  // context. No SDK setup request or separately reset fetch budget is needed.
  auto create=CefDictionaryValue::Create();create->SetString("action","create");create->SetString("scope","context");create->SetInt("browserContextId",browser.id);
  Handle(owner,create,std::move(dispatch),deadline);
}
void SuiteCutHTTPControls::Handle(int owner,Dict params,Reply reply,double original_deadline,std::shared_ptr<void> physical_lease) {
  reply=suitecut::BindNativeLocatorHandlerAsync(std::move(reply));
  if(closed_){reply(false,nullptr,"Native HTTP engine is closed");return;}
  const auto action=params->GetString("action").ToString();
  if(action=="create") {
    if(contexts_.size()>=64){reply(false,nullptr,"Native API request context limit exceeded");return;}
    Context context;
    if(params->GetString("scope")=="page") {const auto browser=hooks_.page_context(params->GetInt("pageId"));context.cef=browser.cef;context.browser_context=browser.id;}
    else if(params->GetString("scope")=="context") {const auto browser=hooks_.browser_context(params->GetInt("browserContextId"));context.cef=browser.cef;context.browser_context=browser.id;}
    const bool independent=params->GetString("scope")=="independent";
    if(!context.cef&&!independent){reply(false,nullptr,"Native API request browser context is unavailable");return;}
    if(!Headers(params->GetList("headers"),context.headers)){reply(false,nullptr,"Invalid character in header content or invalid native API request header");return;}
    if(hooks_.default_user_agent)context.policy.user_agent=hooks_.default_user_agent();
    std::string policy_error;if(context.browser_context&&hooks_.context_policy){const auto inherited=hooks_.context_policy(context.browser_context);if(inherited&&!suitecut::NativeHTTPRequestPolicy::Parse(inherited,context.policy,policy_error)){reply(false,nullptr,policy_error);return;}}
    if(!suitecut::NativeHTTPRequestPolicy::Parse(params,context.policy,policy_error)){reply(false,nullptr,policy_error);return;}
    context.policy_overrides=params->Copy(false);
    // Parsed trust anchors are immutable context state, not reparsed on each request.
    context.policy_overrides->Remove("extraCACertificates");
    const auto initialized=params->HasKey("initializeStorage")?params->GetBool("initializeStorage"):false;
    if(params->HasKey("initializeStorage")&&params->GetType("initializeStorage")!=VTYPE_BOOL){reply(false,nullptr,"Invalid native API creation state");return;}
    const BrowserContext associated{context.cef,context.browser_context};
    auto publish=[weak=weak_from_this(),context=std::move(context),reply=std::move(reply),independent,initialized,original_deadline](BrowserContext browser,const std::string&error)mutable{
      const auto self=weak.lock();if(!self)return;
      if(!error.empty()||!browser.cef||self->closed_){if(independent&&browser.id&&self->hooks_.close_context)self->hooks_.close_context(browser.id,[](bool,Dict,const std::string&){});reply(false,nullptr,error.empty()?"Native HTTP context creation was canceled":error);return;}
      context.cef=browser.cef;context.browser_context=browser.id;context.owned=independent;if(independent)context.jar=std::make_shared<suitecut::NativeAPICookieJar>(self->jar_budget_);context.initializing=initialized;context.creation_deadline=original_deadline;
      const int id=++self->next_context_;self->contexts_[id]=std::move(context);auto result=CefDictionaryValue::Create();result->SetInt("apiContextId",id);result->SetInt("browserContextId",browser.id);reply(true,result,"");if(initialized)self->hooks_.wake();
    };
    if(independent){if(!hooks_.create_context){publish({},"Native managed HTTP contexts are unavailable");return;}hooks_.create_context(std::move(publish));}
    else publish(associated,"");
    return;
  }
  if(action=="resolveLookup"){
    const auto input=params->GetList("addresses");std::vector<suitecut::HTTPLookupControls::Address>addresses;
    if(input&&input->GetSize()<=16)for(size_t i=0;i<input->GetSize();++i){const auto value=input->GetDictionary(i);if(!value||value->GetType("address")!=VTYPE_STRING||value->GetType("family")!=VTYPE_INT){reply(false,nullptr,"Malformed native HTTP lookup answer");return;}addresses.push_back({value->GetString("address"),value->GetInt("family")});}
    const double generation=params->GetType("lookupGeneration")==VTYPE_INT?params->GetInt("lookupGeneration"):params->GetDouble("lookupGeneration");
    if(!std::isfinite(generation)||generation<1||generation>9007199254740991.0||generation!=std::floor(generation)){reply(false,nullptr,"Invalid native HTTP lookup generation");return;}
    const bool accepted=lookups_.Resolve(params->GetInt("lookupOwner"),params->GetInt("apiContextId"),static_cast<uint64_t>(generation),hooks_.now(),std::move(addresses),params->GetString("error").ToString());reply(accepted,CefDictionaryValue::Create(),accepted?"":"Native HTTP lookup no longer owns this request");return;
  }
  const int context_id=params->GetInt("apiContextId");const auto context=contexts_.find(context_id);
  if(action=="dispose") {BeginDispose(context_id,std::move(reply),owner);return;}
  if(context==contexts_.end()){reply(false,nullptr,"API request context was disposed or is unknown");return;}
  if(context->second.closing){reply(false,nullptr,"API request context has been closed");return;}
  if(action=="initializeStorage"){
    const auto method=params->GetString("storageAction").ToString();
    if(!context->second.initializing||(method!="storageUploadBegin"&&method!="storageUploadAppend"&&method!="storageUploadDiscard"&&method!="setStorageState")){reply(false,nullptr,"Invalid native HTTP storage initialization");return;}
    auto config=params->GetDictionary("params");ScopeCommandStart(owner,context_id,method,config?config->Copy(false):CefDictionaryValue::Create(),std::move(reply),method=="setStorageState",original_deadline);return;
  }
  if(context->second.initializing){reply(false,nullptr,"Native HTTP context storage initialization is incomplete");return;}
  if(action=="cookies"||action=="addCookies"||action=="clearCookies"){ScopeCommandStart(owner,context_id,action,params->Copy(false),std::move(reply),false,original_deadline);return;}
  if(action=="contextStorage"){
    const auto method=params->GetString("storageAction").ToString();if(method!="storageState"){reply(false,nullptr,"Invalid native API storage export");return;}
    auto config=params->GetDictionary("params");ScopeCommandStart(owner,context_id,method,config?config->Copy(false):CefDictionaryValue::Create(),std::move(reply),false,original_deadline);return;
  }
  if(action=="tracing"){if(!tracing_||params->GetType("params")!=VTYPE_DICTIONARY){reply(false,nullptr,"Native HTTP tracing is unavailable or invalid");return;}tracing_->Handle(owner,context_id,params->GetDictionary("params"),original_deadline,std::move(reply));return;}
  if(action=="storageState"){ScopeCommandStart(owner,context_id,"storageState",params->Copy(false),std::move(reply),false,original_deadline);return;}
  if(action=="body" || action=="disposeResponse") {
    const auto id=params->GetString("responseId").ToString();const auto response=responses_.find(id);
    if(response==responses_.end() || response->second.context!=context_id){reply(false,nullptr,"Response has been disposed or is unknown");return;}
    auto result=CefDictionaryValue::Create();
    if(action=="body")result->SetString("body",CefBase64Encode(response->second.body.data(),response->second.body.size()));
    else {body_bytes_-=response->second.body.size();responses_.erase(response);}
    reply(true,result,"");return;
  }
  if(action=="appendPreparedMultipart"||action=="commitMultipart"){PreparedMultipart(owner,context_id,params,std::move(reply));return;}
  if(action=="upload") {
    const auto id=params->GetString("uploadId").ToString(),body=params->GetString("body").ToString();
    if(id.empty() || id.size()>128 || preparations_.Contains(id) || body.size()>32768 || (!uploads_.contains(id) && uploads_.size()>=16)){reply(false,nullptr,"Native API upload limit exceeded");return;}
    const auto data=CefBase64Decode(body);if(!data){reply(false,nullptr,"Invalid API body base64");return;}
    auto& upload=uploads_[id];
    if(upload.context && upload.context!=context_id){reply(false,nullptr,"API upload belongs to another context");return;}
    upload.context=context_id;upload.updated=hooks_.now();hooks_.wake();
    const size_t bytes=data->GetSize();std::string error;
    if(!suitecut::AppendNativeHTTPUpload(upload.bytes,bytes,RetainedUploadCapacity(),kMaxUpload,kUploadBudget,[data](uint8_t*target,size_t size){return data->GetData(target,size,0)==size;},error)){reply(false,nullptr,error);return;}
    upload_bytes_+=bytes;
    reply(true,CefDictionaryValue::Create(),"");return;
  }
  if(action!="fetch"&&action!="fetchMultipart"&&action!="prepareMultipart"){reply(false,nullptr,"Unsupported native HTTP action");return;}
  if((action=="fetchMultipart")!=params->HasKey("multipartParts")){reply(false,nullptr,"Native multipart fields require the explicit multipart fetch operation");return;}
  if(active_.size()+storage_queries_.size()>=64 || active_.contains(owner) || storage_queries_.contains(owner)){reply(false,nullptr,"Native HTTP request operation limit exceeded");return;}
  auto state=std::make_shared<State>();state->owner=owner;state->context=context_id;state->generation=++generation_;state->reply=std::move(reply);state->physical_lease=std::move(physical_lease);
  state->url=params->GetString("url").ToString();state->method=params->GetString("method").ToString();state->remaining_redirects=params->GetInt("maxRedirects");state->fail_status=params->GetBool("failOnStatusCode");state->follow_redirects=state->remaining_redirects>0;
  if(params->HasKey("apiName")){const auto name=params->GetString("apiName").ToString();if(name!="fetch"&&name!="get"&&name!="head"&&name!="post"&&name!="put"&&name!="patch"&&name!="delete"){state->reply(false,nullptr,"Invalid native API operation name");return;}state->api_name=name;}
  state->deadline=original_deadline;
  if(params->HasKey("lookupKey")){state->lookup_key=params->GetString("lookupKey");if(params->GetType("lookupKey")!=VTYPE_STRING||state->lookup_key.empty()||state->lookup_key.size()>128||!hooks_.lookup_event){state->reply(false,nullptr,"Invalid or unavailable native HTTP lookup provider");return;}}
  state->policy=context->second.policy;
  if(!context->second.owned&&context->second.browser_context&&hooks_.context_policy){
    suitecut::NativeHTTPRequestPolicy current;current.extra_ca=context->second.policy.extra_ca;if(hooks_.default_user_agent)current.user_agent=hooks_.default_user_agent();
    std::string error;const auto inherited=hooks_.context_policy(context->second.browser_context);
    if(!inherited||!suitecut::NativeHTTPRequestPolicy::Parse(inherited,current,error)||!suitecut::NativeHTTPRequestPolicy::Parse(context->second.policy_overrides,current,error)){state->reply(false,nullptr,error.empty()?"Native API browser context policy is unavailable":error);return;}
    state->policy=std::move(current);
  }
  state->timeout_ms=params->HasKey("timeoutMs")?params->GetInt("timeoutMs"):30000;
  state->retries=params->GetInt("maxRetries");if((params->HasKey("maxRetries")&&params->GetType("maxRetries")!=VTYPE_INT)||state->retries<0||state->retries>100){state->reply(false,nullptr,"Invalid native API retry limit");return;}
  state->ignore_https_errors=state->policy.ignore_https_errors;
  if(params->HasKey("ignoreHTTPSErrors")){if(params->GetType("ignoreHTTPSErrors")!=VTYPE_BOOL){state->reply(false,nullptr,"Native API ignoreHTTPSErrors must be boolean");return;}state->ignore_https_errors=params->GetBool("ignoreHTTPSErrors");}
  if(!URL(state->url) || state->method.empty() || state->method.size()>32 || state->remaining_redirects<0 || state->remaining_redirects>100){state->reply(false,nullptr,"Invalid native API request URL, method or redirect limit");return;}
  for(const unsigned char c:state->method)if(c<=32 || c>=127){state->reply(false,nullptr,"Invalid native API request method");return;}
  if(context->second.browser_context&&hooks_.context_headers){const auto inherited=hooks_.context_headers(context->second.browser_context);if(inherited){CefDictionaryValue::KeyList keys;inherited->GetKeys(keys);for(const auto&name:keys)state->headers.emplace(name,inherited->GetString(name));}}
  Merge(state->headers,context->second.headers);CefRequest::HeaderMap overrides;
  if(!Headers(params->GetList("headers"),overrides)){state->reply(false,nullptr,"Invalid character in header content or invalid native API request header");return;}
  Merge(state->headers,overrides);
  bool has_agent=false;for(const auto&[name,value]:state->headers){(void)value;if(Lower(name.ToString())=="user-agent")has_agent=true;}
  if(!has_agent&&!state->policy.user_agent.empty())state->headers.emplace("User-Agent",state->policy.user_agent);
  if(action=="prepareMultipart"){
    const auto token=params->GetString("uploadId").ToString();
    if(params->HasKey("body")||params->HasKey("multipartParts")||uploads_.contains(token)||!preparations_.Reserve(token,{owner,context_id,state->generation,state->deadline},hooks_.now())){state->reply(false,nullptr,"Invalid or excessive native multipart preparation");return;}
    state->preparing=true;state->stream_prepared=true;state->preparation_token=token;active_[owner]=state;
    if(tracing_)tracing_->Before(owner,context_id,params);
    hooks_.wake();return;
  }
  if(params->HasKey("body")) {
    const auto encoded=params->GetString("body").ToString();auto body=encoded.empty()?nullptr:CefBase64Decode(encoded);
    if((!encoded.empty() && !body) || (body && body->GetSize()>kMaxUpload)){state->reply(false,nullptr,"Invalid native API body");return;}
    if(body){const auto retained=RetainedUploadCapacity();if(retained>kUploadBudget||body->GetSize()>kUploadBudget-retained){state->reply(false,nullptr,"Native API active upload capacity exceeded");return;}state->upload.resize(body->GetSize());if(state->upload.capacity()>kUploadBudget-retained){state->reply(false,nullptr,"Native API active upload capacity exceeded");return;}body->GetData(state->upload.data(),state->upload.size(),0);}
  } else if(params->HasKey("uploadId")) {
    const auto upload=uploads_.find(params->GetString("uploadId").ToString());
    if(upload==uploads_.end() || upload->second.context!=context_id){state->reply(false,nullptr,"Native API upload is unavailable");return;}
    upload_bytes_-=upload->second.bytes.size();state->upload=std::move(upload->second.bytes);uploads_.erase(upload);
  }
  if(params->HasKey("multipartParts")){std::string error;if(!Multipart(state,params->GetList("multipartParts"),kUploadBudget-upload_bytes_-active_upload_bytes_,error)){state->reply(false,nullptr,error);return;}}
  const auto retained_upload=RetainedUploadCapacity();
  if(retained_upload>kUploadBudget||state->upload.capacity()>kUploadBudget-retained_upload||state->upload.size()>kUploadBudget-upload_bytes_-active_upload_bytes_){state->reply(false,nullptr,"Native API active upload budget exceeded");return;}
  Remove(state->headers,"content-length");
  active_upload_bytes_+=state->upload.size();active_[owner]=state;if(tracing_)tracing_->Before(owner,context_id,params);Start(state);
}
bool SuiteCutHTTPControls::Multipart(const std::shared_ptr<State>&state,CefRefPtr<CefListValue>entries,size_t available,std::string&error){
    std::vector<suitecut::NativeHTTPMultipartPart>parts;
    if(!entries||entries->GetSize()>1024){error="Invalid or excessive native multipart parts";return false;}
    for(size_t index=0;index<entries->GetSize();++index){
      const auto item=entries->GetDictionary(index);if(!item||item->GetType("name")!=VTYPE_STRING||item->GetType("offset")!=VTYPE_INT||item->GetType("size")!=VTYPE_INT||item->GetInt("offset")<0||item->GetInt("size")<0||(item->HasKey("filename")&&item->GetType("filename")!=VTYPE_STRING)||(item->HasKey("mimeType")&&item->GetType("mimeType")!=VTYPE_STRING)){error="Malformed native multipart field";return false;}
      suitecut::NativeHTTPMultipartPart part;part.name=item->GetString("name");part.offset=size_t(item->GetInt("offset"));part.size=size_t(item->GetInt("size"));part.file=item->HasKey("filename");part.filename=item->GetString("filename");part.mime=item->GetString("mimeType");if(item->HasKey("inferMime")){if(item->GetType("inferMime")!=VTYPE_BOOL||!part.file){error="Invalid native multipart MIME inference";return false;}if(item->GetBool("inferMime")&&part.mime.empty()){const auto dot=part.filename.find_last_of('.');if(dot!=std::string::npos&&dot+1<part.filename.size())part.mime=CefGetMimeType(part.filename.substr(dot+1)).ToString();}}parts.push_back(std::move(part));
    }
    std::string boundary;bool safe=false;
    for(size_t attempt=0;attempt<8&&!safe;++attempt){unsigned char random[16];if(RAND_bytes(random,sizeof(random))!=1){error="Native multipart boundary entropy is unavailable";return false;}boundary="scut--";for(const auto byte:random){boundary+="0123456789abcdef"[byte>>4];boundary+="0123456789abcdef"[byte&15];}safe=std::search(state->upload.begin(),state->upload.end(),boundary.begin(),boundary.end())==state->upload.end();}
    std::vector<unsigned char>encoded;
    size_t retained=RetainedUploadCapacity();const auto resident=active_.find(state->owner);if(resident==active_.end()||resident->second!=state)retained+=state->upload.capacity();
    if(retained>kUploadBudget){error="Native API active upload capacity exceeded";return false;}
    if(!safe||!suitecut::EncodeNativeHTTPMultipart(state->upload,parts,boundary,kMaxUpload,encoded,error,kUploadBudget-retained)){if(error.empty())error="Native multipart boundary collision limit exceeded";return false;}
    if(encoded.size()>available){error="Native API active upload budget exceeded";return false;}
    state->upload.swap(encoded);Remove(state->headers,"content-type");state->headers.emplace("content-type","multipart/form-data; boundary="+boundary);
    return true;
}
void SuiteCutHTTPControls::PreparedMultipart(int owner,int context,Dict params,Reply reply){
  const auto token=params->GetString("uploadId").ToString();const auto entry=preparations_.Find(token,context);
  const auto found=entry?active_.find(entry->owner):active_.end();
  if(!entry||found==active_.end()||!found->second->preparing||found->second->generation!=entry->generation){reply(false,nullptr,"Native multipart preparation is unavailable");return;}
  const auto state=found->second;
  if(state->deadline&&hooks_.now()>=state->deadline){const auto error=TimeoutError(state->owner);Stop(state->owner,error);reply(false,nullptr,error);return;}
  const auto fail=[this,state,reply](const std::string&error){Stop(state->owner,error);reply(false,nullptr,error);};
  if(params->GetString("action")=="appendPreparedMultipart"){
    const auto body=params->GetString("body").ToString();if(body.empty()||body.size()>32768){fail("Invalid native multipart source chunk");return;}
    const auto bytes=CefBase64Decode(body);if(!bytes||bytes->GetSize()>24576||bytes->GetSize()>kMaxUpload-state->upload.size()||bytes->GetSize()>kUploadBudget-upload_bytes_-active_upload_bytes_){fail("Native multipart source byte budget exceeded");return;}
    std::string error;if(!suitecut::AppendNativeHTTPUpload(state->upload,bytes->GetSize(),RetainedUploadCapacity(),kMaxUpload,kUploadBudget,[bytes](uint8_t*target,size_t size){return bytes->GetData(target,size,0)==size;},error)){fail(error);return;}
    active_upload_bytes_+=bytes->GetSize();reply(true,CefDictionaryValue::Create(),"");return;
  }
  const size_t previous=state->upload.size();std::string error;
  if(!Multipart(state,params->GetList("multipartParts"),kUploadBudget-upload_bytes_-active_upload_bytes_+previous,error)){fail(error);return;}
  active_upload_bytes_=active_upload_bytes_-previous+state->upload.size();state->preparing=false;state->preparation_token.clear();preparations_.Cancel(state->owner);Remove(state->headers,"content-length");
  Start(state);reply(true,CefDictionaryValue::Create(),"");
}
void SuiteCutHTTPControls::Start(const std::shared_ptr<State>& state) {
  if((state->stream_prepared||state->physical_lease)&&state->deadline&&hooks_.now()>=state->deadline){Stop(state->owner,TimeoutError(state->owner));return;}
  const auto context=contexts_.find(state->context);if(context==contexts_.end()){Stop(state->owner,"API request context was disposed");return;}
  state->generation=++generation_;context->second.physical_owners.insert(state->owner);hooks_.wake();
  if(context->second.jar){CookiesReady(state->owner,state->generation,"","");return;}
  const auto weak=weak_from_this();const int owner=state->owner;const auto generation=state->generation;auto params=CefDictionaryValue::Create();
  hooks_.context_command(owner,context->second.browser_context,"cookies",params,[weak,owner,generation,url=state->url,physical_lease=state->physical_lease](bool ok,Dict result,const std::string&error){if(const auto self=weak.lock()){const auto cookies=ok&&result?result->GetList("cookies"):nullptr;const auto state=self->active_.find(owner);if(state==self->active_.end()||state->second->generation!=generation)return;
      if(!ok){self->CookiesReady(owner,generation,"",error);return;}
      self->OrderCookies(state->second->context,cookies,[weak,owner,generation,url,physical_lease](CefRefPtr<CefListValue>ordered,const std::string&failure){if(const auto self=weak.lock())self->CookiesReady(owner,generation,ordered?suitecut::HTTPJarHeader(ordered,url,double(std::time(nullptr))):std::string{},failure);});}});
}
void SuiteCutHTTPControls::CookiesReady(int owner,uint64_t generation,std::string cookies,const std::string& error) {
  const auto found=active_.find(owner);if(found==active_.end() || found->second->generation!=generation)return;
  const auto state=found->second;
  // This genuine cookie receipt can arrive after the owned HTTP deadline but
  // before the Engine tick. Preserve HTTP timeout context in that ordering.
  if(state->deadline>0&&hooks_.now()>=state->deadline){Stop(owner,TimeoutError(owner));return;}
  if(!error.empty()){Stop(owner,error);return;}
  const auto context=contexts_.find(state->context);if(context==contexts_.end()){Stop(owner,"API request context was disposed");return;}
  suitecut::NativeCurlTransport::Request request;request.url=state->url;request.method=state->method;request.body=state->upload;
  for(const auto& [name,value]:state->headers){std::string bytes;if(!suitecut::HTTPEncodeHeaderValue(value.ToString16(),bytes)){Stop(owner,"Invalid character in header content");return;}request.headers.emplace_back(name.ToString(),std::move(bytes));}
  bool explicit_cookie=false;for(const auto&[name,value]:state->headers){(void)value;if(Lower(name.ToString())=="cookie")explicit_cookie=true;}if(!explicit_cookie&&!cookies.empty())request.headers.emplace_back("Cookie",cookies);
  state->policy.Apply(request);request.ignore_https_errors=state->ignore_https_errors;
  if(state->lookup_key.empty()){SendRequest(state,std::move(request));return;}
  if(!request.proxy_server.empty()){Stop(owner,"Native custom HTTP lookup with a proxy is not supported");return;}
  CefURLParts parts;if(!CefParseURL(state->url,parts)){Stop(owner,"Native HTTP lookup URL is invalid");return;}
  const auto host=CefString(&parts.host).ToString();std::string literal=host;if(literal.size()>1&&literal.front()=='['&&literal.back()==']')literal=literal.substr(1,literal.size()-2);in_addr ipv4{};in6_addr ipv6{};
  if(inet_pton(AF_INET,literal.c_str(),&ipv4)==1||inet_pton(AF_INET6,literal.c_str(),&ipv6)==1){SendRequest(state,std::move(request));return;}
  const auto actual_port=CefString(&parts.port).ToString();const int port=actual_port.empty()?(CefString(&parts.scheme)=="https"?443:80):std::atoi(actual_port.c_str());
  const auto weak=weak_from_this();
  if(!lookups_.Begin(owner,state->context,state->lookup_key,host,port,state->deadline,[weak,owner,generation,host,port,request=std::move(request)](auto addresses,const std::string&error)mutable{
    const auto self=weak.lock();if(!self)return;const auto found=self->active_.find(owner);if(found==self->active_.end()||found->second->generation!=generation)return;
    if(!error.empty()){self->Stop(owner,found->second->deadline&&self->hooks_.now()>=found->second->deadline?self->TimeoutError(owner):error);return;}
    request.lookup=suitecut::NativeCurlTransport::Request::Lookup{host,port,std::move(addresses)};self->SendRequest(found->second,std::move(request));
  }))Stop(owner,"Native HTTP host lookup capacity exceeded or invalid destination");
}
void SuiteCutHTTPControls::SendRequest(const std::shared_ptr<State>& state,suitecut::NativeCurlTransport::Request request){
  if((state->stream_prepared||state->physical_lease)&&state->deadline&&hooks_.now()>=state->deadline){Stop(state->owner,TimeoutError(state->owner));return;}
  const int owner=state->owner;const auto generation=state->generation;
  const auto weak=weak_from_this();std::string failure;
  const auto profile=contexts_.find(state->context);
  if(profile==contexts_.end()||profile->second.closing){Stop(owner,"Native API cookie context closed before dispatch");return;}
  if(profile->second.jar){
    request.authoritative_cookies=true;
    request.refresh_cookie_header=[weak,owner,generation](suitecut::NativeCurlTransport::Headers&headers,std::string&error){
      const auto self=weak.lock();if(!self){error="Native independent API cookie context closed";return false;}const auto current=self->active_.find(owner);
      if(current==self->active_.end()||current->second->generation!=generation){error="Native independent API request lost its original owner";return false;}const auto state=current->second;
      const auto found=self->contexts_.find(state->context);if(found==self->contexts_.end()||found->second.closing||!found->second.jar){error="Native independent API cookie context closed";return false;}
      if(state->deadline&&self->hooks_.now()>=state->deadline){error=self->TimeoutError(owner);return false;}
      bool explicit_cookie=false;for(const auto&[name,value]:state->headers){(void)value;if(Lower(name.ToString())=="cookie")explicit_cookie=true;}if(explicit_cookie)return true;
      std::string cookie;if(!found->second.jar->Header(state->url,self->JarClock(state->deadline),cookie,error)){if(state->deadline&&self->hooks_.now()>=state->deadline)error=self->TimeoutError(owner);return false;}
      std::erase_if(headers,[](const auto&header){return Lower(header.first)=="cookie";});if(!cookie.empty())headers.emplace_back("Cookie",std::move(cookie));return true;
    };
    request.receive_cookies=[weak,owner,generation](std::span<const std::string>headers,std::string&error){
      const auto self=weak.lock();if(!self){error="Native independent API cookie context closed";return false;}const auto current=self->active_.find(owner);
      if(current==self->active_.end()||current->second->generation!=generation){error="Native independent API request lost its original owner";return false;}const auto state=current->second;
      const auto found=self->contexts_.find(state->context);if(found==self->contexts_.end()||found->second.closing||!found->second.jar){error="Native independent API cookie context closed";return false;}
      if(state->deadline&&self->hooks_.now()>=state->deadline){error=self->TimeoutError(owner);return false;}
      if(!found->second.jar->Receive(state->url,headers,self->JarClock(state->deadline),error)){if(state->deadline&&self->hooks_.now()>=state->deadline)error=self->TimeoutError(owner);state->error=error;return false;}return true;
    };
  }
  request.checkpoint=[weak,owner,generation]{
    const auto self=weak.lock();if(!self)return false;
    const auto found=self->active_.find(owner);
    if(found==self->active_.end()||found->second->generation!=generation)return false;
    if(found->second->deadline&&self->hooks_.now()>=found->second->deadline){found->second->error=self->TimeoutError(owner);return false;}
    return true;
  };
  request.checkpoint=suitecut::BindNativeLocatorHandlerAsync(std::move(request.checkpoint));
  request.refresh_cookie_header=suitecut::BindNativeLocatorHandlerAsync(std::move(request.refresh_cookie_header));
  request.receive_cookies=suitecut::BindNativeLocatorHandlerAsync(std::move(request.receive_cookies));
  auto received=suitecut::BindNativeLocatorHandlerAsync(suitecut::NativeCurlTransport::Data{[weak,owner,generation](const void* data,size_t size){const auto self=weak.lock();return self && self->Data(owner,generation,data,size);}});
  auto completed=suitecut::BindNativeLocatorHandlerAsync(suitecut::NativeCurlTransport::Done{[weak,owner,generation](suitecut::NativeCurlTransport::Response response){if(auto self=weak.lock())self->Complete(owner,generation,std::move(response));}});
  if(!transport_.Start(owner,std::move(request),std::move(received),std::move(completed),failure))Stop(owner,failure);
  else if(tracing_)tracing_->Request(owner,generation,state->url,state->method,HTTPTraceHeaders(state->headers),state->upload);
  hooks_.wake();
}
size_t SuiteCutHTTPControls::RetainedResponseCapacity()const {
  size_t retained=0;
  for(const auto&[owner,state]:active_)retained+=state->body.capacity();
  for(const auto&[id,response]:responses_)retained+=response.body.capacity();
  return retained;
}
bool SuiteCutHTTPControls::Data(int owner,uint64_t generation,const void* bytes,size_t size) {
  const auto found=active_.find(owner);if(found==active_.end() || found->second->generation!=generation)return false;
  auto& state=*found->second;
  if(!bytes){active_body_bytes_-=state.body.size();std::vector<uint8_t>().swap(state.body);return true;}
  std::string allocation_error;
  if(!suitecut::AppendNativeHTTPUpload(state.body,size,RetainedResponseCapacity(),kMaxBody,kBodyBudget,[bytes](uint8_t*target,size_t amount){std::memcpy(target,bytes,amount);return true;},allocation_error)){state.error="Native API response body budget exceeded";return false;}
  active_body_bytes_+=size;return true;
}
void SuiteCutHTTPControls::Complete(int owner,uint64_t generation,suitecut::NativeCurlTransport::Response response) {
  const auto found=active_.find(owner);if(found==active_.end() || found->second->generation!=generation)return;
  const auto state=found->second;const auto context=contexts_.find(state->context);if(context==contexts_.end()){Stop(owner,"API request context was disposed");return;}
  if(tracing_){auto actual=CefDictionaryValue::Create();actual->SetInt("status",response.status);actual->SetString("statusText",HTTPHeaderText(response.status_text));CefRequest::HeaderMap observed;for(const auto& [name,value]:response.headers)observed.emplace(name,HTTPHeaderText(value));actual->SetDictionary("headers",HTTPTraceHeaders(observed));tracing_->Response(owner,generation,actual,state->body,state->error.empty()?response.error:state->error);}
  if((state->stream_prepared||state->physical_lease)&&state->deadline&&hooks_.now()>=state->deadline){Stop(owner,TimeoutError(owner));return;}
  if(!state->error.empty()){Stop(owner,state->error);return;}
  if(!response.error.empty()){
    const auto retry=suitecut::NativeHTTPRetry(response.error_code,response.os_errno,response.status);
    if(retry!=suitecut::NativeHTTPRetryReason::None&&state->retries>0){
      // maxRetries explicitly permits replay of the retained request. An
      // unexpected framing EOF is distinct from an actual connection reset;
      // keep the real result/errno if this bounded policy is exhausted.
      if(state->deadline&&hooks_.now()>=state->deadline){Stop(owner,TimeoutError(owner));return;}
      --state->retries;active_body_bytes_-=state->body.size();std::vector<uint8_t>().swap(state->body);Start(state);return;
    }
    const auto description=response.error_code==CURLE_GOT_NOTHING?std::string("socket hang up (ECONNRESET)"):response.error_code==CURLE_PARTIAL_FILE?std::string("aborted"):response.error;
    Stop(owner,"apiRequestContext."+state->api_name+": "+description,suitecut::HTTPTransportErrorMetadata(response.error_code,response.os_errno));return;
  }
  if(context->second.jar){Finish(state,std::move(response));return;}
  CefURLParts parts;CefParseURL(state->url,parts);const double now=static_cast<double>(std::time(nullptr));state->cookie_actions.clear();state->cookie_index=0;
  for(const auto&raw:response.set_cookies){
    const auto parsed=suitecut::ParseNativeHTTPCookie(raw,CefString(&parts.host).ToString(),CefString(&parts.path).ToString(),now);if(!parsed)continue;
    // There is no API top-level site to assign a genuine CHIPS key. An
    // unrecognized Partitioned response must never become an unpartitioned one.
    auto attributes=Lower(raw);bool partitioned=false;size_t begin=attributes.find(';');while(begin!=std::string::npos){const auto end=attributes.find(';',begin+1);const auto item=suitecut::CookieTrim(attributes.substr(begin+1,end==std::string::npos?std::string::npos:end-begin-1));if(item=="partitioned"||item.starts_with("partitioned="))partitioned=true;begin=end;}if(partitioned)continue;
    auto cookie=CefDictionaryValue::Create();cookie->SetString("name",parsed->name);cookie->SetString("value",parsed->value);cookie->SetString("path",parsed->path);cookie->SetBool("secure",parsed->secure);cookie->SetBool("httpOnly",parsed->http_only);cookie->SetString("sameSite",parsed->same_site==3?"Strict":parsed->same_site==1?"None":"Lax");
    const auto host=CefString(&parts.host).ToString();if(parsed->domain.empty())cookie->SetString("url",state->url);else cookie->SetString("domain","."+parsed->domain);
    auto params=CefDictionaryValue::Create();
    if(parsed->expires&&*parsed->expires<=now){auto filter=CefDictionaryValue::Create();filter->SetString("name",parsed->name);filter->SetString("path",parsed->path);filter->SetString("domain",parsed->domain.empty()?host:"."+parsed->domain);params->SetDictionary("filter",filter);state->cookie_actions.emplace_back("clearCookies",params);}
    else{if(parsed->expires)cookie->SetDouble("expires",*parsed->expires);auto list=CefListValue::Create();list->SetDictionary(0,cookie);params->SetList("cookies",list);state->cookie_actions.emplace_back("httpCookieCommit",params);}
  }
  state->pending_response=std::move(response);CommitCookies(state);
}
void SuiteCutHTTPControls::CommitCookies(const std::shared_ptr<State>&state){
  const auto active=active_.find(state->owner);if(active==active_.end()||active->second!=state)return;
  if((state->stream_prepared||state->physical_lease)&&state->deadline&&hooks_.now()>=state->deadline){Stop(state->owner,TimeoutError(state->owner));return;}
  if(state->cookie_index==state->cookie_actions.size()){auto response=std::move(*state->pending_response);state->pending_response.reset();state->cookie_actions.clear();Finish(state,std::move(response));return;}
  const auto context=contexts_.find(state->context);if(context==contexts_.end()||context->second.closing){Stop(state->owner,"Native HTTP cookie context closed");return;}
  const auto[action,params]=state->cookie_actions[state->cookie_index++];context->second.physical_owners.insert(state->owner);const auto weak=weak_from_this();
  hooks_.context_command(state->owner,context->second.browser_context,action,params,[weak,state](bool ok,Dict,const std::string&error){const auto self=weak.lock();if(!self)return;const auto current=self->active_.find(state->owner);if(current==self->active_.end()||current->second!=state)return;if(!ok){self->Stop(state->owner,error);return;}self->CommitCookies(state);});
}
void SuiteCutHTTPControls::Finish(const std::shared_ptr<State>& state,suitecut::NativeCurlTransport::Response response) {
  if((state->stream_prepared||state->physical_lease)&&state->deadline&&hooks_.now()>=state->deadline){Stop(state->owner,TimeoutError(state->owner));return;}
  const int owner=state->owner,status=response.status;std::vector<std::pair<std::string,std::string>> headers;
  headers.reserve(response.headers.size());for(const auto& [name,value]:response.headers)headers.emplace_back(name,HTTPHeaderText(value));
  std::string location;for(const auto& [name,value]:response.headers)if(Lower(name)=="location")location=suitecut::HTTPRedirectLocation(value);
  const bool redirect=(status==301 || status==302 || status==303 || status==307 || status==308) && !location.empty();
  if(redirect && state->remaining_redirects>0) {
    CefString target;if(!CefResolveURL(state->url,location,target) || !URL(target.ToString())){Stop(owner,"Invalid API redirect target");return;}
    if(Origin(state->url)!=Origin(target.ToString())){Remove(state->headers,"authorization");Remove(state->headers,"cookie");Remove(state->headers,"host");}
    if(((status==301 || status==302) && state->method=="POST") || (status==303 && state->method!="HEAD")) {state->method="GET";active_upload_bytes_-=state->upload.size();std::vector<uint8_t>().swap(state->upload);Remove(state->headers,"content-type");Remove(state->headers,"content-length");}
    state->url=target.ToString();--state->remaining_redirects;active_body_bytes_-=state->body.size();std::vector<uint8_t>().swap(state->body);Start(state);return;
  }
  if(redirect && state->remaining_redirects==0 && state->follow_redirects){Stop(owner,"Max redirect count exceeded");return;}
  if(state->fail_status && (status<200 || status>=400)){std::string error=std::to_string(status)+" "+HTTPHeaderText(response.status_text);if(state->method!="HEAD"&&!state->body.empty())error+="\nResponse text:\n"+std::string(reinterpret_cast<const char*>(state->body.data()),std::min<size_t>(state->body.size(),1000));Stop(owner,error);return;}
  if(responses_.size()>=128){Stop(owner,"Native API response cache capacity exceeded; dispose responses");return;}
  Response saved;saved.context=state->context;saved.status=status;saved.url=response.url.empty()?state->url:response.url;saved.status_text=HTTPHeaderText(response.status_text);saved.method=std::move(response.method);saved.headers=std::move(headers);saved.body=std::move(state->body);saved.remote_ip=std::move(response.remote_ip);saved.remote_port=response.remote_port;saved.security=std::move(response.security);saved.timing=response.timing;
  active_body_bytes_-=saved.body.size();body_bytes_+=saved.body.size();const auto id=std::to_string(++next_response_);responses_[id]=std::move(saved);auto result=Metadata(id,responses_.at(id));
  active_upload_bytes_-=state->upload.size();std::vector<uint8_t>().swap(state->upload);active_.erase(owner);if(tracing_)tracing_->After(owner,true,result,"");state->reply(true,result,"");
}
SuiteCutHTTPControls::Dict SuiteCutHTTPControls::Metadata(const std::string& id,const Response& response) const {
  auto result=CefDictionaryValue::Create();result->SetString("responseId",id);result->SetInt("apiContextId",response.context);result->SetString("url",response.url);
  result->SetString("method",response.method);result->SetInt("status",response.status);result->SetString("statusText",response.status_text);auto headers=CefListValue::Create();size_t i=0;
  for(const auto& [name,value]:response.headers){auto pair=CefDictionaryValue::Create();pair->SetString("name",name);pair->SetString("value",value);headers->SetDictionary(i++,pair);}
  result->SetList("headers",headers);
  if(response.remote_ip.empty())result->SetNull("serverAddr");else{auto address=CefDictionaryValue::Create();address->SetString("ipAddress",response.remote_ip);address->SetInt("port",response.remote_port);result->SetDictionary("serverAddr",address);}
  if(!response.security)result->SetNull("securityDetails");else{const auto&security=*response.security;auto details=CefDictionaryValue::Create();details->SetString("issuer",security.issuer);details->SetString("protocol",security.protocol);details->SetString("subjectName",security.subject_name);details->SetDouble("validFrom",security.valid_from);details->SetDouble("validTo",security.valid_to);result->SetDictionary("securityDetails",details);}
  const auto&t=response.timing;auto timing=CefDictionaryValue::Create();timing->SetDouble("startTime",t.start_time);timing->SetDouble("domainLookupStart",t.domain_lookup_start);timing->SetDouble("domainLookupEnd",t.domain_lookup_end);timing->SetDouble("connectStart",t.connect_start);timing->SetDouble("secureConnectionStart",t.secure_connection_start);timing->SetDouble("connectEnd",t.connect_end);timing->SetDouble("requestStart",t.request_start);timing->SetDouble("responseStart",t.response_start);timing->SetDouble("responseEnd",t.response_end);result->SetDictionary("timing",timing);return result;
}
void SuiteCutHTTPControls::Cancel(int owner) {preparations_.Cancel(owner);scope_commands_.erase(owner);for(auto&[id,context]:contexts_)std::erase_if(context.dispose_replies,[owner](const auto&entry){return entry.first==owner;});if(tracing_)tracing_->Cancel(owner,"Native API request operation canceled");StopStorage(owner,"Native API storage query was canceled",false);const auto found=active_.find(owner);if(found==active_.end())return;const auto state=found->second;active_body_bytes_-=state->body.size();active_upload_bytes_-=state->upload.size();active_.erase(found);transport_.Cancel(owner);lookups_.Cancel(owner,"Native HTTP original operation canceled");
  // Real cookie callbacks may still retain this canceled State. curl has now
  // released its body span, and those late callbacks reject the missing exact
  // active identity before accessing data, so retain no uncharged payload.
  std::vector<uint8_t>().swap(state->upload);std::vector<uint8_t>().swap(state->body);
}
void SuiteCutHTTPControls::Stop(int owner,const std::string& error,Dict details) {const auto found=active_.find(owner);if(found==active_.end())return;auto reply=found->second->reply;if(tracing_)tracing_->After(owner,false,nullptr,error);Cancel(owner);reply(false,details,error);}
void SuiteCutHTTPControls::ContextClosed(int context){route_contexts_.erase(context);std::vector<int>ids;for(auto&[id,profile]:contexts_)if(profile.browser_context==context){profile.actually_closed=true;ids.push_back(id);}for(const int id:ids)BeginDispose(id);}
void SuiteCutHTTPControls::Clear(){
  closed_=true;std::vector<int>ids;for(const auto&[id,profile]:contexts_)ids.push_back(id);for(const int id:ids)BeginDispose(id);
  if(tracing_){tracing_->Clear();}transport_.Clear();transport_.ResetIdle();
}
void SuiteCutHTTPControls::StopStorage(int owner,const std::string& error,bool respond) {const auto found=storage_queries_.find(owner);if(found==storage_queries_.end())return;auto done=std::move(found->second.reply);found->second.canceled->canceled.store(true);storage_queries_.erase(found);if(respond)done(false,nullptr,error);}
void SuiteCutHTTPControls::Tick(double now) {transport_.Tick();ProfileTick(now);if(tracing_)tracing_->Tick(now);std::vector<int> expired;for(const auto& [owner,query]:storage_queries_)if(now>=query.deadline)expired.push_back(owner);for(const int owner:expired)StopStorage(owner,"Native API storage query deadline exceeded",true);for(auto it=uploads_.begin();it!=uploads_.end();)if(now-it->second.updated>=30000){upload_bytes_-=it->second.bytes.size();it=uploads_.erase(it);}else ++it;}
size_t SuiteCutHTTPControls::RetainedUploadCapacity()const{size_t bytes=0;for(const auto&[id,upload]:uploads_)bytes+=upload.bytes.capacity();for(const auto&[id,state]:active_)bytes+=state->upload.capacity();return bytes;}
void SuiteCutHTTPControls::Diagnostics(Dict result) const {result->SetDouble("httpAPIOriginsAllocatedBytes",static_cast<double>(origins_budget_->Bytes()));result->SetDouble("httpAPIOriginsPeakAllocatedBytes",static_cast<double>(origins_budget_->Peak()));size_t origin_count=0,pending_origins=0;for(const auto&[id,context]:contexts_){(void)id;if(context.origins)origin_count+=context.origins->Size();}for(const auto&[id,command]:scope_commands_){(void)id;if(command.origins)pending_origins+=command.origins->Size();}result->SetDouble("httpAPIOriginsEntries",static_cast<double>(origin_count));result->SetDouble("httpAPIOriginsPendingEntries",static_cast<double>(pending_origins));result->SetDouble("httpAPICookieAllocatedBytes",static_cast<double>(jar_budget_->Bytes()));result->SetDouble("httpAPICookiePeakAllocatedBytes",static_cast<double>(jar_budget_->Peak()));size_t api_cookie_entries=0;for(const auto&[id,context]:contexts_){(void)id;if(context.jar)api_cookie_entries+=context.jar->Size();}result->SetDouble("httpAPICookieEntries",static_cast<double>(api_cookie_entries));result->SetDouble("httpRetainedUploadCapacity",static_cast<double>(RetainedUploadCapacity()));result->SetInt("httpUploadPreparations",static_cast<int>(preparations_.Size()));result->SetInt("httpLookupRequests",static_cast<int>(lookups_.Size()));result->SetDouble("httpCookieMetadataBytes",static_cast<double>(metadata_bytes_->load()));result->SetInt("httpProfilePhysical",static_cast<int>(PhysicalProfiles()));result->SetInt("httpScopeCommands",static_cast<int>(scope_commands_.size()));if(tracing_)tracing_->Diagnostics(result);result->SetInt("httpStorageQueries",static_cast<int>(storage_queries_.size()));result->SetDouble("httpStorageCookieBytes",static_cast<double>(storage_cookie_bytes_->load()));result->SetInt("httpCookieVisits",static_cast<int>(pending_cookie_visits_));result->SetInt("httpCookieCallbacks",static_cast<int>(pending_cookie_saves_));result->SetInt("httpTransfers",static_cast<int>(transport_.size()));result->SetInt("httpContexts",static_cast<int>(contexts_.size()));result->SetInt("httpActive",static_cast<int>(active_.size()));result->SetInt("httpResponses",static_cast<int>(responses_.size()));result->SetInt("httpUploads",static_cast<int>(uploads_.size()));result->SetDouble("httpRetainedBodyCapacity",static_cast<double>(RetainedResponseCapacity()));result->SetDouble("httpRetainedBodyBytes",static_cast<double>(body_bytes_));result->SetDouble("httpActiveBodyBytes",static_cast<double>(active_body_bytes_));result->SetDouble("httpUploadBytes",static_cast<double>(upload_bytes_));result->SetDouble("httpActiveUploadBytes",static_cast<double>(active_upload_bytes_));}

std::string SuiteCutHTTPControls::TimeoutError(int owner)const{const auto found=active_.find(owner);if(found==active_.end())return {};return "apiRequestContext."+found->second->api_name+": Timeout "+std::to_string(found->second->timeout_ms)+"ms exceeded";}

#include "http_profiles.inc"
#include "http_api_cookies.inc"
#include "http_api_origins.inc"

bool SuiteCutHTTPControls::AcceptedWorkReady()const{
  if(!active_.empty()||!storage_queries_.empty()||!scope_commands_.empty()||!uploads_.empty()||
     transport_.size()||preparations_.Size()||lookups_.Size()||pending_cookie_saves_||pending_cookie_visits_||
     (tracing_&&tracing_->Physical()))return false;
  for(const auto&[id,context]:contexts_){(void)id;if(context.initializing||context.closing||context.physical_visits||!context.physical_owners.empty()||!context.dispose_replies.empty())return false;}
  // Idle response buffers, origin snapshots and real cookie jars are resident
  // worker-context state. Their byte budgets remain charged; do not erase them.
  return true;
}
