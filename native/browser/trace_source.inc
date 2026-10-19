// SPDX-License-Identifier: MIT
// Included inside actual Source. All frame/loader/session identities come from
// the native CEF frame registry; captures never manufacture a page or realm.
struct NativeTraceAPI {suitecut::TraceJournal::Lease lease;std::string method;uint64_t call;bool phases=false;};
struct NativeTraceProtocolCall {std::string session;double deadline;bool expired=false;std::function<void(bool,const std::string&)> reply;size_t response_limit=8*1048576;std::function<void()> release;bool dom=true;};
std::map<int,NativeTraceAPI> native_trace_api_;
std::map<int,NativeTraceProtocolCall> native_trace_protocol_;
std::map<int,double> native_wait_until_;
uint64_t native_trace_snapshot_=0;
bool native_trace_page_closing_=false;
double native_trace_last_invalidate_=-100;
bool NativeTraceScreenshots() const{if(!browser_||closing_page_||native_trace_page_closing_)return false;const auto context=engine_.contexts()->ContextId(page_id_);const auto config=engine_.traces()->Configuration(context);return browser_&&!closing_page_&&!native_trace_page_closing_&&config&&config->screenshots&&engine_.traces()->CaptureLease(context).context;}
// Actual console callbacks enter the chunk before public delivery, so a stop
// requested by the last console listener observes every accepted event.
void NativeTraceConsole(Dict info,CefRefPtr<CefListValue> arguments){
  const int context=engine_.contexts()->ContextId(page_id_);const auto traces=engine_.traces();const auto lease=traces->CaptureLease(context);if(!lease.context)return;
  auto event=CefDictionaryValue::Create();event->SetString("type","console");event->SetString("messageType",info->GetString("type"));suitecut::DevToolsSetString(event,"text",suitecut::DevToolsString(info->GetValue("text")));event->SetString("pageId","page@"+std::to_string(page_id_));event->SetDouble("time",Now());
  if(const auto location=info->GetDictionary("location"))event->SetDictionary("location",location->Copy(false));
  auto values=CefListValue::Create();if(arguments)for(size_t i=0;i<std::min<size_t>(arguments->GetSize(),64);++i){const auto remote=arguments->GetDictionary(i);if(!remote)continue;auto arg=CefDictionaryValue::Create();suitecut::DevToolsSetString(arg,"preview",ConsolePreview(remote));const auto type=remote->GetString("type").ToString();if(type=="string")suitecut::DevToolsSetString(arg,"value",suitecut::DevToolsString(remote->GetValue("value")));else if(type=="boolean"&&remote->GetType("value")==VTYPE_BOOL)arg->SetBool("value",remote->GetBool("value"));else if(type=="number"&&(remote->GetType("value")==VTYPE_DOUBLE||remote->GetType("value")==VTYPE_INT))arg->SetValue("value",remote->GetValue("value")->Copy());else if(remote->GetString("subtype")=="null"&&remote->GetType("value")==VTYPE_NULL)arg->SetNull("value");values->SetDictionary(values->GetSize(),arg);}
  if(info->HasKey("argumentsError"))event->SetString("argumentsError",info->GetString("argumentsError"));
  event->SetList("args",values);auto value=CefValue::Create();value->SetDictionary(event);std::string json;if(!suitecut::DevToolsLosslessJSON(value,json,1048576)){traces->CaptureError(lease,"Native trace console event byte capacity exceeded");return;}traces->ConsoleCaptured(lease,json);
}
// Observe accepted genuine socket records before deferred public FIFO delivery.
// The immutable native session lease is selected here, never at a late write.
void NativeTraceWebSocket(Dict info,const std::vector<uint8_t>& bytes,double observed){
  const int context=engine_.contexts()->ContextId(page_id_);const auto traces=engine_.traces();const auto lease=traces->CaptureLease(context);const auto options=traces->Configuration(context);if(!lease.context||!options||!options->dom)return;
  auto event=info->Copy(false);event->SetString("type","websocket");event->SetString("pageId","page@"+std::to_string(page_id_));event->SetDouble("timestamp",observed);if(info->HasKey("opcode")){event->SetString("payload",CefBase64Encode(bytes.data(),bytes.size()));event->SetString("encoding","base64");}
  const auto encoded=Json(event);traces->WebSocketCaptured(lease,encoded);
}
static std::vector<std::string> NativeTraceStyles(){return {"display","visibility","position","color","background-color","font-family","font-size","font-weight","line-height","white-space","border-top-width","border-right-width","border-bottom-width","border-left-width","border-top-color","border-right-color","border-bottom-color","border-left-color","width","height","transform"};}
void NativeTraceDOM(suitecut::TraceJournal::Lease lease,uint64_t call,const std::string& phase,double deadline,std::function<void()> done={}){
  auto traces=engine_.traces();const auto options=traces->Configuration(lease.context);if(!browser_||closing_page_||native_trace_page_closing_||!options||!options->dom||!traces->ChunkCurrent(lease)){if(done)done();return;}
  if(native_trace_snapshot_>=9007199254740991ULL){traces->CaptureError(lease,"Native trace snapshot identity exhausted");if(done)done();return;}
  const auto snapshot=++native_trace_snapshot_;const auto styles=NativeTraceStyles();std::map<std::string,std::string> loaders;std::set<std::string> target_sessions;
  for(const auto& [frame,state]:frame_states_)if(state.context&&!state.loader.empty()&&(state.session.empty()||sessions_.contains(state.session))){loaders[frame]=state.loader;target_sessions.insert(state.session);}
  if(target_sessions.empty()){if(done)done();return;}
  struct Batch {size_t remaining;std::function<void()> done;};auto batch=std::make_shared<Batch>(Batch{target_sessions.size(),std::move(done)});
  const auto finish=[batch]{if(batch->remaining&&!--batch->remaining&&batch->done){auto done=std::move(batch->done);done();}};
  for(const auto& session:target_sessions){
    if(native_trace_protocol_.size()>=8||!engine_.TraceClaim()){traces->CaptureError(lease,"Native trace physical DOM capture capacity exceeded");finish();continue;}
    const int assigned=AllocateDevToolsId();if(!assigned){engine_.TraceRelease();traces->CaptureError(lease,"Native trace DevTools identity exhausted");finish();continue;}
    auto params=CefDictionaryValue::Create();auto properties=CefListValue::Create();for(size_t i=0;i<styles.size();++i)properties->SetString(i,styles[i]);params->SetList("computedStyles",properties);params->SetBool("includeDOMRects",true);params->SetBool("includePaintOrder",true);
    auto callback=[this,traces,lease,call,phase,snapshot,styles,loaders,session,finish](bool ok,const std::string& json){
      if(!traces->ChunkCurrent(lease)||closing_page_||native_trace_page_closing_||(!session.empty()&&!sessions_.contains(session))){finish();return;}std::string projected,projection_error;const bool lossless=ok&&suitecut::DevToolsLosslessProjection(json,projected,projection_error);auto parsed=lossless?CefParseJSON(projected,JSON_PARSER_RFC):nullptr;auto response=parsed?parsed->GetDictionary():nullptr;
      suitecut::TraceDOMTree converter;std::vector<suitecut::TraceDOMTree::Document> documents;std::string error;
      if(!ok||!converter.Convert(response,styles,documents,error)){traces->CaptureError(lease,error.empty()?"Native trace DOM capture failed":error);finish();return;}
      for(const auto& document:documents){const auto frame=frame_states_.find(document.frame);const auto expected=loaders.find(document.frame);if(frame==frame_states_.end()||expected==loaders.end()||frame->second.loader!=expected->second||frame->second.session!=session)continue;
        auto data=CefDictionaryValue::Create();data->SetString("callId","call@"+std::to_string(call));data->SetString("phase",phase);data->SetString("pageId","page@"+std::to_string(page_id_));data->SetString("frameId",document.frame);suitecut::DevToolsSetString(data,"frameUrl",document.url);data->SetString("snapshotName",phase+"@"+std::to_string(call)+"-"+std::to_string(snapshot));if(document.frame==main_frame_id_){auto viewport=CefDictionaryValue::Create();viewport->SetInt("width",width_);viewport->SetInt("height",height_);data->SetDictionary("viewport",viewport);}
        auto value=CefValue::Create();value->SetDictionary(data);std::string snapshot;if(!suitecut::DevToolsLosslessJSON(value,snapshot,16*1048576)||snapshot.empty()){traces->CaptureError(lease,"Native trace snapshot metadata byte limit exceeded");continue;}snapshot.pop_back();snapshot+=",\"html\":"+document.html_json+"}";
        auto event=CefDictionaryValue::Create();event->SetString("type","frame-snapshot");event->SetDouble("timestamp",Now());auto encoded=Json(event);encoded.pop_back();encoded+=",\"snapshot\":"+snapshot+"}";traces->DOMCaptured(lease,encoded);
      }finish();
    };
    native_trace_protocol_.emplace(assigned,NativeTraceProtocolCall{session,deadline,false,std::move(callback)});
    if(!SubmitDevTools(assigned,"DOMSnapshot.captureSnapshot",params,session)){auto work=std::move(native_trace_protocol_.at(assigned));native_trace_protocol_.erase(assigned);NativeTracePhysicalRelease(work);work.reply(false,"Native trace DOM dispatch failed");}
    WakeDeadlines();
  }
}
bool NativeTraceProtocolResult(int id,bool ok,const void* bytes,size_t size,const std::string& session){
  const auto found=native_trace_protocol_.find(id);if(found==native_trace_protocol_.end())return false;if(found->second.session!=session){Error("Native trace protocol session mismatch");return true;}
  auto work=std::move(found->second);native_trace_protocol_.erase(found);NativeTracePhysicalRelease(work);if(work.reply){if(size>work.response_limit)work.reply(false,"Native trace DOM response byte capacity exceeded");else work.reply(ok,size?std::string(static_cast<const char*>(bytes),size):"{}");}return true;
}
bool NativeTraceDispatchReady(int id){const auto found=native_trace_protocol_.find(id);if(found==native_trace_protocol_.end())return false;if(!found->second.expired&&(found->second.deadline<=0||Now()<found->second.deadline))return true;auto work=std::move(found->second);native_trace_protocol_.erase(found);NativeTracePhysicalRelease(work);if(work.reply)work.reply(false,"Native trace DOM capture expired before dispatch");return false;}
void NativeTraceDispatchFailed(int id){const auto found=native_trace_protocol_.find(id);if(found==native_trace_protocol_.end())return;auto work=std::move(found->second);native_trace_protocol_.erase(found);NativeTracePhysicalRelease(work);if(work.reply)work.reply(false,"Native trace DOM protocol dispatch failed");}
void NativeTraceSessionDetached(const std::string& session){std::vector<std::function<void(bool,const std::string&)>> replies;for(auto it=native_trace_protocol_.begin();it!=native_trace_protocol_.end();)if(it->second.session==session){if(it->second.reply)replies.push_back(std::move(it->second.reply));NativeTracePhysicalRelease(it->second);it=native_trace_protocol_.erase(it);}else++it;for(auto& reply:replies)reply(false,"Native trace frame target detached");}
void NativeTracePhysicalRelease(NativeTraceProtocolCall& work){engine_.TraceRelease(work.dom);if(work.release){auto release=std::move(work.release);release();}}
#include "trace_phases.inc"
bool NativeTraceNeedsTick() const{for(const auto& [id,work]:native_trace_protocol_)if(!work.expired)return true;return !native_wait_until_.empty()||NativeTraceScreenshots()||(native_trace_phases_&&native_trace_phases_->NeedsTick());}
void NativeTraceTick(){
  if(native_trace_phases_)native_trace_phases_->Tick();
  if(NativeTraceScreenshots()&&Now()-native_trace_last_invalidate_>=100){native_trace_last_invalidate_=Now();browser_->GetHost()->Invalidate(PET_VIEW);}
  std::vector<std::function<void(bool,const std::string&)>> expired;for(auto& [id,work]:native_trace_protocol_)if(!work.expired&&work.deadline>0&&Now()>=work.deadline){work.expired=true;if(work.reply)expired.push_back(std::move(work.reply));}for(auto& reply:expired)reply(false,"Native trace DOM capture deadline expired");
  std::vector<int> waited;for(const auto& [id,until]:native_wait_until_)if(Now()>=until)waited.push_back(id);for(int id:waited)Complete(id,true);
}
void NativeTraceClear(){if(native_trace_phases_)native_trace_phases_->Clear();for(auto& [id,work]:native_trace_protocol_)NativeTracePhysicalRelease(work);native_trace_protocol_.clear();native_trace_api_.clear();native_wait_until_.clear();}
bool NativeTraceBegin(int id,Dict message,const std::string& command,double timeout){
  const int context=engine_.contexts()->ContextId(page_id_);const auto traces=engine_.traces();const auto lease=traces->CaptureLease(context);if(!lease.context)return false;const auto before=suitecut::TraceActionMetadata(message,page_id_,Now());if(!before)return false;const auto call=traces->NextCall();if(!call){traces->CaptureError(lease,"Native trace action identity exhausted");return false;}before->SetString("callId","call@"+std::to_string(call));traces->Before(context,call,before);native_trace_api_[id]={lease,before->GetString("method").ToString(),call};
  const auto options=traces->Configuration(context);const bool phases=suitecut::TraceActionHasPhases(command);native_trace_api_[id].phases=phases;
  if(!options||(!options->dom&&!options->action_png&&!options->aria)||!phases){return false;}const auto operation=operations_.Get(id);if(!operation)return false;const double deadline=operation->deadline_ms>0?std::min(operation->deadline_ms,Now()+5000):Now()+5000;
  auto dispatch=[this,id,generation=operation->generation,message=message->Copy(false),command,timeout]{if(operations_.IsCurrent(id,generation)&&browser_&&!closing_page_){const auto original=operations_.Get(id);if(original&&original->deadline_ms>0&&Now()>=original->deadline_ms)Complete(id,false,"Native operation timed out");else DispatchCommand(id,message,command,timeout);}};
  NativeTraceDOM(lease,call,"before",deadline,[this,lease,call,deadline,dispatch=std::move(dispatch)]()mutable{NativeTracePhase(lease,call,"before",deadline,std::move(dispatch));});return true;
}
void NativeTraceEnd(int id,bool ok,const std::string& error,const std::string& json){
  native_wait_until_.erase(id);const auto found=native_trace_api_.find(id);if(found==native_trace_api_.end())return;const auto record=found->second;native_trace_api_.erase(found);const auto traces=engine_.traces();auto after=CefDictionaryValue::Create();after->SetString("type","after");after->SetString("callId","call@"+std::to_string(record.call));after->SetDouble("endTime",Now());if(!ok){auto failure=CefDictionaryValue::Create();failure->SetString("message",error);after->SetDictionary("error",failure);}else if(record.method=="screenshot"||record.method=="pdf"){auto result=CefDictionaryValue::Create();result->SetString("binary","<Buffer>");after->SetDictionary("result",result);}else if(!json.empty()&&json.size()<65536){auto parsed=CefParseJSON(json,JSON_PARSER_RFC);if(parsed)after->SetValue("result",parsed->Copy());}traces->After(record.lease.context,record.call,after);
  if(ok&&suitecut::TraceActionCapturesAfterDOM(record.method)&&traces->ChunkCurrent(record.lease)){if((record.method=="screenshot"||record.method=="pdf")&&artifacts_)artifacts_->Cancel(id);const double deadline=Now()+5000;auto after=[this,record,deadline]{NativeTraceDOM(record.lease,record.call,"after",deadline,[this,record,deadline]{if(record.phases)NativeTracePhase(record.lease,record.call,"after",deadline);});};if(record.phases)NativeTracePhase(record.lease,record.call,"action",deadline,std::move(after));else after();}
}
