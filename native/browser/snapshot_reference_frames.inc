// SPDX-License-Identifier: MIT
// Included inside Source after the single-frame reference capture methods.
// All captures remain staging-only until one final native CommitBatch.
struct SnapshotReferenceFrameBatch {
  struct Frame {std::shared_ptr<SnapshotReferenceCapture> work;std::string forest;size_t parent=0;std::string owner_ref;};
  struct Edge {size_t parent=0,index=0;};
  int owner=0;uint64_t generation=0;size_t next=0,requests=0,nodes=0,merge=0;
  std::vector<Frame> frames;std::vector<Edge> edges;std::set<std::string> seen;
  std::vector<uint64_t> publication_tickets;bool publication_ready=false;
};
std::map<int,std::shared_ptr<SnapshotReferenceFrameBatch>> reference_frame_batches_;
bool ReferenceFramesCurrent(std::shared_ptr<SnapshotReferenceFrameBatch> batch){
  if(!operations_.IsCurrent(batch->owner,batch->generation))return false;
  const auto owned=reference_frame_batches_.find(batch->owner);
  if(owned==reference_frame_batches_.end()||owned->second!=batch)return false;
  if(operations_.IsExpired(batch->owner,batch->generation,Now())){Complete(batch->owner,false,"Native operation timed out");return false;}
  for(size_t i=0;i<batch->frames.size();++i){const auto& frame=batch->frames[i];const auto current=ReferenceDocument(frame.work->document.frame);
    const auto actual=frame_states_.find(frame.work->document.frame);
    if(!current||*current!=frame.work->document||(i&&(actual==frame_states_.end()||actual->second.parent!=batch->frames[frame.parent].work->document.frame))){Complete(batch->owner,false,"Native aggregate snapshot frame document changed");return false;}}
  return true;
}
bool ReferenceFrameOperationCurrent(int owner){
  const auto found=reference_frame_batches_.find(owner);
  return found==reference_frame_batches_.end()||ReferenceFramesCurrent(found->second);
}
// Source calls only after any real network-publication hold has drained and
// immediately before Finish. A timed-out/changed held result never evicts old refs.
bool PublishReferenceFrameBatch(int owner){
  const auto found=reference_frame_batches_.find(owner);if(found==reference_frame_batches_.end())return true;
  const auto batch=found->second;if(!ReferenceFramesCurrent(batch))return false;
  if(!batch->publication_ready){Complete(owner,false,"Native aggregate snapshot publication was not prepared");return false;}
  if(!SnapshotReferences().CommitBatch(batch->publication_tickets,Now())){
    Complete(owner,false,operations_.IsExpired(owner,batch->generation,Now())?"Native operation timed out":"Native aggregate snapshot publication generation changed");return false;}
  return true;
}
void ReferenceFramesAttach(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,const std::shared_ptr<SnapshotReferenceCapture>& work){
  const std::weak_ptr<SnapshotReferenceFrameBatch> weak=batch;
  work->all_current=[this,weak]{const auto batch=weak.lock();return batch&&ReferenceFramesCurrent(batch);};
  work->request_limit=8192;
  work->before_request=[this,weak]{const auto batch=weak.lock();if(!batch||!ReferenceFramesCurrent(batch))return false;
    if(++batch->requests>8192){Complete(batch->owner,false,"Native aggregate snapshot command limit exceeded");return false;}return true;};
}
void ReferenceFramesFinish(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch){
  if(!ReferenceFramesCurrent(batch))return;
  std::vector<std::vector<std::string>> groups;std::vector<uint64_t> tickets;
  for(const auto& frame:batch->frames){const auto group=SnapshotReferences().GroupFor(frame.work->ticket);
    if(!group){Complete(batch->owner,false,"Native aggregate snapshot group unavailable");return;}
    groups.push_back(group->references);tickets.push_back(group->ticket);}
  std::string error;
  if(!suitecut::SnapshotReferenceForests(batch->frames.front().forest,groups,error)){Complete(batch->owner,false,error);return;}
  const auto root=batch->frames.front().work;const auto group=SnapshotReferences().GroupFor(root->ticket);
  auto params=CefDictionaryValue::Create();params->SetString("objectId",group->array);params->SetString("objectGroup",group->name);params->SetBool("returnByValue",true);
  params->SetString("functionDeclaration","function(){return JSON.stringify(("+std::string(root->json?kSuiteCutNativeAriaJSON:kSuiteCutNativeAriaFormatter)+")(this.forest,{mode:'ai',boxes:"+std::string(root->boxes?"true":"false")+(root->depth<0?std::string():",depth:"+std::to_string(root->depth))+"}));}");
  ReferenceRequest(root,"Runtime.callFunctionOn",params,[this,batch,tickets=std::move(tickets)](Dict result){
    if(!ReferenceFramesCurrent(batch))return;
    const auto value=result->GetDictionary("result");
    if(!value||value->GetType("value")!=VTYPE_STRING){Complete(batch->owner,false,"Native aggregate snapshot serialized observation unavailable");return;}
    const auto snapshot=value->GetString("value").ToString();
    const auto root=batch->frames.front().work;
    const size_t bound=root->json?65536:393218;
    const auto valid=snapshot.size()<=bound?CefParseJSON(suitecut::StorageJSONValidationProjection(snapshot),JSON_PARSER_RFC):nullptr;
    if(!valid||valid->GetType()!=(root->json?VTYPE_LIST:VTYPE_STRING)){Complete(batch->owner,false,"Native aggregate snapshot serialization exceeds bounds or is invalid");return;}
    auto response=CefDictionaryValue::Create();response->SetString(root->json?"snapshotJSON":"yamlJSON",snapshot);const auto json=Json(response);
    if(!ReferenceFramesCurrent(batch))return;
    batch->publication_tickets=tickets;batch->publication_ready=true;
    Complete(batch->owner,true,"",json);
  });
}
void ReferenceFramesMerge(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch){
  if(!ReferenceFramesCurrent(batch))return;
  if(!batch->merge){ReferenceFramesFinish(batch);return;}
  const size_t index=batch->merge--;const auto child=batch->frames[index];
  const auto parent=batch->frames[child.parent].work;const auto group=SnapshotReferences().GroupFor(parent->ticket);
  if(!group){Complete(batch->owner,false,"Native aggregate snapshot parent group unavailable");return;}
  auto params=CefDictionaryValue::Create();params->SetString("objectId",group->array);params->SetString("objectGroup",group->name);params->SetBool("returnByValue",true);
  params->SetString("functionDeclaration",kSuiteCutSnapshotReferenceMerge);
  auto args=CefListValue::Create();auto ref=CefDictionaryValue::Create();ref->SetString("value",child.owner_ref);args->SetDictionary(0,ref);
  auto forest=CefDictionaryValue::Create();forest->SetString("value",child.forest);args->SetDictionary(1,forest);params->SetList("arguments",args);
  ReferenceRequest(parent,"Runtime.callFunctionOn",params,[this,batch,parent_index=child.parent](Dict result){
    const auto value=result->GetDictionary("result");
    if(!value||value->GetType("value")!=VTYPE_STRING){Complete(batch->owner,false,"Native aggregate snapshot merge unavailable");return;}
    const auto forest=value->GetString("value").ToString();if(forest.size()>65536){Complete(batch->owner,false,"Native aggregate snapshot exceeds 64 KiB");return;}
    batch->frames[parent_index].forest=forest;ReferenceFramesMerge(batch);
  });
}
void ReferenceFramesPrepared(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,size_t index){
  if(!ReferenceFramesCurrent(batch))return;
  const auto work=batch->frames[index].work;const auto group=SnapshotReferences().GroupFor(work->ticket);
  if(!group||group->nodes.size()>4096-batch->nodes){Complete(batch->owner,false,"Native aggregate snapshot node capacity exceeded");return;}
  batch->nodes+=group->nodes.size();const auto references=group->references;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",group->array);params->SetString("objectGroup",group->name);params->SetBool("returnByValue",true);
  params->SetString("functionDeclaration",kSuiteCutSnapshotReferencePublish);auto args=CefListValue::Create();auto arg=CefDictionaryValue::Create();auto tokens=CefListValue::Create();for(size_t i=0;i<references.size();++i)tokens->SetString(i,references[i]);arg->SetList("value",tokens);args->SetDictionary(0,arg);params->SetList("arguments",args);
  ReferenceRequest(work,"Runtime.callFunctionOn",params,[this,batch,index,references](Dict result){
    const auto value=result->GetDictionary("result");
    if(!value||value->GetType("value")!=VTYPE_STRING){Complete(batch->owner,false,"Native aggregate snapshot prepared forest unavailable");return;}
    const auto forest=value->GetString("value").ToString();std::vector<size_t> frames;std::string error;
    if(!suitecut::SnapshotReferenceFrameIndices(forest,references,frames,error)){Complete(batch->owner,false,error);return;}
    if(frames.size()>31-batch->edges.size()){Complete(batch->owner,false,"Native aggregate snapshot frame capacity exceeded");return;}
    batch->frames[index].forest=forest;for(const auto node:frames)batch->edges.push_back({index,node});
    ReferenceFramesNext(batch);
  });
}
void ReferenceFramesCapture(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,size_t parent_index,size_t node_index,const std::string& child,bool attached=false){
  if(!ReferenceFramesCurrent(batch))return;
  const auto parent=batch->frames[parent_index].work;
  const auto frame=frame_states_.find(child);
  if(frame==frame_states_.end()){
    // A real DOM owner can be observed before its target attachment is admitted.
    // Once actual attachment was observed, absence is genuine disappearance.
    if(attached){Complete(batch->owner,false,"Native aggregate snapshot child frame detached");return;}
  }else if(frame->second.parent!=parent->document.frame){Complete(batch->owner,false,"Native aggregate snapshot child frame parent changed");return;}
  if(frame!=frame_states_.end()){attached=true;EnsureUtilityWorld(child);}
  const auto document=ReferenceDocument(child);
  if(!document){const std::weak_ptr<SnapshotReferenceFrameBatch> weak=batch;
    CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),weak,parent_index,node_index,child,attached]{if(const auto batch=weak.lock())self->ReferenceFramesCapture(batch,parent_index,node_index,child,attached);}),10);return;}
  if(batch->frames.size()>=32||!batch->seen.insert(child).second){Complete(batch->owner,false,"Native aggregate snapshot frame cycle or capacity exceeded");return;}
  auto work=std::make_shared<SnapshotReferenceCapture>();work->owner=batch->owner;work->generation=batch->generation;work->document=*document;
  const auto root=batch->frames.front().work;work->json=root->json;work->boxes=root->boxes;work->depth=root->depth;ReferenceFramesAttach(batch,work);
  const size_t index=batch->frames.size();batch->frames.push_back({work,"",parent_index,SnapshotReferences().GroupFor(parent->ticket)->references.at(node_index)});
  const std::weak_ptr<SnapshotReferenceFrameBatch> weak=batch;
  work->installed_done=[this,weak,index]{if(const auto batch=weak.lock())ReferenceFramesPrepared(batch,index);};
  ReferenceFramesReadChild(batch,index);
}
void ReferenceFramesReadChild(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,size_t index){
  if(!ReferenceFramesCurrent(batch))return;
  const auto work=batch->frames[index].work;const auto child=work->document.frame;
  const std::weak_ptr<SnapshotReferenceFrameBatch> weak=batch;
  auto config=CefDictionaryValue::Create();config->SetString("frame",child);config->SetString("selector","body, frameset");if(work->json)config->SetString("referenceFormat","json");config->SetBool("accessibilityBoxes",work->boxes);
  CaptureDOMSemantic(batch->owner,config,[](const std::string&,const std::string&){},[this,weak,index](const std::string& capture){
    const auto batch=weak.lock();if(!batch||!ReferenceFramesCurrent(batch))return;
    if(!ReferenceTemporaryGroups(batch->owner)){Complete(batch->owner,false,"Native aggregate snapshot temporary-group ownership unavailable");return;}
    const auto work=batch->frames[index].work;work->capture=capture;
    auto params=CefDictionaryValue::Create();params->SetString("objectId",capture);params->SetString("functionDeclaration","function(){return this.scopeCount;}");params->SetBool("returnByValue",true);
    ReferenceRequest(work,"Runtime.callFunctionOn",params,[this,weak,index](Dict result){
      const auto batch=weak.lock();if(!batch||!ReferenceFramesCurrent(batch))return;
      const auto value=result->GetDictionary("result");
      if(!value||value->GetType("value")!=VTYPE_INT||value->GetInt("value")<0||value->GetInt("value")>1){Complete(batch->owner,false,"Native aggregate snapshot child scope unavailable");return;}
      // A loading document can temporarily have no body. Reobserve only this
      // same pinned document under the unchanged original native deadline.
      if(!value->GetInt("value")){
        CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),weak,index]{if(const auto batch=weak.lock())self->ReferenceFramesReadChild(batch,index);}),10);return;}
      ReadReferenceSnapshot(batch->frames[index].work);
    });
  });
}
void ReferenceFramesDescribe(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,const SnapshotReferenceFrameBatch::Edge& edge){
  if(!ReferenceFramesCurrent(batch))return;
  const auto work=batch->frames[edge.parent].work;const auto group=SnapshotReferences().GroupFor(work->ticket);
  if(!group||edge.index>=group->nodes.size()){Complete(batch->owner,false,"Native aggregate snapshot iframe backend unavailable");return;}
  const int backend=group->nodes[edge.index].backend;auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",backend);params->SetInt("depth",0);
  ReferenceRequest(work,"DOM.describeNode",params,[this,batch,edge,backend](Dict result){
    const auto node=result->GetDictionary("node");
    if(!node||node->GetType("backendNodeId")!=VTYPE_INT||node->GetInt("backendNodeId")!=backend||node->GetInt("nodeType")!=1||
       (node->GetString("nodeName")!="IFRAME"&&node->GetString("nodeName")!="FRAME")){Complete(batch->owner,false,"Native aggregate snapshot iframe backend identity changed");return;}
    if(node->GetType("frameId")!=VTYPE_STRING||node->GetString("frameId").empty()){
      const std::weak_ptr<SnapshotReferenceFrameBatch> weak=batch;
      CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),weak,edge]{if(const auto batch=weak.lock())self->ReferenceFramesDescribe(batch,edge);}),10);return;}
    ReferenceFramesCapture(batch,edge.parent,edge.index,node->GetString("frameId").ToString());
  });
}
void ReferenceFramesNext(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch){
  if(!ReferenceFramesCurrent(batch))return;
  if(batch->next==batch->edges.size()){batch->merge=batch->frames.size()-1;ReferenceFramesMerge(batch);return;}
  const auto edge=batch->edges[batch->next++];ReferenceFramesDescribe(batch,edge);
}
void BeginReferenceFrameBatch(const std::shared_ptr<SnapshotReferenceCapture>& work){
  if(!ReferenceCurrent(work))return;
  auto batch=std::make_shared<SnapshotReferenceFrameBatch>();batch->owner=work->owner;batch->generation=work->generation;batch->frames.push_back({work,"",0,""});batch->seen.insert(work->document.frame);
  reference_frame_batches_[work->owner]=batch;ReferenceFramesAttach(batch,work);ReferenceFramesPrepared(batch,0);
}
