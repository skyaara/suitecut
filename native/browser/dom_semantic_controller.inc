// SPDX-License-Identifier: MIT
std::string DOMSemanticQuote(const std::string& json){auto value=CefValue::Create();value->SetString(json);return CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();}
struct DOMSemanticControls {
  std::shared_ptr<AXCapture> capture;
  Dict message;
  std::string files;
  std::vector<std::string> nodes;
  size_t next=0;
  CefRefPtr<CefListValue> forests=CefListValue::Create();
  std::function<void(const std::string&,const std::string&)> done;
  std::function<void(const std::string&)> reference_done;
};
void DOMSemanticFinish(const std::shared_ptr<DOMSemanticControls>& state){
  const auto work=state->capture;if(!AXCurrent(work))return;
  auto forest_value=CefValue::Create();forest_value->SetList(state->forests->Copy());std::string native;
  if(!suitecut::DevToolsLosslessJSON(forest_value,native)){AXError(work,"Native UA control metadata exceeds 64 KiB");return;}
  if(native.size()>65536){AXError(work,"Native UA control metadata exceeds 64 KiB");return;}
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->array);params->SetBool("returnByValue",true);params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
  params->SetString("functionDeclaration","function(files){const nativeControls=new WeakMap(),forests=JSON.parse("+DOMSemanticQuote(native)+");for(let i=0;i<files.length;i++)nativeControls.set(files[i],forests[i]);return JSON.stringify(this.length?("+std::string(kSuiteCutDOMSemanticSnapshot)+")(this[0],("+std::string(kSuiteCutDomQuery)+"),{boxes:"+(state->message->GetBool("accessibilityBoxes")?"true":"false")+",nativeControls}):[]);}");
  if(state->reference_done){
    params->SetBool("returnByValue",false);
    params->SetString("functionDeclaration","function(files){const nativeControls=new WeakMap(),forests=JSON.parse("+DOMSemanticQuote(native)+");for(let i=0;i<files.length;i++)nativeControls.set(files[i],forests[i]);const capture=this.length?("+std::string(kSuiteCutDOMSemanticSnapshot)+")(this[0],("+std::string(kSuiteCutDomQuery)+"),{captureReferences:true,ai:"+(state->message->GetString("referenceFormat")=="json"?"true":"false")+",boxes:"+(state->message->GetBool("accessibilityBoxes")?"true":"false")+",nativeControls}):{forest:[],elements:[],entries:[]};capture.scopeCount=this.length;return capture;}");
  }
  auto args=CefListValue::Create();auto arg=CefDictionaryValue::Create();arg->SetString("objectId",state->files);args->SetDictionary(0,arg);params->SetList("arguments",args);
  AXRequest(work,"Runtime.callFunctionOn",params,[this,state,work](Dict result){
    if(state->reference_done){
      const auto value=result->GetDictionary("result");
      if(!value||value->GetString("type")!="object"||value->GetType("objectId")!=VTYPE_STRING||value->GetString("objectId").empty()){AXError(work,"Native semantic reference capture unavailable");return;}
      const auto capture=value->GetString("objectId").ToString();ax_captures_.erase(work->owner);state->reference_done(capture);return;
    }
    auto value=result->GetDictionary("result");if(!value||value->GetType("value")!=VTYPE_STRING){AXError(work,"Native semantic snapshot result unavailable");return;}
    const auto serialized=value->GetString("value").ToString();if(serialized.size()>65536){AXError(work,"Native semantic snapshot exceeds 64 KiB");return;}
    // JSON.stringify escapes isolated UTF16. Validate a separate projection,
    // keeping the exact original string for the actual renderer observation.
    auto valid=CefParseJSON(suitecut::StorageJSONValidationProjection(serialized),JSON_PARSER_RFC);if(!valid||valid->GetType()!=VTYPE_LIST){AXError(work,"Invalid native semantic snapshot forest");return;}
    ax_captures_.erase(work->owner);state->done(work->array,serialized);
  });
}
void DOMSemanticReadControl(const std::shared_ptr<DOMSemanticControls>& state){
  const auto work=state->capture;if(!AXCurrent(work))return;
  if(state->next==state->nodes.size()){DOMSemanticFinish(state);return;}
  const size_t index=state->next++;auto params=CefDictionaryValue::Create();params->SetString("objectId",state->nodes[index]);
  AXRequest(work,"DOM.describeNode",params,[this,state,work,index](Dict result){
    const auto node=result->GetDictionary("node");if(!node||node->GetType("backendNodeId")!=VTYPE_INT||node->GetInt("backendNodeId")<=0){AXError(work,"Native UA control backend identity unavailable");return;}
    const int backend=node->GetInt("backendNodeId");auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",backend);params->SetBool("fetchRelatives",false);
    AXRequest(work,"Accessibility.getPartialAXTree",params,[this,state,work,index,backend](Dict result){
      // Borrowed CEF child values must never escape their actual reply owner.
      const auto borrowed=result->GetList("nodes");const auto nodes=borrowed?borrowed->Copy():nullptr;
      SuiteCutAXForest projector;CefRefPtr<CefListValue> forest;std::vector<SuiteCutAXForest::Link> links;std::string error;
      if(!projector.Project(nodes,backend,forest,links,error)){AXError(work,error);return;}
      state->forests->SetList(index,forest->Copy());DOMSemanticReadControl(state);
    });
  });
}
void DOMSemanticCollectControls(const std::shared_ptr<DOMSemanticControls>& state){
  const auto work=state->capture;auto params=CefDictionaryValue::Create();params->SetString("objectId",work->array);params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
  // The retained array contains actual scoped composed-DOM file controls, not
  // IDs/names synthesized from author HTML or platform language assumptions.
  params->SetString("functionDeclaration","function(){return this.length?("+std::string(kSuiteCutDOMSemanticSnapshot)+")(this[0],("+std::string(kSuiteCutDomQuery)+"),{collectNativeControls:true}):[];}");
  AXRequest(work,"Runtime.callFunctionOn",params,[this,state,work](Dict result){
    const auto files=result->GetDictionary("result");if(!files||files->GetString("subtype")!="array"||files->GetType("objectId")!=VTYPE_STRING){AXError(work,"Native UA control array unavailable");return;}state->files=files->GetString("objectId").ToString();
    auto params=CefDictionaryValue::Create();params->SetString("objectId",state->files);params->SetBool("ownProperties",true);
    AXRequest(work,"Runtime.getProperties",params,[this,state,work](Dict result){
      auto descriptors=result->GetList("result");if(!descriptors||descriptors->GetSize()>129){AXError(work,"Invalid native UA control array properties");return;}
      int length=-1;std::map<size_t,std::string> members;
      for(size_t i=0;i<descriptors->GetSize();++i){auto descriptor=descriptors->GetDictionary(i);auto value=descriptor?descriptor->GetDictionary("value"):nullptr;if(!descriptor||!value){AXError(work,"Invalid native UA control array descriptor");return;}
        const auto key=descriptor->GetString("name").ToString();if(key=="length"){if(value->GetType("value")!=VTYPE_INT){AXError(work,"Invalid native UA control array length");return;}length=value->GetInt("value");continue;}
        if(key.empty()||key.size()>3){AXError(work,"Invalid native UA control array index");return;}size_t index=0;for(const char digit:key){if(digit<'0'||digit>'9'){AXError(work,"Invalid native UA control array index");return;}index=index*10+static_cast<size_t>(digit-'0');}
        if(index>=128||value->GetType("objectId")!=VTYPE_STRING||value->GetString("subtype")!="node"||members.contains(index)){AXError(work,"Invalid native UA control node identity");return;}members[index]=value->GetString("objectId").ToString();
      }
      if(length<0||length>128||members.size()!=static_cast<size_t>(length)){AXError(work,"Invalid native UA control array cardinality");return;}
      for(int i=0;i<length;++i){const auto member=members.find(static_cast<size_t>(i));if(member==members.end()){AXError(work,"Native UA control array has missing node");return;}state->nodes.push_back(member->second);}
      if(state->nodes.empty()){DOMSemanticFinish(state);return;}
      AXRequest(work,"Accessibility.enable",CefDictionaryValue::Create(),[this,state](Dict){DOMSemanticReadControl(state);});
    });
  });
}
// Native operation/frame/deadline and physical admission are shared with raw AX
// capture; the authored DOM forest enriches only genuine opaque UA controls.
void CaptureDOMSemantic(int id,Dict message,std::function<void(const std::string&,const std::string&)> done,std::function<void(const std::string&)> reference_done={}){
  const auto operation=operations_.Get(id);if(!operation)return;
  if(ax_captures_.size()>=64&&!ax_captures_.contains(id)){Complete(id,false,"Native accessibility capture capacity exceeded");return;}
  if(ax_capture_sequence_==std::numeric_limits<uint64_t>::max()){Complete(id,false,"Native accessibility capture sequence exhausted");return;}
  auto work=std::make_shared<AXCapture>();work->owner=id;work->generation=operation->generation;work->token=++ax_capture_sequence_;work->frame=FrameId(message);work->release=[this](size_t bytes){ax_capture_bytes_-=bytes;};ax_captures_[id]=work->token;
  auto state=std::make_shared<DOMSemanticControls>();state->capture=work;state->message=message;state->done=std::move(done);state->reference_done=std::move(reference_done);
  QueryNodesInWorld(id,message,true,[this,state,work](const std::string& array){
    const auto found=ax_captures_.find(work->owner);if(!operations_.IsCurrent(work->owner,work->generation)||found==ax_captures_.end()||found->second!=work->token)return;
    const auto frame=frame_states_.find(work->frame);if(frame==frame_states_.end()||!frame->second.context||!frame->second.utility_context){RetryContext(work->owner);return;}
    work->main=frame->second.context;work->utility=frame->second.utility_context;work->session=frame->second.session;work->loader=frame->second.loader;
    if(state->reference_done){
      const auto frame_id=work->frame,session=work->session,loader=work->loader,main_unique=frame->second.context_identity,utility_unique=frame->second.utility_identity;const int main=work->main,utility=work->utility;const auto epoch=frame->second.context_generation;
      work->extra_current=[this,id=work->owner,generation=work->generation,frame_id,session,loader,main_unique,utility_unique,main,utility,epoch]{
        if(!operations_.IsCurrent(id,generation))return false;
        if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return false;}
        const auto current=frame_states_.find(frame_id);
        if(current==frame_states_.end()||current->second.session!=session||current->second.loader!=loader||current->second.context!=main||current->second.utility_context!=utility||current->second.context_identity!=main_unique||current->second.utility_identity!=utility_unique||current->second.context_generation!=epoch){Complete(id,false,"Native semantic reference snapshot document changed");return false;}
        return true;
      };
    }
    auto params=CefDictionaryValue::Create();params->SetString("objectId",array);params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
    params->SetString("functionDeclaration","function(){if(this.length>1)throw new Error('strict mode violation: semantic snapshot locator resolved to '+this.length+' elements');return [...this];}");
    AXRequest(work,"Runtime.callFunctionOn",params,[this,state,work](Dict result){
      auto array=result->GetDictionary("result");if(!array||array->GetString("subtype")!="array"||array->GetType("objectId")!=VTYPE_STRING){AXError(work,"Native semantic snapshot scope pin failed");return;}work->array=array->GetString("objectId").ToString();DOMSemanticCollectControls(state);
    });
  });
}
