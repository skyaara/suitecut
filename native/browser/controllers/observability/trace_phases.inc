// SPDX-License-Identifier: MIT
// Included inside Source. These phase reads use actual CEF callbacks, the
// genuine document epochs and the same physical capture reservation as PDF.
std::shared_ptr<suitecut::TracePhaseControls> native_trace_phases_;
uint64_t native_trace_png_readbacks_=0,native_trace_aria_readbacks_=0;
static std::string NativeTracePhaseCause(const std::string& json,const std::string& fallback){auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto dict=parsed?parsed->GetDictionary():nullptr;return dict&&dict->GetType("message")==VTYPE_STRING?dict->GetString("message").ToString():fallback;}
bool NativeTracePhaseProtocol(const std::string& method,Dict params,const std::string& session,double deadline,bool screenshot,std::function<void(bool,const std::string&)> reply){
  if(screenshot&&(!artifacts_||!artifacts_->ExternalCaptureAvailable()))return false;
  if(!browser_||closing_page_||native_trace_page_closing_){reply(false,"Native trace phase page is closed");return true;}
  if(native_trace_protocol_.size()>=8||!engine_.TraceClaim(false)){reply(false,"Native trace phase physical capacity exceeded");return true;}
  const int assigned=AllocateDevToolsId();if(!assigned){engine_.TraceRelease(false);reply(false,"Native trace phase protocol identity exhausted");return true;}
  if(screenshot&&!artifacts_->AcquireExternalCapture(assigned)){engine_.TraceRelease(false);return false;}
  std::function<void()> release;if(screenshot)release=[this,assigned]{if(artifacts_)artifacts_->ReleaseExternalCapture(assigned);};
  native_trace_protocol_.emplace(assigned,NativeTraceProtocolCall{session,deadline,false,std::move(reply),screenshot?12*1048576U:8*1048576U,std::move(release),false});
  if(!SubmitDevTools(assigned,method,params,session)&&native_trace_protocol_.contains(assigned))NativeTraceDispatchFailed(assigned);
  WakeDeadlines();return true;
}
void NativeTraceReadARIA(const suitecut::TracePhaseControls::Request& request,std::function<void(bool,std::string,const std::string&)> done){
  struct Document{std::string frame,session,loader;};struct Batch{size_t remaining=0;bool finished=false;std::map<std::string,CefRefPtr<CefListValue>> forests;std::function<void(bool,std::string,const std::string&)> done;};
  std::vector<Document> documents;
  for(const auto& [frame,state]:frame_states_){if(!state.context||state.loader.empty())continue;if(state.session.empty()){if(frame!=main_frame_id_)continue;}else{const auto root=sessions_.find(state.session);if(root==sessions_.end()||root->second!=frame)continue;}documents.push_back({frame,state.session,state.loader});}
  if(documents.empty()){done(false,"","Native trace accessibility document is unavailable");return;}
  auto batch=std::make_shared<Batch>();batch->remaining=documents.size();batch->done=std::move(done);
  for(const auto& document:documents){if(batch->finished)break;auto params=CefDictionaryValue::Create();params->SetString("frameId",document.frame);
    NativeTracePhaseProtocol("Accessibility.getFullAXTree",params,document.session,request.deadline,false,[this,batch,document,request](bool ok,const std::string& json){
      if(batch->finished)return;
      const auto frame=frame_states_.find(document.frame);const auto traces=engine_.traces();
      const auto fail=[batch](const std::string& cause){batch->finished=true;auto done=std::move(batch->done);if(done)done(false,"",cause);};
      if(!ok){fail(NativeTracePhaseCause(json,"Native trace accessibility read failed"));return;}
      if(!traces->ChunkCurrent(request.scope)||frame==frame_states_.end()||frame->second.session!=document.session||frame->second.loader!=document.loader){fail("Native trace accessibility document epoch changed");return;}
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto nodes=result?result->GetList("nodes"):nullptr;int backend=0;
      if(nodes)for(size_t i=0;i<nodes->GetSize();++i){auto node=nodes->GetDictionary(i);if(!node||node->GetInt("backendDOMNodeId")<=0)continue;auto role=node->GetDictionary("role");const auto name=role?suitecut::DevToolsString(role->GetValue("value")):std::string();if((name=="RootWebArea"||name=="WebArea")&&(!node->HasKey("parentId")||node->GetString("frameId")==document.frame)){backend=node->GetInt("backendDOMNodeId");break;}}
      SuiteCutAXForest projector;CefRefPtr<CefListValue> forest;std::vector<SuiteCutAXForest::Link> links;std::string error;
      if(!backend||!projector.Project(nodes,backend,forest,links,error)){fail(error.empty()?"Native trace accessibility root is absent":error);return;}
      ++native_trace_aria_readbacks_;batch->forests.emplace(document.frame,forest);if(--batch->remaining)return;
      auto combined=CefListValue::Create();size_t index=0;for(const auto& [frame,roots]:batch->forests)for(size_t i=0;i<roots->GetSize();++i)combined->SetValue(index++,roots->GetValue(i)->Copy());auto value=CefValue::Create();value->SetList(combined);std::string encoded;
      if(!suitecut::DevToolsLosslessJSON(value,encoded,65536)){fail("Native trace accessibility forest exceeds 64 KiB");return;}
      batch->finished=true;auto done=std::move(batch->done);if(done)done(true,std::move(encoded),"");
    });
  }
}
bool NativeTracePhaseDocumentCurrent(const suitecut::TracePhaseControls::Request& request){const auto frame=frame_states_.find(request.frame);return frame!=frame_states_.end()&&request.frame==main_frame_id_&&frame->second.session==request.session&&frame->second.loader==request.loader;}
bool NativeTraceReadPhase(const suitecut::TracePhaseControls::Request& request,suitecut::TracePhaseControls::Reply reply){
  if(!NativeTracePhaseDocumentCurrent(request)){reply(false,{},"Native trace phase document epoch changed");return true;}
  if(request.screen){ArtifactEnsureController();if(!artifacts_->ExternalCaptureAvailable())return false;}
  auto output=std::make_shared<suitecut::TracePhaseControls::Result>();auto completion=std::make_shared<suitecut::TracePhaseControls::Reply>(std::move(reply));
  const auto finish=[output,completion](bool ok,const std::string& error){if(*completion){auto done=std::move(*completion);done(ok,std::move(*output),error);}};
  auto aria=[this,request,output,finish]{if(!request.aria){finish(true,"");return;}NativeTraceReadARIA(request,[output,finish](bool ok,std::string bytes,const std::string& error){output->aria=std::move(bytes);finish(ok,error);});};
  if(!request.screen){aria();return true;}
  auto params=CefDictionaryValue::Create();params->SetString("format","png");params->SetBool("fromSurface",true);params->SetBool("captureBeyondViewport",false);
  return NativeTracePhaseProtocol("Page.captureScreenshot",params,"",request.deadline,true,[this,output,aria=std::move(aria),finish,request](bool ok,const std::string& json)mutable{
    if(!ok){finish(false,NativeTracePhaseCause(json,"Native trace PNG readback failed"));return;}if(!NativeTracePhaseDocumentCurrent(request)){finish(false,"Native trace PNG document epoch changed");return;}
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto data=result&&result->GetType("data")==VTYPE_STRING?CefBase64Decode(result->GetString("data")):nullptr;
    if(!data||data->GetSize()<8||data->GetSize()>8*1048576){finish(false,"Native trace PNG bytes unavailable or over 8 MiB");return;}
    output->png.resize(data->GetSize());if(data->GetData(output->png.data(),output->png.size(),0)!=output->png.size()||std::memcmp(output->png.data(),"\x89PNG\r\n\x1a\n",8)!=0){finish(false,"Native trace PNG readback bytes invalid");return;}
    ++native_trace_png_readbacks_;aria();
  });
}
void NativeTracePhase(suitecut::TraceJournal::Lease lease,uint64_t call,const std::string& phase,double deadline,std::function<void()> done={}){
  const auto traces=engine_.traces();const auto options=traces->Configuration(lease.context);if(!options||(!options->action_png&&!options->aria)||!traces->ChunkCurrent(lease)||!browser_||closing_page_||native_trace_page_closing_){if(done)done();return;}
  if(!native_trace_phases_){suitecut::TracePhaseControls::Hooks hooks;hooks.now=[]{return Now();};hooks.current=[this](auto scope){return engine_.traces()->ChunkCurrent(scope);};hooks.wake=[this]{WakeDeadlines();};hooks.error=[this](auto scope,const std::string& error){engine_.traces()->CaptureError(scope,error);};
    hooks.read=[this](const auto& request,auto reply){return NativeTraceReadPhase(request,std::move(reply));};
    hooks.captured=[this](const auto& request,auto result){const auto traces=engine_.traces();const auto emit=[&](const char* type,const std::string& file,std::vector<uint8_t> bytes){auto event=CefDictionaryValue::Create();event->SetString("type",type);event->SetString("callId","call@"+std::to_string(request.call));event->SetString("phase",request.phase);event->SetString("file",file);event->SetDouble("timestamp",Now());traces->Captured(request.scope,event,file,std::move(bytes));};const auto name="call@"+std::to_string(request.call)+"-"+request.phase;if(request.screen)emit("screenshot","screenshots/"+name+".png",std::move(result.png));if(request.aria)emit("aria-snapshot","aria/"+name+".json",std::vector<uint8_t>(result.aria.begin(),result.aria.end()));};native_trace_phases_=std::make_shared<suitecut::TracePhaseControls>(std::move(hooks));
  }
  const auto frame=frame_states_.find(main_frame_id_);if(frame==frame_states_.end()||frame->second.loader.empty()){traces->CaptureError(lease,"Native trace phase document epoch is unavailable");if(done)done();return;}
  auto continuation=std::make_shared<std::function<void()>>(std::move(done));if(!native_trace_phases_->Submit({lease,0,call,phase,deadline,options->action_png,options->aria,main_frame_id_,frame->second.session,frame->second.loader},[continuation]{if(*continuation){auto done=std::move(*continuation);done();}})){traces->CaptureError(lease,"Native trace phase capacity or deadline exceeded");if(*continuation){auto done=std::move(*continuation);done();}}
}
