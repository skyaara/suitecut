// SPDX-License-Identifier: MIT
// Native hit testing only. Coordinates are viewport CSS pixels in the requested
// frame. A different returned frame is rejected, never silently adopted.
void LocatorPickAt(const std::shared_ptr<LocatorSuggestionWork>&work,int x,int y,const std::string&root){
  if(!LocatorSuggestionCurrent(work))return;
  auto params=CefDictionaryValue::Create();params->SetInt("x",x);params->SetInt("y",y);
  params->SetBool("includeUserAgentShadowDOM",false);params->SetBool("ignorePointerEventsNone",false);
  work->pick->SetInt("inspectorX",x);work->pick->SetInt("inspectorY",y);work->pick->SetString("inspectorRoot",root);
  LocatorSuggestionSend(work,"DOM.getNodeForLocation",params,[this,work](bool ok,const std::string&json){
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
    if(!ok||!result||result->GetType("frameId")!=VTYPE_STRING||result->GetType("backendNodeId")!=VTYPE_INT||result->GetInt("backendNodeId")<=0){Complete(work->owner,false,"Native point picking lacks actual Inspector frame/backend identity");return;}
    if(result->GetString("frameId")!=work->document.frame){Complete(work->owner,false,"Native point picking selected a different frame; select that actual frame explicitly");return;}
    work->picked_backend=result->GetInt("backendNodeId");
    auto resolve=CefDictionaryValue::Create();resolve->SetInt("backendNodeId",work->picked_backend);resolve->SetInt("executionContextId",work->document.utility);resolve->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
    LocatorSuggestionSend(work,"DOM.resolveNode",resolve,[this,work](bool ok,const std::string&json){
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;const auto node=result?result->GetDictionary("object"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!node||node->GetString("subtype")!="node"||node->GetType("objectId")!=VTYPE_STRING||node->GetString("objectId").empty()||node->GetString("objectId").length()>4096){Complete(work->owner,false,"Native picked backend node cannot be adopted in the captured utility realm");return;}
      work->node=node->GetString("objectId").ToString();LocatorSuggestionObserve(work);
    });
  });
}
void LocatorPickCoordinates(const std::shared_ptr<LocatorSuggestionWork>&work,int x,int y){
  const auto&document=work->document;std::string root;
  if(document.session.empty())root=main_frame_id_;
  else {const auto active=sessions_.find(document.session);if(active==sessions_.end()){Complete(work->owner,false,"Native picking Inspector-root mapping is unavailable");return;}root=active->second;}
  work->inspector_root=root;
  // Each transform depends on captured real frame documents. A same-text frame
  // replacement or ancestor navigation invalidates the outstanding hit test.
  std::map<std::string,std::string>parent_edges;
  std::string cursor=document.frame;
  while(true){
    if(work->ancestors.size()>=64||work->ancestors.contains(cursor)){Complete(work->owner,false,"Native picking frame ancestry is cyclic or exceeds its bound");return;}
    const auto frame=frame_states_.find(cursor);const auto pin=ReferenceDocument(cursor);
    if(frame==frame_states_.end()||!pin||pin->session!=document.session){Complete(work->owner,false,"Native picking frame transform identity is unavailable");return;}
    work->ancestors.emplace(cursor,*pin);parent_edges.emplace(cursor,frame->second.parent);
    if(cursor==root)break;
    if(frame->second.parent.empty()){Complete(work->owner,false,"Native picking frame is outside its actual Inspector root");return;}
    cursor=frame->second.parent;
  }
  work->picking_current=[this,root,session=document.session,parent_edges=std::move(parent_edges)]{
    if(session.empty()){if(root!=main_frame_id_)return false;}
    else{const auto at=sessions_.find(session);if(at==sessions_.end()||at->second!=root)return false;}
    for(const auto&[frame,parent]:parent_edges){const auto at=frame_states_.find(frame);if(at==frame_states_.end()||at->second.parent!=parent||at->second.session!=session)return false;}
    return true;
  };
  if(document.frame==root){LocatorPickAt(work,x,y,root);return;}
  auto viewport=CefDictionaryValue::Create();viewport->SetInt("contextId",document.utility);viewport->SetString("expression","({width:innerWidth,height:innerHeight})");viewport->SetBool("returnByValue",true);
  LocatorSuggestionSend(work,"Runtime.evaluate",viewport,[this,work,x,y,root](bool ok,const std::string&json){
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;const auto remote=result?result->GetDictionary("result"):nullptr;const auto size=remote?remote->GetDictionary("value"):nullptr;
    const double width=suitecut::DictionaryNumber(size,"width"),height=suitecut::DictionaryNumber(size,"height");
    if(!ok||!result||result->HasKey("exceptionDetails")||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0){Complete(work->owner,false,"Native picking frame viewport is unavailable");return;}
    auto owner=CefDictionaryValue::Create();owner->SetString("frameId",work->document.frame);
    LocatorSuggestionSend(work,"DOM.getFrameOwner",owner,[this,work,x,y,root,width,height](bool ok,const std::string&json){
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
      if(!ok||!result||result->GetType("backendNodeId")!=VTYPE_INT||result->GetInt("backendNodeId")<=0){Complete(work->owner,false,"Native picking frame owner is unavailable");return;}
      auto box=CefDictionaryValue::Create();box->SetInt("backendNodeId",result->GetInt("backendNodeId"));
      LocatorSuggestionSend(work,"DOM.getBoxModel",box,[this,work,x,y,root,width,height](bool ok,const std::string&json){
        const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;const auto model=result?result->GetDictionary("model"):nullptr;
        std::array<double,8>content{},mapped{},point{double(x),double(y),double(x),double(y),double(x),double(y),double(x),double(y)};
        if(!ok||!suitecut::ElementBoundsReadQuad(model?model->GetList("content"):nullptr,content)||!suitecut::MapElementBoundsQuad(point,content,width,height,mapped)||mapped[0]<0||mapped[1]<0||mapped[0]>1000000||mapped[1]>1000000){Complete(work->owner,false,"Native picking frame content transform is unavailable");return;}
        // CDP's hit test accepts integer pixels. Report the actual native rounded
        // Inspector coordinates, separately from requested local coordinates.
        LocatorPickAt(work,static_cast<int>(std::llround(mapped[0])),static_cast<int>(std::llround(mapped[1])),root);
      });
    });
  });
}
void BeginLocatorPick(int owner,Dict message){
  const auto operation=operations_.Get(owner);if(!operation)return;
  if(message->GetType("x")!=VTYPE_INT||message->GetType("y")!=VTYPE_INT||message->GetInt("x")<0||message->GetInt("y")<0||message->GetInt("x")>1000000||message->GetInt("y")>1000000){Complete(owner,false,"Native point picking requires bounded integer viewport coordinates");return;}
  for(const char*key:{"nativeHandle","nativeRootHandle","nativeQueryNodes","framePath","query","selector","point"})if(message->HasKey(key)){Complete(owner,false,"Native point picking cannot borrow another selection or handle");return;}
  if(operations_.IsExpired(owner,operation->generation,Now())){Complete(owner,false,"Native point picking original deadline expired");return;}
  if(!SelectorWaits().Admit(owner,operation->generation)){Complete(owner,false,"Native point picking physical capacity exceeded");return;}
  const auto frame=FrameId(message);const auto generation=operation->generation;const int x=message->GetInt("x"),y=message->GetInt("y");
  WithFrame(owner,frame,[this,owner,generation,frame,x,y](int){
    if(!operations_.IsCurrent(owner,generation))return;
    const auto document=ReferenceDocument(frame);if(!document){Complete(owner,false,"Native point picking document identity unavailable");return;}
    auto work=std::make_shared<LocatorSuggestionWork>();work->owner=owner;work->generation=generation;work->document=*document;
    work->pick=CefDictionaryValue::Create();work->pick->SetInt("x",x);work->pick->SetInt("y",y);locator_suggestion_work_[owner]=work;
    if(!LocatorSuggestionCurrent(work))return;
    if(!ReferenceTemporaryGroups(owner)){Complete(owner,false,"Native point picking group ownership unavailable");return;}
    LocatorPickCoordinates(work,x,y);
  },true);
}
