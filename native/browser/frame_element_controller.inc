// SPDX-License-Identifier: MIT
// DOM.getFrameOwner reads Chromium's real frame-to-backend-node relationship.
// Preparation shares bounded selected-node command/group ownership: cancellation
// stops delivery while actual DevTools acknowledgments retire physical work.
struct FrameElementWork {
  int owner=0,context=0;
  uint64_t generation=0;
  std::string child,parent,session,loader;
};
bool FrameElementCurrent(const std::shared_ptr<FrameElementWork>& work) {
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  const auto child=frame_states_.find(work->child),parent=frame_states_.find(work->parent);
  if(child==frame_states_.end()||child->second.parent!=work->parent||parent==frame_states_.end()||
     parent->second.context!=work->context||parent->second.session!=work->session||parent->second.loader!=work->loader){
    Complete(work->owner,false,"Native frame owner context destroyed by navigation or detachment");return false;
  }
  return true;
}
void FrameElementHandle(int id,Dict message) {
  const auto operation=operations_.Get(id);if(!operation)return;
  const auto child=FrameId(message);const auto found=frame_states_.find(child);
  if(found==frame_states_.end()){Complete(id,false,"Native frame detached or unavailable");return;}
  const auto parent=found->second.parent;
  if(parent.empty()){Complete(id,false,"Frame has been detached, or was the main frame");return;}
  if(!SelectorWaits().Admit(id,operation->generation)){Complete(id,false,"Native frame owner preparation capacity exceeded");return;}
  WithFrame(id,parent,[this,id,generation=operation->generation,child,parent](int context){
    if(!operations_.IsCurrent(id,generation))return;
    const auto current=frame_states_.find(parent);if(current==frame_states_.end()){Complete(id,false,"Native frame owner context unavailable");return;}
    auto work=std::make_shared<FrameElementWork>();work->owner=id;work->generation=generation;work->child=child;
    work->parent=parent;work->context=context;work->session=current->second.session;work->loader=current->second.loader;
    if(!FrameElementCurrent(work))return;
    auto request=CefDictionaryValue::Create();request->SetString("frameId",child);
    DevTools(id,"DOM.getFrameOwner",request,[this,work](bool ok,const std::string& json){
      if(!FrameElementCurrent(work))return;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      if(!ok||!result||result->GetType("backendNodeId")!=VTYPE_INT||result->GetInt("backendNodeId")<=0){Complete(work->owner,false,"Native frame owner backend node unavailable");return;}
      const auto group="suitecut-automation-"+std::to_string(work->owner);
      auto resolve=CefDictionaryValue::Create();resolve->SetInt("backendNodeId",result->GetInt("backendNodeId"));
      resolve->SetInt("executionContextId",work->context);resolve->SetString("objectGroup",group);
      evaluation_groups_[work->owner].insert(work->session);
      DevTools(work->owner,"DOM.resolveNode",resolve,[this,work](bool ok,const std::string& json){
        if(!FrameElementCurrent(work))return;
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        auto node=result?result->GetDictionary("object"):nullptr;
        if(!ok||!node||node->GetString("subtype")!="node"||node->GetType("objectId")!=VTYPE_STRING||node->GetString("objectId").empty()){
          Complete(work->owner,false,"Native frame owner DOM node unavailable");return;
        }
        // Pass only an internally resolved object ID into the ordinary genuine
        // retained-handle constructor; public inputs never select that receiver.
        auto config=CefDictionaryValue::Create();config->SetString("action","create");config->SetString("frame",work->parent);
        config->SetString("nativeQueryNodes",node->GetString("objectId"));config->SetString("nativeFrameOwnerChild",work->child);config->SetString("function","function(){return {value:this};}");
        config->SetList("handles",CefListValue::Create());Handle(work->owner,config,true);
      },work->session);
    },work->session);
  });
}
