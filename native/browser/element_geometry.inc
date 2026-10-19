// SPDX-License-Identifier: MIT
// Include inside Source. Selected and retained nodes share this utility-world
// reader. Inspector border quads are mapped once per actual renderer ancestor;
// no author DOMRect, selector reacquisition or synthetic owner identity is used.
struct ElementBoundsWork {
  int owner=0;uint64_t generation=0;
  std::string frame,node,root;
  Dict config;
  std::map<std::string,SnapshotReferenceDocument> documents;
  std::array<double,8> quad{};
};
std::set<int> element_bounds_operations_;
// Logical work survives deferred native network publication. Physical leases
// remain independently owned by SelectorWaits after logical cancellation.
std::map<int,std::shared_ptr<ElementBoundsWork>> element_bounds_work_;
bool ElementBoundsCurrent(const std::shared_ptr<ElementBoundsWork>& work){
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"boundingBox: Native operation timed out");return false;}
  for(const auto& [frame,document]:work->documents){
    const auto current=ReferenceDocument(frame);
    if(!current||*current!=document){Complete(work->owner,false,"Native element geometry document changed");return false;}
  }
  return true;
}
void ElementBoundsSend(const std::shared_ptr<ElementBoundsWork>& work,const std::string& session,
                       const std::string& method,Dict params,DevToolsCallback callback){
  if(!ElementBoundsCurrent(work))return;
  DevTools(work->owner,method,params,[this,work,callback=std::move(callback)](bool ok,const std::string& json)mutable{
    if(ElementBoundsCurrent(work))callback(ok,json);
  },session);
}
void ElementBoundsPublish(const std::shared_ptr<ElementBoundsWork>& work){
  if(!ElementBoundsCurrent(work))return;
  double left=work->quad[0],right=left,top=work->quad[1],bottom=top;
  for(size_t i=1;i<4;++i){left=std::min(left,work->quad[2*i]);right=std::max(right,work->quad[2*i]);top=std::min(top,work->quad[2*i+1]);bottom=std::max(bottom,work->quad[2*i+1]);}
  auto value=CefDictionaryValue::Create();value->SetDouble("x",left);value->SetDouble("y",top);
  value->SetDouble("width",right-left);value->SetDouble("height",bottom-top);
  if(ElementBoundsCurrent(work))Complete(work->owner,true,"",Json(value));
}
void ElementBoundsViewport(const std::shared_ptr<ElementBoundsWork>& work,const std::string& root,
                           std::function<void(double,double)> done){
  if(!ElementBoundsCurrent(work))return;
  const auto document=work->documents.find(root);
  if(document==work->documents.end()){Complete(work->owner,false,"Native element viewport document is unavailable");return;}
  auto params=CefDictionaryValue::Create();params->SetInt("contextId",document->second.utility);
  params->SetString("expression","({width:innerWidth,height:innerHeight})");params->SetBool("returnByValue",true);
  ElementBoundsSend(work,document->second.session,"Runtime.evaluate",params,[this,work,done=std::move(done)](bool ok,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
    auto remote=result?result->GetDictionary("result"):nullptr;auto viewport=remote?remote->GetDictionary("value"):nullptr;
    const double width=suitecut::DictionaryNumber(viewport,"width"),height=suitecut::DictionaryNumber(viewport,"height");
    if(!ok||!result||result->HasKey("exceptionDetails")||!viewport||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0){Complete(work->owner,false,"Native element viewport geometry is unavailable");return;}
    done(width,height);
  });
}
void ElementBoundsMap(const std::shared_ptr<ElementBoundsWork>& work){
  if(!ElementBoundsCurrent(work))return;
  if(work->root==main_frame_id_){ElementBoundsPublish(work);return;}
  const auto child=frame_states_.find(work->root);
  if(child==frame_states_.end()||child->second.parent.empty()){Complete(work->owner,false,"Native element frame parent is unavailable");return;}
  const auto parent=frame_states_.find(child->second.parent);
  if(parent==frame_states_.end()){Complete(work->owner,false,"Native element frame parent was detached");return;}
  const auto session=parent->second.session;
  const auto root=session.empty()?main_frame_id_:sessions_.at(session);
  ElementBoundsViewport(work,work->root,[this,work,session,root](double width,double height){
    auto params=CefDictionaryValue::Create();params->SetString("frameId",work->root);
    ElementBoundsSend(work,session,"DOM.getFrameOwner",params,[this,work,session,root,width,height](bool ok,const std::string& json){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      if(!ok||!result||result->GetType("backendNodeId")!=VTYPE_INT||result->GetInt("backendNodeId")<=0){Complete(work->owner,false,"Native element frame owner is unavailable");return;}
      auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",result->GetInt("backendNodeId"));
      ElementBoundsSend(work,session,"DOM.getBoxModel",params,[this,work,root,width,height](bool ok,const std::string& json){
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        auto model=result?result->GetDictionary("model"):nullptr;auto content=model?model->GetList("content"):nullptr;
        std::array<double,8> quad{},mapped{};
        if(!ok||!suitecut::ElementBoundsReadQuad(content,quad)||!suitecut::MapElementBoundsQuad(work->quad,quad,width,height,mapped)){Complete(work->owner,false,"Native element frame content transform is unavailable");return;}
        work->quad=mapped;work->root=root;ElementBoundsMap(work);
      });
    });
  });
}
void ElementBoundsLayout(const std::shared_ptr<ElementBoundsWork>& work,bool after_box_failure=false){
  if(!ElementBoundsCurrent(work))return;
  const auto document=work->documents.at(work->frame);
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->node);
  params->SetString("functionDeclaration","function(){if(this.nodeType!==1)return false;if(!this.isConnected)return false;const style=getComputedStyle(this);if(style.visibility==='hidden'||style.visibility==='collapse'||style.display==='none')return false;for(const r of this.getClientRects())if(r.width>0&&r.height>0)return true;return false;}");
  params->SetBool("returnByValue",true);
  ElementBoundsSend(work,document.session,"Runtime.callFunctionOn",params,[this,work,document,after_box_failure](bool ok,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")||!remote||remote->GetType("value")!=VTYPE_BOOL){Complete(work->owner,false,"Native element layout is unavailable");return;}
    if(!remote->GetBool("value")){Complete(work->owner,true,"","null");return;}
    if(after_box_failure){Complete(work->owner,false,"Inspector could not compute the actual element box model");return;}
    auto params=CefDictionaryValue::Create();params->SetString("objectId",work->node);
    ElementBoundsSend(work,document.session,"DOM.getBoxModel",params,[this,work](bool ok,const std::string& json){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      // A layout change within the same document can remove the node's box.
      // Reinspect actual layout only for Chromium's specific no-model reply;
      // arbitrary protocol failures remain errors.
      if(!ok&&result&&result->GetInt("code")==-32000&&result->GetString("message")=="Could not compute box model."){ElementBoundsLayout(work,true);return;}
      auto model=result?result->GetDictionary("model"):nullptr;
      if(!ok||!suitecut::ElementBoundsReadQuad(model?model->GetList("border"):nullptr,work->quad)){Complete(work->owner,false,"Malformed or unavailable native element box model");return;}
      const auto session=work->documents.at(work->frame).session;
      work->root=session.empty()?main_frame_id_:sessions_.at(session);ElementBoundsMap(work);
    });
  });
}
void ElementBoundsSelect(const std::shared_ptr<ElementBoundsWork>& work){
  if(!ElementBoundsCurrent(work))return;
  const auto document=work->documents.at(work->frame);
  operation_frames_[work->owner]=work->frame;operation_sessions_[work->owner]=document.session;
  if(!work->node.empty()){
    evaluation_groups_[work->owner].insert(document.session);
    if(!ReferenceTemporaryGroups(work->owner)){Complete(work->owner,false,"Native element geometry temporary-group capacity exceeded");return;}
    SuiteCutQueryNodeTransfer::AdoptNode(
      [this,work,document](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result done){ElementBoundsSend(work,document.session,method,params,std::move(done));},
      [this,work]{return ElementBoundsCurrent(work);},
      [this,work](const std::string& error){if(ElementBoundsCurrent(work))Complete(work->owner,false,error);},work->node,document.utility,
      "suitecut-automation-"+std::to_string(work->owner),[this,work](Dict node){work->node=node->GetString("objectId").ToString();ElementBoundsLayout(work);});return;
  }
  const auto strict=work->config->GetType("strict")!=VTYPE_BOOL||work->config->GetBool("strict");
  QueryInspect(work->owner,work->config,"if(nodes.length>1&&"+std::string(strict?"true":"false")+")throw new Error("+QueryDiagnosticExpression(work->config,"nodes")+");return nodes[0];",true,false,
    [this,work](bool ok,const std::string& json){
      if(!ElementBoundsCurrent(work))return;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote){auto details=result?result->GetDictionary("exceptionDetails"):nullptr;auto exception=details?details->GetDictionary("exception"):nullptr;Complete(work->owner,false,exception?suitecut::DevToolsString(exception->GetValue("description")):"Native element geometry selection failed");return;}
      if(remote->GetString("type")=="undefined"||remote->GetString("subtype")=="null"){locator_waits_[work->owner]="element to be attached before boundingBox";RetryContext(work->owner);return;}
      if(remote->GetString("subtype")!="node"||remote->GetType("objectId")!=VTYPE_STRING){Complete(work->owner,false,"Native element geometry selection is not a DOM node");return;}
      if(!ReferenceTemporaryGroups(work->owner)){Complete(work->owner,false,"Native element geometry temporary-group capacity exceeded");return;}
      work->node=remote->GetString("objectId").ToString();ElementBoundsLayout(work);
    });
}
void ElementBoundsPrepare(const std::shared_ptr<ElementBoundsWork>& work,const std::string& frame){
  if(!ElementBoundsCurrent(work))return;
  if(work->documents.size()>=64){Complete(work->owner,false,"Native element geometry frame-depth limit exceeded");return;}
  WithFrame(work->owner,frame,[this,work,frame](int){
    if(!ElementBoundsCurrent(work))return;
    const auto document=ReferenceDocument(frame);const auto state=frame_states_.find(frame);
    if(!document||state==frame_states_.end()){Complete(work->owner,false,"Native element geometry document is unavailable");return;}
    if(!work->documents.emplace(frame,*document).second){Complete(work->owner,false,"Native element geometry frame cycle");return;}
    if(state->second.parent.empty()){ElementBoundsSelect(work);return;}
    ElementBoundsPrepare(work,state->second.parent);
  },true);
}
void ElementBoundsStart(int id,uint64_t generation,Dict config,const std::string& node=""){
  if(!operations_.IsCurrent(id,generation))return;
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"boundingBox: Native operation timed out");return;}
  element_bounds_operations_.insert(id);
  if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native element geometry physical capacity exceeded");return;}
  auto work=std::make_shared<ElementBoundsWork>();work->owner=id;work->generation=generation;
  work->frame=FrameId(config);work->node=node;work->config=config;
  element_bounds_work_[id]=work;
  ElementBoundsPrepare(work,work->frame);
}
void ElementBoundingBox(int id,const std::string& object,const std::string& frame){
  const auto entry=operations_.Get(id);if(!entry)return;
  context_retries_.erase(id);
  auto config=CefDictionaryValue::Create();config->SetString("frame",frame);
  ElementBoundsStart(id,entry->generation,config,object);
}
void ElementBoundsAction(int id,uint64_t generation,Dict config){
  if(!operations_.IsCurrent(id,generation))return;
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"boundingBox: Native operation timed out");return;}
  element_bounds_operations_.insert(id);
  if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native element geometry physical capacity exceeded");return;}
  if(!PrepareSnapshotReferenceScope(id,config))return;
  const auto path=config->GetList("framePath");
  if(path&&path->GetSize()){ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation](Dict resolved){ElementBoundsAction(id,generation,resolved);});return;}
  if(config->HasKey("nativeHandle")){
    const auto handle=handles_.find(config->GetInt("nativeHandle"));
    if(handle==handles_.end()){Complete(id,false,MissingHandle(config->GetInt("nativeHandle")));return;}
    if(handle->second.node_id.empty()){Complete(id,false,"Native handle is not a DOM node");return;}
    context_retries_.erase(id);config=config->Copy(false);config->SetString("frame",handle->second.frame_id);
    ElementBoundsStart(id,generation,config,handle->second.node_id);return;
  }
  ElementBoundsStart(id,generation,config);
}
