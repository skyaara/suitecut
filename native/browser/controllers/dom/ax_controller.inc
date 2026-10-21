// SPDX-License-Identifier: MIT
// Source-owned private Chromium AX capture. The ordinary operation registry owns
// admission/deadlines; every continuation additionally pins its exact frame epoch.
struct AXCapture {
  int owner=0,main=0,utility=0,backend=0;
  uint64_t generation=0,token=0;
  std::string frame,session,loader,array;
  size_t requests=0,captured_bytes=0,charged_bytes=0,link=0,layout=0;
  bool boxes=false;
  size_t layout_bytes=0;
  std::vector<CefRefPtr<CefDictionaryValue>> layout_nodes;
  std::function<void(size_t)> release;
  std::function<bool()> extra_current;
  ~AXCapture(){if(charged_bytes&&release)release(charged_bytes);}
  CefRefPtr<CefListValue> nodes,forest;
  std::vector<SuiteCutAXForest::Link> links;
  std::function<void(const std::string&,CefRefPtr<CefListValue>)> done;
};
struct AXPhysicalLatch {bool settled=false;std::function<void()> release;void Settle(){if(!settled){settled=true;release();}}~AXPhysicalLatch(){Settle();}};
std::map<int,uint64_t> ax_captures_;
uint64_t ax_capture_sequence_=0;
size_t ax_physical_=0,ax_capture_bytes_=0;
bool AXCurrent(const std::shared_ptr<AXCapture>& work){
  const auto found=ax_captures_.find(work->owner);
  if(!operations_.IsCurrent(work->owner,work->generation)||found==ax_captures_.end()||found->second!=work->token)return false;
  if(work->extra_current&&!work->extra_current())return false;
  const auto frame=frame_states_.find(work->frame);
  if(frame==frame_states_.end()||frame->second.context!=work->main||frame->second.utility_context!=work->utility||frame->second.session!=work->session||frame->second.loader!=work->loader){RetryContext(work->owner);return false;}
  return true;
}
void AXError(const std::shared_ptr<AXCapture>& work,const std::string& error){if(AXCurrent(work))Complete(work->owner,false,error);}
void AXRequest(const std::shared_ptr<AXCapture>& work,const std::string& method,Dict params,std::function<void(Dict)> done){
  if(!AXCurrent(work))return;
  if(++work->requests>20000){AXError(work,"Native accessibility capture work limit exceeded");return;}
  if(ax_physical_>=64){AXError(work,"Native accessibility physical command capacity exceeded");return;}
  ++ax_physical_;
  auto physical=std::make_shared<AXPhysicalLatch>();physical->release=[this]{--ax_physical_;};
  const size_t limit=method=="Accessibility.getFullAXTree"?16*1048576:1048576;
  DevTools(work->owner,method,params,[this,work,method,physical,done=std::move(done)](bool ok,const std::string& json)mutable{
    physical->Settle(); // The real native command has now physically settled.
    if(!AXCurrent(work))return;
    const size_t limit=method=="Accessibility.getFullAXTree"?16*1048576:1048576;
    if(json.size()>limit){AXError(work,"Native accessibility capture response byte limit exceeded");return;}
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
    if(!ok||!result||result->HasKey("exceptionDetails")){
      auto details=result?result->GetDictionary("exceptionDetails"):nullptr;auto exception=details?details->GetDictionary("exception"):nullptr;
      const auto cause=exception?exception->GetString("description").ToString():result?result->GetString("message").ToString():"";
      if((method=="DOM.describeNode"&&cause.find("Could not find node")!=std::string::npos)||cause.find("session was detached")!=std::string::npos){RetryContext(work->owner);return;}
      AXError(work,cause.empty()?"Native accessibility capture command failed":cause.substr(0,4096));return;
    }
    if(method=="Accessibility.getFullAXTree"||method=="Accessibility.getPartialAXTree"){
      work->captured_bytes+=json.size();if(work->captured_bytes>16*1048576||json.size()>16*1048576-ax_capture_bytes_){AXError(work,"Native accessibility aggregate capture byte limit exceeded");return;}
      ax_capture_bytes_+=json.size();work->charged_bytes+=json.size();
    }
    done(result);
  },work->session,true,limit,true);
}
void AXFinish(const std::shared_ptr<AXCapture>& work){
  if(!AXCurrent(work))return;
  auto value=CefValue::Create();value->SetList(work->forest->Copy());
  std::string serialized;
  if(!suitecut::DevToolsLosslessJSON(value,serialized,SuiteCutAXForest::kOutputBytes)){AXError(work,"Native accessibility snapshot exceeds 64 KiB");return;}
  ax_captures_.erase(work->owner);work->done(work->array,work->forest);
}
void AXReadLink(const std::shared_ptr<AXCapture>& work){
  if(!AXCurrent(work))return;
  if(work->link==work->links.size()){AXFinish(work);return;}
  const auto link=work->links[work->link++];auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",link.backend);
  AXRequest(work,"DOM.describeNode",params,[this,work,link](Dict result){
    auto node=result->GetDictionary("node");auto attributes=node?node->GetList("attributes"):nullptr;
    if(!node){AXError(work,"Native accessibility link backend node unavailable");return;}
    if(attributes){if(attributes->GetSize()>4096||attributes->GetSize()%2){AXError(work,"Native accessibility link attribute limit exceeded");return;}
      for(size_t i=0;i<attributes->GetSize();i+=2){
        if(attributes->GetType(i)!=VTYPE_STRING||(attributes->GetType(i+1)!=VTYPE_STRING&&attributes->GetType(i+1)!=VTYPE_DICTIONARY)){AXError(work,"Invalid native accessibility link attributes");return;}
        if(attributes->GetString(i)=="href"){
          const auto href=suitecut::DevToolsString(attributes->GetValue(i+1));if(href.size()>SuiteCutAXForest::kOutputBytes){AXError(work,"Native accessibility link URL byte limit exceeded");return;}
          SuiteCutAXForest::SetRawURL(link.node,href);break;
        }
      }
    }
    AXReadLink(work);
  });
}
void AXProject(const std::shared_ptr<AXCapture>& work){
  if(!AXCurrent(work))return;
  SuiteCutAXForest projector;std::string error;
  if(!projector.Project(work->nodes,work->backend,work->forest,work->links,error)){AXError(work,error);return;}
  work->nodes=nullptr;ax_capture_bytes_-=work->charged_bytes;work->charged_bytes=0;AXReadLink(work);
}
void AXReadLayout(const std::shared_ptr<AXCapture>& work){
  if(!AXCurrent(work))return;
  if(work->layout==work->layout_nodes.size()){AXProject(work);return;}
  const auto node=work->layout_nodes[work->layout++];auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",node->GetInt("backendDOMNodeId"));params->SetInt("executionContextId",work->utility);params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
  AXRequest(work,"DOM.resolveNode",params,[this,work,node](Dict result){
    auto object=result->GetDictionary("object");if(!object||object->GetType("objectId")!=VTYPE_STRING){AXError(work,"Native accessibility layout backend node unavailable");return;}
    const auto id=object->GetString("objectId").ToString();auto params=CefDictionaryValue::Create();params->SetString("objectId",id);params->SetBool("returnByValue",true);
    params->SetString("functionDeclaration",R"JS(function(boxes){
      const start=this.nodeType===1?this:(this.assignedSlot||this.parentElement);let element=start,hidden=false,steps=0;
      while(element){if(++steps>64)throw new Error('Native accessibility layout ancestor depth limit exceeded');const style=getComputedStyle(element);if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse')hidden=true;const parent=element.assignedSlot||element.parentElement;const root=parent?null:element.getRootNode();element=parent||(root&&root.host)||null;}
      const own=this.nodeType===1?getComputedStyle(this):null;const display=own?own.display:'';
      const result={hidden,block:!!display&&display!=='contents'&&!display.startsWith('inline')&&display!=='none'};
      if(boxes){let rect;if(this.nodeType===3){const range=this.ownerDocument.createRange();range.selectNodeContents(this);rect=range.getBoundingClientRect();range.detach();}else if(this.nodeType===1)rect=this.getBoundingClientRect();if(rect)result.box=[rect.x,rect.y,rect.width,rect.height];}
      if(!hidden&&this.nodeType===1){
        const invalid=this.getAttribute('aria-invalid');if(invalid==='grammar'||invalid==='spelling')result.invalidToken=invalid;
        if(this instanceof HTMLInputElement||this instanceof HTMLTextAreaElement){const placeholder=this.getAttribute('placeholder');if(placeholder!==null&&placeholder!=='')result.placeholder=placeholder;}
      }
      return result;
    })JS");auto args=CefListValue::Create();auto argument=CefDictionaryValue::Create();argument->SetBool("value",work->boxes);args->SetDictionary(0,argument);params->SetList("arguments",args);
    AXRequest(work,"Runtime.callFunctionOn",params,[this,work,node,id](Dict result){
      auto release=CefDictionaryValue::Create();release->SetString("objectId",id);SendDevTools("Runtime.releaseObject",release,work->session);
      auto remote=result->GetDictionary("result"),layout=remote?remote->GetDictionary("value"):nullptr;
      if(!layout||layout->GetSize()<2||layout->GetSize()>5||layout->GetType("hidden")!=VTYPE_BOOL||layout->GetType("block")!=VTYPE_BOOL){AXError(work,"Invalid native accessibility layout metadata");return;}
      auto box=layout->GetList("box");if(box){if(!work->boxes||box->GetSize()!=4){AXError(work,"Invalid native accessibility geometry");return;}for(size_t i=0;i<4;++i){const auto type=box->GetType(i);const double value=type==VTYPE_INT?box->GetInt(i):type==VTYPE_DOUBLE?box->GetDouble(i):std::numeric_limits<double>::quiet_NaN();if(!std::isfinite(value)||(i>=2&&value<0)){AXError(work,"Invalid native accessibility geometry");return;}}}
      if((layout->HasKey("placeholder")&&layout->GetType("placeholder")!=VTYPE_STRING)||(layout->HasKey("invalidToken")&&(layout->GetType("invalidToken")!=VTYPE_STRING||(layout->GetString("invalidToken")!="grammar"&&layout->GetString("invalidToken")!="spelling")))){AXError(work,"Invalid native accessibility DOM property metadata");return;}
      auto serialized=CefValue::Create();serialized->SetDictionary(layout->Copy(false));const auto bytes=CefWriteJSON(serialized,JSON_WRITER_DEFAULT).length();
      if(bytes>SuiteCutAXForest::kOutputBytes||bytes>2*1048576-work->layout_bytes||bytes>16*1048576-ax_capture_bytes_){AXError(work,"Native accessibility layout metadata byte limit exceeded");return;}
      work->layout_bytes+=bytes;work->charged_bytes+=bytes;ax_capture_bytes_+=bytes;
      node->SetDictionary("__nativeLayout",layout->Copy(false));AXReadLayout(work);
    });
  });
}
void AXPrepareLayout(const std::shared_ptr<AXCapture>& work){
  if(!AXCurrent(work))return;
  // Validate native output bounds before admitting metadata commands. The
  // semantic roles/names/states remain exclusively actual Chromium AX data.
  SuiteCutAXForest bounded;CefRefPtr<CefListValue> forest;std::vector<SuiteCutAXForest::Link> links;std::string error;
  if(!bounded.Project(work->nodes,work->backend,forest,links,error)){AXError(work,error);return;}
  std::map<std::string,Dict> graph;std::string root;
  for(size_t i=0;i<work->nodes->GetSize();++i){auto node=work->nodes->GetDictionary(i);const auto id=node->GetString("nodeId").ToString();graph[id]=node;if(root.empty()&&node->GetInt("backendDOMNodeId")==work->backend)root=id;}
  std::vector<std::string> pending{root};std::set<std::string> seen;size_t visited=0;
  while(!pending.empty()){const auto id=pending.back();pending.pop_back();if(!seen.insert(id).second)continue;if(++visited>SuiteCutAXForest::kCaptureNodes){AXError(work,"Native accessibility metadata graph node limit exceeded");return;}const auto found=graph.find(id);if(found==graph.end())continue;auto node=found->second;auto role=node->GetDictionary("role");const auto name=role?role->GetString("value").ToString():"";if(name=="InlineTextBox"||name=="ListMarker")continue;
    if(node->GetInt("backendDOMNodeId")>0){if(work->layout_nodes.size()>=8192){AXError(work,"Native accessibility layout node limit exceeded");return;}work->layout_nodes.push_back(node);}
    auto children=node->GetList("childIds");if(children)for(size_t i=0;i<children->GetSize();++i)pending.push_back(children->GetString(i).ToString());
  }
  AXReadLayout(work);
}
void AXReadTree(const std::shared_ptr<AXCapture>& work){
  auto params=CefDictionaryValue::Create();params->SetString("frameId",work->frame);
  AXRequest(work,"Accessibility.getFullAXTree",params,[this,work](Dict result){
    auto nodes=result->GetList("nodes");if(!nodes||nodes->GetSize()>SuiteCutAXForest::kCaptureNodes){AXError(work,"Native accessibility capture node limit exceeded");return;}
    // A CEF child-list view cannot outlive the response dictionary. Keep actual
    // copied AX data across the optional scope lookup and raw-link requests.
    work->nodes=nodes->Copy();
    if(SuiteCutAXForest::HasRoot(work->nodes,work->backend)){AXPrepareLayout(work);return;}
    // Chromium can omit an ignored selected DOM container from the full graph.
    // Obtain that exact real AX scope; this is never a DOM semantic fallback.
    auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",work->backend);params->SetBool("fetchRelatives",false);
    AXRequest(work,"Accessibility.getPartialAXTree",params,[this,work](Dict result){
      auto extra=result->GetList("nodes");if(!extra||extra->GetSize()>4096){AXError(work,"Native accessibility scope capture node limit exceeded");return;}
      std::map<std::string,size_t> indices;
      for(size_t i=0;i<work->nodes->GetSize();++i){auto node=work->nodes->GetDictionary(i);if(node)indices[node->GetString("nodeId").ToString()]=i;}
      for(size_t i=0;i<extra->GetSize();++i){auto node=extra->GetDictionary(i);if(!node)continue;const auto found=indices.find(node->GetString("nodeId").ToString());
        if(found!=indices.end())work->nodes->SetDictionary(found->second,node->Copy(false));else work->nodes->SetDictionary(work->nodes->GetSize(),node->Copy(false));
        if(work->nodes->GetSize()>SuiteCutAXForest::kCaptureNodes){AXError(work,"Native accessibility capture node limit exceeded");return;}
      }
      AXPrepareLayout(work);
    });
  });
}
void CaptureAXForest(int id,Dict message,std::function<void(const std::string&,CefRefPtr<CefListValue>)> done){
  const auto operation=operations_.Get(id);if(!operation)return;
  if(ax_captures_.size()>=64&&!ax_captures_.contains(id)){Complete(id,false,"Native accessibility capture capacity exceeded");return;}
  if(ax_capture_sequence_==std::numeric_limits<uint64_t>::max()){Complete(id,false,"Native accessibility capture sequence exhausted");return;}
  auto work=std::make_shared<AXCapture>();work->owner=id;work->boxes=message->GetBool("accessibilityBoxes");work->generation=operation->generation;work->token=++ax_capture_sequence_;work->frame=FrameId(message);work->done=std::move(done);work->release=[this](size_t bytes){ax_capture_bytes_-=bytes;};
  ax_captures_[id]=work->token;
  QueryNodesInWorld(id,message,true,[this,work](const std::string& array){
    const auto found=ax_captures_.find(work->owner);if(!operations_.IsCurrent(work->owner,work->generation)||found==ax_captures_.end()||found->second!=work->token)return;
    const auto frame=frame_states_.find(work->frame);if(frame==frame_states_.end()||!frame->second.context||!frame->second.utility_context){RetryContext(work->owner);return;}
    work->main=frame->second.context;work->utility=frame->second.utility_context;work->session=frame->second.session;work->loader=frame->second.loader;work->array=array;
    auto params=CefDictionaryValue::Create();params->SetString("objectId",array);params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
    params->SetString("functionDeclaration","function(){if(this.length>1)throw new Error('strict mode violation: accessibility snapshot locator resolved to '+this.length+' elements');return [...this];}");
    AXRequest(work,"Runtime.callFunctionOn",params,[this,work](Dict result){
      auto array=result->GetDictionary("result");
      if(!array||array->GetString("subtype")!="array"||array->GetType("objectId")!=VTYPE_STRING){AXError(work,"Native accessibility scope array pin failed");return;}
      // The planner's original lease can now expire. This distinct actual
      // array retains the same genuine DOM nodes in the operation group.
      work->array=array->GetString("objectId").ToString();
      auto params=CefDictionaryValue::Create();params->SetString("objectId",work->array);params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
      params->SetString("functionDeclaration","function(){return this[0]||null;}");
      AXRequest(work,"Runtime.callFunctionOn",params,[this,work](Dict result){
      auto node=result->GetDictionary("result");if(node&&node->GetString("subtype")=="null"){work->forest=CefListValue::Create();AXFinish(work);return;}
      if(!node||node->GetType("objectId")!=VTYPE_STRING){AXError(work,"Native accessibility snapshot DOM scope unavailable");return;}
      const auto node_object=node->GetString("objectId").ToString();auto params=CefDictionaryValue::Create();params->SetString("objectId",node_object);
      AXRequest(work,"DOM.describeNode",params,[this,work,node_object](Dict result){auto release=CefDictionaryValue::Create();release->SetString("objectId",node_object);SendDevTools("Runtime.releaseObject",release,work->session);auto node=result->GetDictionary("node");if(!node||node->GetType("backendNodeId")!=VTYPE_INT||node->GetInt("backendNodeId")<=0){AXError(work,"Native accessibility snapshot backend scope unavailable");return;}work->backend=node->GetInt("backendNodeId");AXRequest(work,"Accessibility.enable",CefDictionaryValue::Create(),[this,work](Dict){AXReadTree(work);});});
      });
    });
  });
}
