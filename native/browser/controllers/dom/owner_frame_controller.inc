// SPDX-License-Identifier: MIT
// Genuine owner-document backend identity is compared only within the same
// actual Chromium V8 isolate. Backend integer IDs alone can collide across
// renderer processes. No selector/name/URL or generated author marker is used.
std::unique_ptr<suitecut::OwnerFrameControls> owner_frame_controls_;
suitecut::OwnerFrameControls& OwnerFramePeers(){
 if(!owner_frame_controls_)owner_frame_controls_=std::make_unique<suitecut::OwnerFrameControls>([]{return Now();},[this](const std::string&error){NetworkFatal(error);});
 return *owner_frame_controls_;
}
struct OwnerFrameReply {
 bool settled=false;std::function<void(bool,const std::string&)>finish;
 void Settle(bool ok,const std::string&json){if(settled)return;settled=true;auto complete=std::move(finish);complete(ok,json);}
 ~OwnerFrameReply(){if(!settled)Settle(false,"{\"message\":\"Actual peer frame-owner channel was destroyed\"}");}
};
struct OwnerFrameCandidate {CefRefPtr<Source>source;std::string frame;};
struct OwnerFrameWork {
 int owner=0,handle=0,context=0,utility=0,backend=0;uint64_t generation=0,context_generation=0;
 std::string frame,session,loader,node,isolate,context_identity,utility_identity;size_t index=0,requests=0;
 std::vector<OwnerFrameCandidate>candidates;
};
bool OwnerFrameCurrent(const std::shared_ptr<OwnerFrameWork>&work){
 if(!browser_||closing_page_||(!work->session.empty()&&!sessions_.contains(work->session))||!operations_.IsCurrent(work->owner,work->generation))return false;
 const auto frame=frame_states_.find(work->frame);const auto handle=handles_.find(work->handle);
 if(frame==frame_states_.end()||frame->second.context!=work->context||frame->second.utility_context!=work->utility||frame->second.context_generation!=work->context_generation||frame->second.context_identity!=work->context_identity||frame->second.utility_identity!=work->utility_identity||frame->second.session!=work->session||frame->second.loader!=work->loader||handle==handles_.end()||handle->second.node_id!=work->node){Complete(work->owner,false,"Native element owner context was destroyed or disposed");return false;}
 return true;
}
struct OwnerFrameDocumentJob {
 int id=0;std::string session,group;std::shared_ptr<suitecut::OwnerFrameDocument>read;
 std::shared_ptr<suitecut::OwnerFrameControls::Physical>remote;
 std::shared_ptr<suitecut::SelectorWaitControls::Physical>origin;
};
std::map<int,std::shared_ptr<OwnerFrameDocumentJob>> owner_frame_documents_;
void OwnerFrameDocumentCommand(const std::shared_ptr<OwnerFrameDocumentJob>&job,suitecut::OwnerFrameDocument::Method method,const std::string&object,int context,std::function<void(suitecut::OwnerFrameDocument::Reply)>done){
 using Read=suitecut::OwnerFrameDocument;
 if(!browser_||closing_page_){job->read->ChannelClosed();return;}
 auto params=CefDictionaryValue::Create();std::string command;
 if(method==Read::Method::Isolate)command="Runtime.getIsolateId";
 else if(method==Read::Method::Document){command="Runtime.evaluate";params->SetString("expression","globalThis.document");params->SetInt("contextId",context);params->SetString("objectGroup",job->group);params->SetBool("returnByValue",false);}
 else if(method==Read::Method::Describe){command="DOM.describeNode";params->SetString("objectId",object);params->SetInt("depth",0);}
 else{command="Runtime.releaseObjectGroup";params->SetString("objectGroup",job->group);}
 const int call=AllocateDevToolsId();if(!call){done({});return;}
 auto reply=std::make_shared<OwnerFrameReply>();reply->finish=[method,done=std::move(done)](bool ok,const std::string&json)mutable{
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;Read::Reply value;
  value.ok=ok&&result&&!result->HasKey("exceptionDetails");
  if(!value.ok)value.error=result&&result->GetType("message")==VTYPE_STRING?result->GetString("message").ToString():"Actual owner document protocol reply failed";
  if(value.ok&&method==Read::Method::Isolate){if(result->GetType("id")==VTYPE_STRING)value.isolate=result->GetString("id");else value.ok=false;}
  if(value.ok&&method==Read::Method::Document){auto node=result->GetDictionary("result");if(node&&node->GetString("subtype")=="node"&&node->GetType("objectId")==VTYPE_STRING)value.object=node->GetString("objectId");else value.ok=false;}
  if(value.ok&&method==Read::Method::Describe){auto node=result->GetDictionary("node");if(node&&node->GetInt("nodeType")==9&&node->GetType("backendNodeId")==VTYPE_INT)value.backend=node->GetInt("backendNodeId");else value.ok=false;}
  done(std::move(value));
 };
 devtools_.emplace(call,DevToolsCall{0,[reply](bool ok,const std::string&json){reply->Settle(ok,json);},job->session,true,1048576,true,method==Read::Method::Describe||method==Read::Method::Document});WakeDeadlines();
 if(!SubmitDevTools(call,command,params,job->session)){const auto found=devtools_.find(call);if(found!=devtools_.end()){auto failed=std::move(found->second.callback);devtools_.erase(found);failed(false,"{}");}}
}
bool OwnerFrameCandidateCurrent(CefRefPtr<Source>peer,const std::string&frame,int context,int utility,const std::string&session,const std::string&loader){
 const auto found=peer->frame_states_.find(frame);return peer->browser_&&!peer->closing_page_&&(session.empty()||peer->sessions_.contains(session))&&found!=peer->frame_states_.end()&&found->second.context==context&&found->second.utility_context==utility&&found->second.session==session&&found->second.loader==loader;
}
void OwnerFrameReadCandidate(const std::shared_ptr<OwnerFrameWork>&work,const OwnerFrameCandidate&candidate){
 if(!OwnerFrameCurrent(work))return;
 auto peer=candidate.source;const auto frame=peer->frame_states_.find(candidate.frame);
 if(!peer->browser_||peer->closing_page_||frame==peer->frame_states_.end()||(!frame->second.session.empty()&&!peer->sessions_.contains(frame->second.session))){OwnerFrameNext(work);return;}
 if(!frame->second.context||!frame->second.utility_context){peer->EnsureUtilityWorld(candidate.frame);CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),work,candidate]{if(self->OwnerFrameCurrent(work))self->OwnerFrameReadCandidate(work,candidate);}),10);return;}
 if(++work->requests>4096){Complete(work->owner,false,"Native element owner frame work limit exceeded");return;}
 auto physical=SelectorWaits().Begin(work->owner);auto remote=peer->OwnerFramePeers().Begin();
 if(!physical||!remote){Complete(work->owner,false,"Native element owner frame physical capacity exceeded");return;}
 const int token=peer->AllocateDevToolsId();if(!token){Complete(work->owner,false,"Native element owner frame command identity exhausted");return;}
 const int context=frame->second.context,utility=frame->second.utility_context;
 const auto session=frame->second.session,loader=frame->second.loader,frame_id=candidate.frame;
 auto job=std::make_shared<OwnerFrameDocumentJob>();job->id=token;job->session=session;job->group="suitecut-owner-document-"+std::to_string(token);job->remote=std::move(remote);job->origin=std::move(physical);
 const auto original=CefRefPtr<Source>(this);
 job->read=std::make_shared<suitecut::OwnerFrameDocument>(
  [peer,job,utility](auto method,const std::string&object,auto done){peer->OwnerFrameDocumentCommand(job,method,object,utility,std::move(done));},
  [original,work]{return original->OwnerFrameCurrent(work);},
  [peer,frame_id,context,utility,session,loader]{return peer->OwnerFramePeers().Healthy()&&peer->OwnerFrameCandidateCurrent(peer,frame_id,context,utility,session,loader);},
  [original,peer,work,token,frame_id,context,utility,session,loader](suitecut::OwnerFrameDocument::Result result){
   const auto found=peer->owner_frame_documents_.find(token);if(found!=peer->owner_frame_documents_.end()){auto held=std::move(found->second);peer->owner_frame_documents_.erase(found);held->origin->Settle();held->remote->Settle();held->read.reset();}
   if(!original->OwnerFrameCurrent(work))return;
   if(!result.ok&&peer->OwnerFrameCandidateCurrent(peer,frame_id,context,utility,session,loader)){original->Complete(work->owner,false,result.error.empty()?"Native owner document protocol read failed":result.error);return;}
   if(result.ok&&result.isolate==work->isolate&&result.backend==work->backend){auto output=CefDictionaryValue::Create();output->SetInt("pageId",peer->page_id_);output->SetString("frame",frame_id);original->Complete(work->owner,true,"",Json(output));return;}original->OwnerFrameNext(work);
  },[peer](const std::string&error){peer->OwnerFramePeers().Failed(error);},work->isolate,Now()+30000);
 peer->owner_frame_documents_.emplace(token,job);peer->WakeDeadlines();job->read->Start();
}
void OwnerFrameNext(const std::shared_ptr<OwnerFrameWork>&work){
 if(!OwnerFrameCurrent(work))return;
 if(work->index<work->candidates.size()){const auto candidate=work->candidates[work->index++];OwnerFrameReadCandidate(work,candidate);return;}
 Complete(work->owner,true,"","null");
}
void OwnerFrameDocumentsClosed(){
 auto documents=std::move(owner_frame_documents_);owner_frame_documents_.clear();
 for(auto&[id,job]:documents){job->read->ChannelClosed();job->origin->Settle();job->remote->Settle();job->read.reset();}
 if(owner_frame_controls_)owner_frame_controls_->Close();
}
void OwnerFrameDocumentsDetached(const std::string&session){auto documents=owner_frame_documents_;for(const auto&[id,job]:documents)if(job->session==session)job->read->ChannelClosed();}
void OwnerFrameDocumentsTick(){if(owner_frame_controls_)owner_frame_controls_->Tick();}
size_t OwnerFrameDocumentGroups()const{size_t groups=0;for(const auto&[id,job]:owner_frame_documents_)if(job->read&&job->read->GroupOwned())++groups;return groups;}
bool OwnerFrameDocumentsNeedTick()const{return !owner_frame_documents_.empty();}
void OwnerFrameOriginIsolate(const std::shared_ptr<OwnerFrameWork>&work,int backend){
 if(!OwnerFrameCurrent(work))return;
 work->backend=backend;
 DevTools(work->owner,"Runtime.getIsolateId",CefDictionaryValue::Create(),[this,work](bool ok,const std::string&json){
  if(!OwnerFrameCurrent(work))return;
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
  if(!ok||!result||result->GetType("id")!=VTYPE_STRING||result->GetString("id").empty()||result->GetString("id").length()>256){Complete(work->owner,false,"Native element owner renderer identity unavailable");return;}work->isolate=result->GetString("id");OwnerFrameNext(work);
 });
}
void OwnerFrameResolveOrigin(const std::shared_ptr<OwnerFrameWork>&work,int backend,int type,const std::string&group,std::function<void(const std::string&)>done){
 using Origin=suitecut::OwnerFrameOrigin;
 auto transaction=std::make_shared<Origin>(
  [this,work,backend,group](Origin::Method method,const std::string&object,std::function<void(Origin::Reply)>reply){
   auto params=CefDictionaryValue::Create();std::string command;
   if(method==Origin::Method::UtilityBefore||method==Origin::Method::UtilityAfter){command="Runtime.evaluate";params->SetString("expression","globalThis.document");params->SetString("uniqueContextId",work->utility_identity);params->SetString("objectGroup",group);params->SetBool("returnByValue",false);}
   else if(method==Origin::Method::Resolve){command="DOM.resolveNode";params->SetInt("backendNodeId",backend);params->SetInt("executionContextId",work->utility);params->SetString("objectGroup",group);}
   else{command="DOM.describeNode";params->SetString("objectId",method==Origin::Method::DescribeRetained?work->node:object);params->SetInt("depth",0);}
   DevTools(work->owner,command,params,[method,reply=std::move(reply)](bool ok,const std::string&json)mutable{
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;Origin::Reply value;
    value.ok=ok&&result&&!result->HasKey("exceptionDetails");
    if(!value.ok){value.code=result?result->GetInt("code"):0;value.error=result&&result->GetType("message")==VTYPE_STRING?result->GetString("message").ToString():"Native element owner protocol context validation failed";}
    if(value.ok&&(method==Origin::Method::UtilityBefore||method==Origin::Method::UtilityAfter||method==Origin::Method::Resolve)){
     auto node=result->GetDictionary(method==Origin::Method::Resolve?"object":"result");value.node=node&&node->GetString("type")=="object"&&node->GetString("subtype")=="node";if(node&&node->GetType("objectId")==VTYPE_STRING)value.object=node->GetString("objectId");
    }else if(value.ok){auto node=result->GetDictionary("node");if(node&&node->GetType("nodeType")==VTYPE_INT&&node->GetType("backendNodeId")==VTYPE_INT){value.type=node->GetInt("nodeType");value.backend=node->GetInt("backendNodeId");}}
    reply(std::move(value));
   },std::nullopt,false,1048576,true);
  },[this,work]{return OwnerFrameCurrent(work);},
  [this,work,done=std::move(done)](Origin::Result result){if(!OwnerFrameCurrent(work))return;if(!result.ok){Complete(work->owner,false,result.error);return;}if(result.no_owner){Complete(work->owner,true,"","null");return;}done(result.object);},backend,type);
 transaction->Start();
}
void OwnerFramePrepared(int id,uint64_t generation,int handle_id,const std::string&node,const std::string&frame_id,int utility){
  if(!operations_.IsCurrent(id,generation))return;
  const auto frame=frame_states_.find(frame_id);const auto handle=handles_.find(handle_id);
  if(frame==frame_states_.end()||handle==handles_.end()||handle->second.node_id!=node||!frame->second.context||frame->second.utility_context!=utility){Complete(id,false,"Native element owner execution context was destroyed or disposed");return;}
  if(frame->second.context_identity.empty()||frame->second.utility_identity.empty()){
   if(!CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,handle_id,node,frame_id,utility]{self->OwnerFramePrepared(id,generation,handle_id,node,frame_id,utility);}),10)){Complete(id,false,"Native owner execution context identity retry could not be scheduled");}
   return;
  }
  auto work=std::make_shared<OwnerFrameWork>();work->owner=id;work->handle=handle_id;work->generation=generation;work->node=node;work->frame=frame_id;work->context=frame->second.context;work->utility=utility;work->session=frame->second.session;work->loader=frame->second.loader;work->context_generation=frame->second.context_generation;work->context_identity=frame->second.context_identity;work->utility_identity=frame->second.utility_identity;
  // Prioritize the actual execution frame, then the real current page, then
  // context peers. The identity comparison, never this order, decides ownership.
  work->candidates.push_back({this,frame_id});
  const auto peers=engine_.OwnerFrameSources(page_id_);
  for(auto peer:peers)for(const auto&[id,state]:peer->frame_states_){if(peer.get()==this&&id==frame_id)continue;if(id.size()>256||work->candidates.size()>=4096){Complete(work->owner,false,"Native element owner frame inventory limit exceeded");return;}work->candidates.push_back({peer,id});}
  if(!OwnerFrameCurrent(work))return;
  const auto group="suitecut-automation-"+std::to_string(id);evaluation_groups_[id].insert(work->session);
  auto original=CefDictionaryValue::Create();original->SetString("objectId",node);original->SetInt("depth",0);
  DevTools(work->owner,"DOM.describeNode",original,[this,work,group,node](bool ok,const std::string&json){
   if(!OwnerFrameCurrent(work))return;
   auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto description=result?result->GetDictionary("node"):nullptr;
   if(!ok||!description||description->GetType("nodeType")!=VTYPE_INT||description->GetType("backendNodeId")!=VTYPE_INT||description->GetInt("backendNodeId")<=0){Complete(work->owner,false,"Native element owner retained node metadata unavailable");return;}
   // A genuine Document is its own owner-document identity. Chromium cannot
   // rewrap a createHTMLDocument node in another browsing document's world;
   // its real nodeType/backend identity needs no JavaScript realm transfer.
   if(description->GetInt("nodeType")==9){OwnerFrameOriginIsolate(work,description->GetInt("backendNodeId"));return;}
  OwnerFrameResolveOrigin(work,description->GetInt("backendNodeId"),description->GetInt("nodeType"),group,[this,work,group](const std::string&node){
   auto params=CefDictionaryValue::Create();params->SetString("objectId",node);params->SetString("objectGroup",group);params->SetBool("returnByValue",false);
   params->SetString("functionDeclaration","function(){const document=Object.getOwnPropertyDescriptor(Node.prototype,'ownerDocument').get.call(this);return document||this;}");
   DevTools(work->owner,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string&json){
    if(!OwnerFrameCurrent(work))return;
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("result"):nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")||!node||node->GetString("subtype")!="node"||node->GetType("objectId")!=VTYPE_STRING){Complete(work->owner,false,"Native element owner document unavailable");return;}
    auto params=CefDictionaryValue::Create();params->SetString("objectId",node->GetString("objectId"));params->SetInt("depth",0);
    DevTools(work->owner,"DOM.describeNode",params,[this,work](bool ok,const std::string&json){
     if(!OwnerFrameCurrent(work))return;
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("node"):nullptr;
     if(!ok||!node||node->GetInt("nodeType")!=9||node->GetType("backendNodeId")!=VTYPE_INT||node->GetInt("backendNodeId")<=0){Complete(work->owner,false,"Native element owner document backend unavailable");return;}OwnerFrameOriginIsolate(work,node->GetInt("backendNodeId"));
    });
   },std::nullopt,false,1048576,true);
  });
  });
}
void ElementOwnerFrame(int id,Dict config){
 const int handle_id=config->GetInt("handle");
 const auto entry=operations_.Get(id);if(!entry)return;const auto found=handles_.find(handle_id);
 if(found==handles_.end()||found->second.node_id.empty()){Complete(id,false,"Native element owner requires an actual retained DOM node");return;}
 if(found->second.frame_id!=FrameId(config)){Complete(id,false,"Native handle belongs to a different frame context");return;}
 if(!SelectorWaits().Admit(id,entry->generation)){Complete(id,false,"Native element owner preparation capacity exceeded");return;}
 const auto node=found->second.node_id,frame_id=found->second.frame_id;
 WithFrame(id,frame_id,[this,id,generation=entry->generation,handle_id,node,frame_id](int utility){
  OwnerFramePrepared(id,generation,handle_id,node,frame_id,utility);
 },true);
}
