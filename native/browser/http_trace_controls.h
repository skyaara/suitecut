// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "trace_controls.h"
#include "trace_sources.h"
#include "har_capture.h"
#include "curl_transport.h"

namespace suitecut {
// Independent journals for actual native HTTP contexts. The embedder reports
// admission, accepted curl transfers and real completion; this class performs
// no HTTP request and never borrows a browser page's trace/session identity.
class HTTPTraceControls {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Bytes=std::vector<uint8_t>;
  struct Hooks {
    std::string raw_directory;
    std::function<double()> now;
    std::function<bool(int)> context_exists;
    std::function<bool(std::function<void()>)> background,owner;
    std::function<void()> wake;
  };
  explicit HTTPTraceControls(Hooks hooks):hooks_(std::move(hooks)),capture_(
      [](int,Dict){},[](int,const std::string&,const Bytes&){},hooks_.now,
      [this](const std::string& error){for(const auto& [context,owner]:owners_)controls_->CaptureError(context,error);}) {
    TraceControls::Hooks controls;controls.raw_directory=hooks_.raw_directory;controls.raw_prefix="api";controls.now=hooks_.now;controls.wake=hooks_.wake;controls.post=hooks_.owner;controls.background=hooks_.background;
    controls.context_options=[this](int context)->Dict{if(!hooks_.context_exists(context))return nullptr;auto event=CefDictionaryValue::Create();event->SetInt("version",1);event->SetString("origin","suitecut-native");event->SetString("requestContextId",Reference(context));event->SetDouble("monotonicTime",hooks_.now());event->SetDouble("wallTime",Wall());event->SetDictionary("options",CefDictionaryValue::Create());return event;};
    controls.request_capture=[](int){};
    controls.settled_context=[this](int){hooks_.wake();};
    controls.pending_capture=[this](int context){return sources_->Pending(context);};
    controls.begin_session=[this](TraceJournal::Lease lease,bool,std::string& error){const auto options=controls_->Configuration(lease.context);if(!options||!options->dom)return true;if(!capture_.Start(lease.context)){error="Native HTTP trace resource consumer is unavailable";return false;}return true;};
    controls.seal_session=[this](TraceJournal::Lease lease){capture_.Stop(lease.context);};
    controls.end_session=[this](TraceJournal::Lease lease){capture_.Stop(lease.context);};
    controls.source=[this](auto lease,const std::string& path){if(!sources_->Capture(lease,path))controls_->CaptureError(lease,"Native HTTP trace source admission failed");};
    controls_=std::make_shared<TraceControls>(std::move(controls));
    TraceSources::Hooks source;source.background=hooks_.background;source.owner=hooks_.owner;source.wake=hooks_.wake;
    source.captured=[this](auto lease,const std::string& path,Bytes bytes){controls_->SourceResource(lease,path,std::move(bytes));};
    source.error=[this](auto lease,const std::string& error){controls_->CaptureError(lease,error);};
    sources_=std::make_shared<TraceSources>(std::move(source));
    capture_.SetReservation([this](int context){const auto scope=controls_->CaptureLease(context);return NativeHARCapture::Publication{
      [this,scope](Dict event){auto snapshot=event->GetDictionary("snapshot");if(snapshot){snapshot->SetString("_apiRequestRef",Reference(scope.context));if(auto request=snapshot->GetDictionary("request"))request->SetString("_headersSource","native-configured");}controls_->Network(scope,event);},
      [this,scope](const std::string& path,const Bytes& bytes){controls_->Resource(scope,path,bytes);}};});
  }
  ~HTTPTraceControls(){Clear();}
  void Handle(int owner,int context,Dict params,double deadline,TraceControls::Reply reply){controls_->Handle(owner,context,params,deadline,std::move(reply));}
  void Before(int owner,int context,Dict params){
    const auto scope=controls_->CaptureLease(context);if(!scope.context)return;
    if(calls_.size()>=64||calls_.contains(owner)){controls_->CaptureError(scope,"Native HTTP trace pending action capacity exceeded");return;}
    const auto call=controls_->NextCall();if(!call){controls_->CaptureError(scope,"Native HTTP trace action identity exhausted");return;}
    auto before=CefDictionaryValue::Create();before->SetString("type","before");before->SetString("callId","call@"+std::to_string(call));before->SetString("class","APIRequestContext");before->SetString("method","fetch");before->SetDouble("startTime",hooks_.now());
    const auto url=params->GetString("url").ToString(),method=params->GetString("method").ToString();before->SetString("apiName",method+" "+DisplayURL(url));auto arguments=CefDictionaryValue::Create();arguments->SetString("url",url);arguments->SetString("method",method);before->SetDictionary("params",arguments);if(auto stack=params->GetList("traceStack"))before->SetList("stack",stack->Copy());
    calls_.emplace(owner,Call{scope,call});controls_->Before(context,call,before);
  }
  // Invoke only after curl accepted this actual owner/generation transfer.
  // Headers are the native configured values; no generated wire headers are
  // invented. A later transport observer may enrich them with exact wire data.
  void Request(int owner,uint64_t generation,const std::string& url,const std::string& method,Dict headers,std::span<const uint8_t> upload){
    const auto call=calls_.find(owner);if(call==calls_.end()||!controls_->ChunkCurrent(call->second.scope))return;const auto scope=call->second.scope;if(!capture_.Active(scope.context))return;
    if(transfers_.size()>=64){controls_->CaptureError(scope,"Native HTTP trace transfer capacity exceeded");return;}
    const auto identity=std::to_string(owner)+":"+std::to_string(generation);const auto actor="api:"+std::to_string(scope.context);
    if(!owners_.contains(scope.context)){
      if(!capture_.Attach(scope.context,actor,[this](const std::string&,const std::string& id,double,NativeHARCapture::BodyReply reply){if(!reading_||reading_->identity!=id){reply(false,{},"Actual native HTTP body observation is unavailable");return;}if(reading_->bytes.size()>2097152){reply(false,{},"Actual native HTTP trace body exceeds 2 MiB");return;}reply(true,Bytes(reading_->bytes.begin(),reading_->bytes.end()),"");},NativeHARCapture::OwnerKind::APIRequestContext)){controls_->CaptureError(scope,"Native HTTP trace resource owner is unavailable");return;}owners_[scope.context]=actor;
    }
    auto request=CefDictionaryValue::Create();request->SetString("id",identity);request->SetString("requestId",identity);request->SetString("session","");request->SetString("url",url);request->SetString("method",method);request->SetString("resourceType","Fetch");request->SetDictionary("headers",headers->Copy(false));auto timing=CefDictionaryValue::Create();timing->SetDouble("responseEnd",-1);request->SetDictionary("timing",timing);
    auto original=CefDictionaryValue::Create();original->SetString("requestId",identity);original->SetDouble("wallTime",Wall()/1000);auto raw=request->Copy(false);if(!upload.empty()){auto entries=CefListValue::Create();auto part=CefDictionaryValue::Create();part->SetString("bytes",CefBase64Encode(upload.data(),upload.size()));entries->SetDictionary(0,part);raw->SetList("postDataEntries",entries);}original->SetDictionary("request",raw);
    if(!capture_.OriginalRequest(scope.context,actor,"",original)){controls_->CaptureError(scope,"Native HTTP trace request admission failed");return;}
    // Retain an independent dictionary before event ownership transfers.
    auto retained=request->Copy(false);auto event=CefDictionaryValue::Create();event->SetString("event","request");event->SetDictionary("request",request->Copy(false));if(!capture_.Observe(scope.context,actor,event,Wall())){controls_->CaptureError(scope,"Native HTTP trace resource admission failed");return;}
    transfers_.emplace(identity,Transfer{owner,generation,scope,std::move(retained),hooks_.now()});
  }
  void Response(int owner,uint64_t generation,Dict response,std::span<const uint8_t> bytes,const std::string& error){
    const auto identity=std::to_string(owner)+":"+std::to_string(generation);const auto found=transfers_.find(identity);if(found==transfers_.end())return;auto transfer=std::move(found->second);transfers_.erase(found);if(!controls_->ChunkCurrent(transfer.scope)||!capture_.Active(transfer.scope.context))return;
    auto timing=CefDictionaryValue::Create();timing->SetDouble("responseEnd",std::max(0.0,hooks_.now()-transfer.started));transfer.request->SetDictionary("timing",timing);auto event=CefDictionaryValue::Create();event->SetString("event",error.empty()?"requestfinished":"requestfailed");event->SetDictionary("request",transfer.request);if(response){auto actual=response->Copy(false);actual->SetString("requestId",identity);actual->SetString("session","");event->SetDictionary("response",actual);}if(!error.empty())event->SetString("errorText",error);
    BodyReading reading{identity,bytes};reading_=&reading;const bool accepted=capture_.Observe(transfer.scope.context,owners_.at(transfer.scope.context),event,Wall());reading_=nullptr;if(!accepted)controls_->CaptureError(transfer.scope,"Native HTTP trace completion observation failed");
  }
  void After(int owner,bool ok,Dict result,const std::string& error){
    const auto found=calls_.find(owner);if(found==calls_.end())return;const auto call=found->second;calls_.erase(found);auto event=CefDictionaryValue::Create();event->SetString("type","after");event->SetString("callId","call@"+std::to_string(call.call));event->SetDouble("endTime",hooks_.now());if(!ok){auto failure=CefDictionaryValue::Create();failure->SetString("message",error);event->SetDictionary("error",failure);}else if(result)event->SetDictionary("result",result->Copy(false));controls_->After(call.scope.context,call.call,event);
    std::vector<std::pair<int,uint64_t>> incomplete;for(const auto& [id,transfer]:transfers_)if(transfer.owner==owner){incomplete.emplace_back(owner,transfer.generation);}for(const auto& [actual,generation]:incomplete)Response(actual,generation,nullptr,{},error.empty()?"Actual native HTTP request ended before transfer completion":error);
  }
  void Cancel(int owner,const std::string& error){controls_->Cancel(owner);After(owner,false,nullptr,error);}
  void Closed(int context){controls_->Closed(context);sources_->Closed(context);capture_.Stop(context);if(const auto owner=owners_.find(context);owner!=owners_.end()){capture_.Detach(owner->second);owners_.erase(owner);}for(auto it=calls_.begin();it!=calls_.end();)if(it->second.scope.context==context)it=calls_.erase(it);else++it;for(auto it=transfers_.begin();it!=transfers_.end();)if(it->second.scope.context==context)it=transfers_.erase(it);else++it;}
  void Clear(){controls_->Clear();sources_->Clear();capture_.Clear();calls_.clear();transfers_.clear();owners_.clear();}
  void Tick(double now){sources_->Tick();controls_->Tick(now);capture_.Tick(now);}
  void Diagnostics(Dict output)const{auto values=CefDictionaryValue::Create();controls_->Diagnostics(values);for(const char* key:{"Contexts","Physical","WaitingStop","PendingActions"})output->SetInt(std::string("apiTrace")+key,values->GetInt(std::string("nativeTrace")+key));output->SetInt("apiTraceSourcesPhysical",int(sources_->PhysicalCount()));output->SetInt("apiTraceTransfers",int(transfers_.size()));}
  bool ContextReady(int context)const{return controls_->ContextReady(context)&&!sources_->Pending(context);}
  bool NeedsTick()const{return controls_->NeedsTick()||sources_->PhysicalCount();}
  size_t Physical()const{return controls_->PhysicalCount()+sources_->PhysicalCount();}
 private:
  struct Call{TraceJournal::Lease scope;uint64_t call;};
  struct Transfer{int owner;uint64_t generation;TraceJournal::Lease scope;Dict request;double started;};
  struct BodyReading{std::string identity;std::span<const uint8_t> bytes;};
  static double Wall(){return std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();}
  static std::string Reference(int context){return "request-context@"+std::to_string(context);}
  static std::string DisplayURL(const std::string& url){CefURLParts parts;if(!CefParseURL(url,parts))return url;const auto port=CefString(&parts.port).ToString();return CefString(&parts.host).ToString()+(port.empty()?"":":"+port)+CefString(&parts.path).ToString()+(parts.query.length?"?"+CefString(&parts.query).ToString():"");}
  Hooks hooks_;std::shared_ptr<TraceControls> controls_;std::shared_ptr<TraceSources> sources_;NativeHARCapture capture_;std::map<int,Call>calls_;std::map<std::string,Transfer>transfers_;std::map<int,std::string>owners_;BodyReading*reading_=nullptr;
};
} // namespace suitecut
