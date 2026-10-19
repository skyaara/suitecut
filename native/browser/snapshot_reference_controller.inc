// SPDX-License-Identifier: MIT
// Included inside Source, after the native semantic capture implementation.
// Only the Source's physical lifetime owns this controller. Logical operations
// may disappear while the exact installer/release callbacks retain their groups.
using SnapshotReferenceDocument=suitecut::SnapshotReferences::Document;
using SnapshotReferenceCapture=suitecut::SnapshotReferenceCapture;
std::unique_ptr<suitecut::SnapshotReferences> snapshot_references_;
std::optional<SnapshotReferenceDocument> ReferenceDocument(const std::string& id)const{
  const auto frame=frame_states_.find(id);if(renderer_failed_||frame==frame_states_.end()||(!frame->second.session.empty()&&!sessions_.contains(frame->second.session)))return{};
  const auto& value=frame->second;
  if(!value.context||!value.utility_context||value.context_identity.empty()||value.utility_identity.empty())return{};
  return SnapshotReferenceDocument{value.id,value.session,value.loader,value.context_identity,value.utility_identity,value.context,value.utility_context,value.context_generation};
}
#include "snapshot_reference_current.inc"
suitecut::SnapshotReferences& SnapshotReferences(){
  if(!snapshot_references_)snapshot_references_=std::make_unique<suitecut::SnapshotReferences>(
    handle_namespace_,
    [this](int owner,uint64_t generation,const SnapshotReferenceDocument& document){
      const auto current=ReferenceDocument(document.frame);
      return operations_.IsCurrent(owner,generation)&&!operations_.IsExpired(owner,generation,Now())&&current&&*current==document;
    },
    [this](const suitecut::SnapshotReferences::Group& group,std::function<void(bool,const std::string&)> ack){
      if(!browser_){ack(false,"Native snapshot reference browser is unavailable");return;}
      if(!group.document.session.empty()&&!sessions_.contains(group.document.session)){ack(true,"");return;}
      const int tool=AllocateDevToolsId();if(!tool){ack(false,"Native snapshot reference release ID exhausted");return;}
      auto params=CefDictionaryValue::Create();params->SetString("objectGroup",group.name);const auto session=group.document.session;
      devtools_.emplace(tool,DevToolsCall{0,[ack=std::move(ack)](bool ok,const std::string&)mutable{ack(ok,ok?"":"Native snapshot reference group release failed");},session,true,65536,true,false});
      if(!SubmitDevTools(tool,"Runtime.releaseObjectGroup",params,session)){
        const auto found=devtools_.find(tool);if(found!=devtools_.end()){auto callback=std::move(found->second.callback);devtools_.erase(found);callback(false,"");}
      }
      WakeDeadlines();
    },[this](const std::string& error){NetworkFatal(error);});
  return *snapshot_references_;
}
void ReferenceRequest(const std::shared_ptr<SnapshotReferenceCapture>& work,const std::string& method,Dict params,std::function<void(Dict)> done){
  if(!ReferenceCurrent(work))return;
  if(work->before_request&&!work->before_request())return;
  if(++work->requests>work->request_limit){Complete(work->owner,false,"Native snapshot reference capture work limit exceeded");return;}
  auto callback=[this,work,done=std::move(done)](bool ok,const std::string& json)mutable{
    if(!ReferenceCurrent(work))return;
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")){
      const auto details=result?result->GetDictionary("exceptionDetails"):nullptr;const auto exception=details?details->GetDictionary("exception"):nullptr;
      const auto cause=exception?suitecut::DevToolsString(exception->GetValue("description")):result?result->GetString("message").ToString():"";
      Complete(work->owner,false,cause.empty()?"Native snapshot reference capture command failed":cause.substr(0,4096));return;
    }
    done(result);
  };
  if(!work->ticket){DevTools(work->owner,method,params,std::move(callback),work->document.session,true,1048576,true);return;}
  auto lease=SnapshotReferences().Use(work->ticket);
  if(!lease){Complete(work->owner,false,"Native snapshot reference physical read capacity exceeded");return;}
  ReferenceSubmit(work->owner,work->generation,work->document.session,method,params,[]{return true;},
    [callback=std::move(callback),lease=std::move(lease)](bool submitted,bool ok,const std::string& json)mutable{
      // Settlement occurs even when the original operation disappeared. Keep
      // the actual group alive through callback execution, then release once.
      callback(submitted&&ok,json);lease.reset();
    });
}
void PublishReferenceSnapshot(const std::shared_ptr<SnapshotReferenceCapture>& work){
  if(!ReferenceCurrent(work))return;
  BeginReferenceFrameBatch(work);
}
void InstallReferenceSnapshot(const std::shared_ptr<SnapshotReferenceCapture>& work,std::vector<suitecut::SnapshotReferences::Node> nodes){
  if(!ReferenceCurrent(work))return;
  const auto operation=operations_.Get(work->owner);if(!operation)return;
  work->ticket=SnapshotReferences().Begin(work->owner,work->generation,work->document,std::move(nodes),operation->deadline_ms,Now());
  if(!work->ticket){Complete(work->owner,false,"Native snapshot reference resident capacity exceeded");return;}
  const auto group=SnapshotReferences().GroupFor(work->ticket);
  if(!group){Complete(work->owner,false,"Native snapshot reference staging ownership unavailable");return;}
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->capture);params->SetString("functionDeclaration","function(){return this;}");params->SetString("objectGroup",group->name);params->SetBool("returnByValue",false);
  // Installation consumes the temporary capture object. Hold only its existing
  // selector physical lease until the actual installer ACK, without adding the
  // persistent destination group to the selector's temporary group registry.
  auto temporary=selector_waits_&&selector_waits_->Contains(work->owner)?selector_waits_->Begin(work->owner):nullptr;
  if(!temporary){SnapshotReferences().Rejected(work->ticket);Complete(work->owner,false,"Native reference snapshot temporary capture lease unavailable");return;}
  // Unlike ordinary continuations this callback must run after cancellation:
  // only the real ACK can settle installation and release its late-created group.
  ReferenceSubmit(work->owner,work->generation,work->document.session,"Runtime.callFunctionOn",params,
    [this,work]{return SnapshotReferences().Dispatched(work->ticket);},[this,work,temporary](bool submitted,bool ok,const std::string& json){
    if(!submitted){SnapshotReferences().Rejected(work->ticket);if(ReferenceCurrent(work))Complete(work->owner,false,"Native snapshot reference installation submission failed");return;}
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;const auto remote=result?result->GetDictionary("result"):nullptr;
    const bool installed=ok&&result&&!result->HasKey("exceptionDetails")&&remote&&remote->GetString("type")=="object"&&remote->GetType("objectId")==VTYPE_STRING&&!remote->GetString("objectId").empty();
    const bool retained=SnapshotReferences().Installed(work->ticket,installed,installed?remote->GetString("objectId").ToString():std::string(),Now());
    if(!ReferenceCurrent(work))return;
    if(!retained){Complete(work->owner,false,"Native snapshot reference group installation failed");return;}
    auto done=std::move(work->installed_done);
    if(done)done();else PublishReferenceSnapshot(work);
  });
}
void ReadReferenceSnapshot(const std::shared_ptr<SnapshotReferenceCapture>& work){
  if(!ReferenceCurrent(work))return;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->capture);params->SetString("functionDeclaration","function(){return this.elements;}");params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
  auto serialization=CefDictionaryValue::Create();serialization->SetString("serialization","deep");serialization->SetInt("maxDepth",1);auto additional=CefDictionaryValue::Create();additional->SetInt("maxNodeDepth",0);serialization->SetDictionary("additionalParameters",additional);params->SetDictionary("serializationOptions",serialization);
  ReferenceRequest(work,"Runtime.callFunctionOn",params,[this,work](Dict result){
    std::vector<suitecut::SnapshotReferences::Node> nodes;std::string error;
    if(!suitecut::SnapshotReferenceNodes(result,nodes,error)){Complete(work->owner,false,error);return;}
    InstallReferenceSnapshot(work,std::move(nodes));
  });
}
void BeginReferenceSnapshot(int id,Dict message){
  const auto operation=operations_.Get(id);if(!operation)return;
  if(operations_.IsExpired(id,operation->generation,Now())){Complete(id,false,"Native operation timed out");return;}
  for(const char* reserved:{"nativeQueryNodes","nativeSelectorWaitState","nativeAccessibilitySnapshot","nativeSemanticSnapshotJson","formNode"})if(message->HasKey(reserved)){Complete(id,false,"Invalid reserved native reference snapshot field");return;}
  if(message->HasKey("referenceFormat")&&(message->GetType("referenceFormat")!=VTYPE_STRING||message->GetString("referenceFormat")!="json")){Complete(id,false,"Invalid native reference snapshot format");return;}
  if(message->HasKey("referenceDepth")&&(message->GetType("referenceDepth")!=VTYPE_INT||message->GetInt("referenceDepth")<0||message->GetInt("referenceDepth")>64)){Complete(id,false,"Invalid native reference snapshot depth option");return;}
  if(message->HasKey("accessibilityBoxes")&&message->GetType("accessibilityBoxes")!=VTYPE_BOOL){Complete(id,false,"Invalid native reference snapshot boxes option");return;}
  if(const auto path=message->GetList("framePath");path&&path->GetSize()){
    ResolveFramePath(id,operation->generation,message,0,FrameId(message),[this,id](Dict resolved){BeginReferenceSnapshot(id,resolved);});return;
  }
  CaptureDOMSemantic(id,message,[](const std::string&,const std::string&){},[this,id,generation=operation->generation,message](const std::string& capture){
    if(!operations_.IsCurrent(id,generation))return;
    if(!ReferenceTemporaryGroups(id)){Complete(id,false,"Native reference snapshot temporary-group ownership unavailable");return;}
    const auto document=ReferenceDocument(operation_frames_[id]);
    if(!document){Complete(id,false,"Native reference snapshot document identity unavailable");return;}
    auto work=std::make_shared<SnapshotReferenceCapture>();work->owner=id;work->generation=generation;work->document=*document;work->capture=capture;work->boxes=message->GetBool("accessibilityBoxes");work->json=message->GetString("referenceFormat")=="json";work->depth=message->HasKey("referenceDepth")?message->GetInt("referenceDepth"):-1;
    auto params=CefDictionaryValue::Create();params->SetString("objectId",capture);params->SetString("functionDeclaration","function(){return this.scopeCount;}");params->SetBool("returnByValue",true);
    ReferenceRequest(work,"Runtime.callFunctionOn",params,[this,work](Dict result){
      const auto value=result->GetDictionary("result");
      if(!value||value->GetType("value")!=VTYPE_INT||(value->GetInt("value")!=0&&value->GetInt("value")!=1)){Complete(work->owner,false,"Native reference snapshot scope count unavailable");return;}
      if(!value->GetInt("value")){RetryContext(work->owner);return;}
      ReadReferenceSnapshot(work);
    });
  });
}
#include "snapshot_reference_frames.inc"
void ResolveSnapshotReference(int id,const std::string& token,const SuiteCutQueryWorldPlan::Nodes& scope,std::function<void(std::string)> done){
  const auto operation=operations_.Get(id);if(!operation)return;
  const auto document=ReferenceDocument(operation_frames_[id]);
  if(!document||scope.context!=document->utility){Complete(id,false,"Native snapshot reference utility scope is unavailable");return;}
  auto borrowed=SnapshotReferences().Lookup(token,*document);
  if(!borrowed){Complete(id,false,"Native snapshot reference is unknown, stale, or belongs to a different frame");return;}
  const auto index=borrowed.index;const int backend=borrowed.group->nodes[index].backend;
  auto work=std::make_shared<SnapshotReferenceCapture>();work->owner=id;work->generation=operation->generation;work->document=*document;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",borrowed.group->array);params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
  params->SetString("functionDeclaration","function(){const node=this.elements["+std::to_string(index)+"];if(!(node instanceof Element)||node.ownerDocument!==document)throw new Error('Native snapshot reference Element changed owner document');return node;}");
  ReferenceRequest(work,"Runtime.callFunctionOn",params,[this,work,borrowed,scope,backend,done=std::move(done)](Dict result)mutable{
    const auto remote=result->GetDictionary("result");if(!remote||remote->GetString("subtype")!="node"||remote->GetType("objectId")!=VTYPE_STRING){Complete(work->owner,false,"Native snapshot reference retained Element unavailable");return;}
    const auto target=remote->GetString("objectId").ToString();auto params=CefDictionaryValue::Create();params->SetString("objectId",target);
    ReferenceRequest(work,"DOM.describeNode",params,[this,work,scope,target,backend,done=std::move(done)](Dict result)mutable{
      const auto node=result->GetDictionary("node");if(!node||node->GetType("backendNodeId")!=VTYPE_INT||node->GetInt("backendNodeId")!=backend||node->GetType("nodeType")!=VTYPE_INT||node->GetInt("nodeType")!=1){Complete(work->owner,false,"Native snapshot reference backend identity changed");return;}
      auto params=CefDictionaryValue::Create();params->SetString("objectId",target);params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
      params->SetString("functionDeclaration","function(scopes){let work=0;for(const scope of scopes){if(++work>100000)throw new Error('Native snapshot reference scope work limit exceeded');let depth=0;for(let node=this;node;node=node.assignedSlot||node.parentNode||node.getRootNode()?.host||null){if(++depth>256||++work>100000)throw new Error('Native snapshot reference ancestry limit exceeded');if(node===scope)return [this];}}return [];}");
      auto args=CefListValue::Create();auto arg=CefDictionaryValue::Create();arg->SetString("objectId",scope.object);args->SetDictionary(0,arg);params->SetList("arguments",args);
      ReferenceRequest(work,"Runtime.callFunctionOn",params,[this,work,scope,done=std::move(done)](Dict result)mutable{
        const auto array=result->GetDictionary("result");if(!array||array->GetString("subtype")!="array"||array->GetType("objectId")!=VTYPE_STRING){Complete(work->owner,false,"Native snapshot reference scoped array unavailable");return;}
        done(array->GetString("objectId").ToString());
      });
    });
  });
}
#include "snapshot_reference_scope.inc"

void SnapshotReferenceRendererClosed(){
  if(!snapshot_references_)return;
  // This CEF callback attests actual main-renderer destruction, distinct from
  // logical cancellation. Separate surviving target sessions still need ACKs.
  snapshot_references_->ChannelDestroyed("");
  for(const auto&[id,frame]:frame_states_)if(!frame.session.empty())snapshot_references_->InvalidateFrame(id,Now());
  WakeDeadlines();
}
