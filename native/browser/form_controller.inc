// SPDX-License-Identifier: MIT
// Include inside Source. See the integration notes sent to the parent agent.
// Expired identities preserve a controlled timeout after native staging cleanup.
std::map<std::string,double> expired_upload_tokens_;
void FormStart(int id,uint64_t generation,Dict message) {
  const auto action=message->GetString("action").ToString();
  if(action!="uploadAbort"&&message->HasKey("uploadToken")&&message->GetType("uploadToken")==VTYPE_STRING&&expired_upload_tokens_.contains(message->GetString("uploadToken"))){Complete(id,false,"Timeout exceeded: native upload exceeded its original action or idle deadline");return;}
  if(action=="uploadBegin"||action=="uploadAppend"||action=="uploadAbort") { FormStageCommand(id,message);return; }
  std::string selector;
  if(!suitecut::InboundString(message,"selector",selector)||selector.empty()||
      (action!="setChecked"&&action!="selectOption"&&action!="setInputFiles")){
    Complete(id,false,"Invalid native form action");return;
  }
  if(message->HasKey("force")&&message->GetType("force")!=VTYPE_BOOL){Complete(id,false,"Invalid native form force option");return;}
  if(action=="setChecked"&&message->GetType("checked")!=VTYPE_BOOL){Complete(id,false,"Native checked state must be a boolean");return;}
  if(action=="selectOption"){
    const auto choices=message->GetList("options");
    if(!choices||choices->GetSize()>10000){Complete(id,false,"Invalid native select options");return;}
    for(size_t i=0;i<choices->GetSize();++i){
      const auto choice=choices->GetDictionary(i);
      if(!choice||(!choice->HasKey("value")&&!choice->HasKey("label")&&!choice->HasKey("index")&&!choice->HasKey("handle")&&!choice->HasKey("valueOrLabel"))||
          (choice->HasKey("value")&&choice->GetType("value")!=VTYPE_STRING)||
          (choice->HasKey("valueOrLabel")&&choice->GetType("valueOrLabel")!=VTYPE_STRING)||
          (choice->HasKey("label")&&choice->GetType("label")!=VTYPE_STRING)||
          (choice->HasKey("index")&&(choice->GetType("index")!=VTYPE_INT||choice->GetInt("index")<0))||
          (choice->HasKey("handle")&&(choice->GetType("handle")!=VTYPE_INT||!handles_.contains(choice->GetInt("handle"))||choice->GetSize()!=1))){Complete(id,false,"Invalid native select option descriptor");return;}
      CefDictionaryValue::KeyList keys;choice->GetKeys(keys);
      for(const auto& key:keys)if(key!="value"&&key!="label"&&key!="index"&&key!="handle"&&key!="valueOrLabel"){Complete(id,false,"Unknown native select option descriptor field");return;}
    }
  }
  if(action=="setInputFiles"){
    if(message->HasKey("uploadToken")){
      if(message->GetType("uploadToken")!=VTYPE_STRING||!pending_uploads_.contains(message->GetString("uploadToken"))){Complete(id,false,"Native upload token is missing or expired");return;}
      auto& upload=pending_uploads_.at(message->GetString("uploadToken"));upload.active_owner=id;upload.active_generation=generation;
    }else{
    const auto files=message->GetList("files");
    if(!files||files->GetSize()>1000){Complete(id,false,"Invalid native upload files");return;}
    for(size_t i=0;i<files->GetSize();++i){
      const auto file=files->GetDictionary(i);
      if(!file||file->GetType("path")!=VTYPE_STRING){
        Complete(id,false,"Invalid native upload file descriptor");return;
      }
    }
    }
  }
  auto config=message->Copy(false);
  if(config->HasKey("nativeHandle")){
    if(config->GetType("nativeHandle")!=VTYPE_INT){Complete(id,false,"Invalid native element handle");return;}
    const auto handle=handles_.find(config->GetInt("nativeHandle"));
    if(handle==handles_.end()){Complete(id,false,MissingHandle(config->GetInt("nativeHandle")));return;}
    if(handle->second.node_id.empty()){Complete(id,false,"Native handle is not a DOM node");return;}
    config->SetString("frame",handle->second.frame_id);
  }
  locator_logs_[id]=action+" action\n - resolving "+suitecut::InboundDisplay(suitecut::InboundString(config,"selector")).substr(0,1024);
  context_retries_[id]=[this,id,generation,config]{FormAction(id,generation,config);};
  FormAction(id,generation,config);
}
void FormNodeEvaluate(int id, Dict config, const std::string& expression, DevToolsCallback callback) {
  auto params=CefDictionaryValue::Create();params->SetString("objectId",config->GetString("formNode"));
  params->SetString("functionDeclaration","async function(){return await("+expression+");}");
  params->SetBool("awaitPromise",true);params->SetBool("returnByValue",true);
  params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
  const auto frame=frame_states_.find(FrameId(config));
  if(frame==frame_states_.end()){Complete(id,false,"Native frame detached");return;}
  operation_frames_[id]=frame->first;operation_sessions_[id]=frame->second.session;
  DevTools(id,"Runtime.callFunctionOn",params,std::move(callback));
}

struct FormSelectWork {
  int owner=0,context=0;
  uint64_t generation=0;
  Dict config,params;
  CefRefPtr<CefListValue> arguments;
  std::vector<int> handles;
  std::vector<std::string> temporary_nodes;
  size_t index=0;
  DevToolsCallback callback;
};
void FormSelectChoices(int id,uint64_t generation,Dict config,DevToolsCallback callback) {
  WithFrame(id,FrameId(config),[this,id,generation,config,callback=std::move(callback)](int context) mutable {
    auto work=std::make_shared<FormSelectWork>();work->owner=id;work->generation=generation;work->context=context;work->config=config;work->callback=std::move(callback);
    auto choices=config->GetList("options")->Copy();
    for(size_t i=0;i<choices->GetSize();++i){auto choice=choices->GetDictionary(i);if(choice->HasKey("handle")){work->handles.push_back(choice->GetInt("handle"));choice->SetInt("handle",static_cast<int>(work->handles.size()-1));}}
    work->arguments=CefListValue::Create();work->params=CefDictionaryValue::Create();work->params->SetString("objectId",config->GetString("formNode"));
    work->params->SetString("functionDeclaration","function(...nodes){const choices="+CefWriteJSON([&]{auto value=CefValue::Create();value->SetList(choices);return value;}(),JSON_WRITER_DEFAULT).ToString()+".map(value=>value.handle!==undefined?nodes[value.handle]:value);return ("+SuiteCutSelectScript()+")([this],choices,"+(config->GetBool("force")?"true":"false")+");}");
    work->params->SetBool("returnByValue",true);work->params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
    FormSelectArgument(work);
  },true);
}
void FormSelectArgument(std::shared_ptr<FormSelectWork> work) {
  const int id=work->owner;if(!operations_.IsCurrent(id,work->generation))return;
  if(work->index==work->handles.size()){
    work->params->SetList("arguments",work->arguments);
    DevTools(id,"Runtime.callFunctionOn",work->params,[this,work](bool ok,const std::string& json){
      for(const auto& object:work->temporary_nodes){auto params=CefDictionaryValue::Create();params->SetString("objectId",object);SendDevTools("Runtime.releaseObject",params,operation_sessions_[work->owner]);}
      work->callback(ok,json);
    });return;
  }
  const auto handle=handles_.find(work->handles[work->index]);
  if(handle==handles_.end()){Complete(id,false,MissingHandle(work->handles[work->index]));return;}
  if(handle->second.node_id.empty()||handle->second.session!=operation_sessions_[id]){Complete(id,false,"Unable to adopt option handle from a different document");return;}
  auto params=CefDictionaryValue::Create();params->SetString("objectId",handle->second.node_id);
  DevTools(id,"DOM.describeNode",params,[this,work](bool,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("node"):nullptr;
    if(!node){Complete(work->owner,false,"Native option node unavailable");return;}
    auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",node->GetInt("backendNodeId"));params->SetInt("executionContextId",work->context);params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
    DevTools(work->owner,"DOM.resolveNode",params,[this,work](bool,const std::string& json){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("object"):nullptr;
      if(!node||node->GetType("objectId")!=VTYPE_STRING){Complete(work->owner,false,"Native option node unavailable");return;}
      const auto object=node->GetString("objectId").ToString();work->temporary_nodes.push_back(object);
      auto argument=CefDictionaryValue::Create();argument->SetString("objectId",object);work->arguments->SetDictionary(work->index++,argument);FormSelectArgument(work);
    });
  });
}

void FormAction(int id,uint64_t generation,Dict config,bool verify=false) {
  if(!operations_.IsCurrent(id,generation))return;
  auto path=config->GetList("framePath");
  if(path&&path->GetSize()){
    ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation,verify](Dict resolved){FormAction(id,generation,resolved,verify);});return;
  }
  if(!locator_handlers_.empty()&&!verify&&!config->HasKey("formNode")&&(config->GetString("action")=="setChecked"||config->GetString("action")=="selectOption")&&LocatorHandlerBefore(id,generation,config,[this,id,generation,config]{FormAction(id,generation,config);}))return;
  const auto action=config->GetString("action").ToString();
  if(!config->HasKey("formNode")){
    FormResolveNode(id,generation,config,[this,id,generation,verify](Dict pinned){FormAction(id,generation,pinned,verify);});return;
  }
  const auto expression="("+std::string(kSuiteCutFormInspect)+")([this],"+Literal(action)+","+
    std::string(config->GetBool("checked")?"true":"false")+","+(config->GetBool("force")?"true":"false")+")";
  auto receiver=config;
  if(action=="setChecked"&&config->HasKey("formControl")){receiver=config->Copy(false);receiver->SetString("formNode",config->GetString("formControl"));}
  FormNodeEvaluate(id,receiver,expression,[this,id,generation,config,action,verify](bool,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
    auto details=result?result->GetDictionary("exceptionDetails"):nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
    auto state=remote?remote->GetDictionary("value"):nullptr;
    if(details||!state){FormError(id,action,result);return;}
    if(!state->GetBool("ready")){
      locator_waits_[id]=state->GetString("reason").ToString();
      if(verify){Complete(id,false,"Click did not change the checked state");return;}
      FormReleaseNode(id,config);auto retry=config->Copy(false);retry->Remove("formNode");
      CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,retry]{self->FormAction(id,generation,retry);}),20);return;
    }
    if(action=="setChecked"){
      if(state->GetBool("done")){Complete(id,true);return;}
      if(verify){Complete(id,false,"Click did not change the checked state");return;}
      form_clicks_[id]=[this,id,generation,config]{if(config->GetBool("trial"))Complete(id,true);else FormAction(id,generation,config,true);};
      config->SetBool("formCheck",true);
      context_retries_.erase(id);
      LocatorAction(id,generation,suitecut::InboundDisplay(suitecut::InboundString(config,"selector")),"click","",config);return;
    }
    if(action=="selectOption"){
      FormSelectChoices(id,generation,config,[this,id,generation,config,action](bool,const std::string& json){
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        auto remote=result?result->GetDictionary("result"):nullptr;auto state=remote?remote->GetDictionary("value"):nullptr;
        if(!state||result->HasKey("exceptionDetails")){FormError(id,action,result);return;}
        if(!state->GetBool("ready")){
          locator_waits_[id]=state->GetString("reason").ToString();FormReleaseNode(id,config);auto retry=config->Copy(false);retry->Remove("formNode");
          CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,retry]{self->FormAction(id,generation,retry);}),20);return;
        }
        Complete(id,true,"",CefWriteJSON(state->GetValue("values"),JSON_WRITER_DEFAULT).ToString());
      });return;
    }
    size_t count=0;
    if(config->HasKey("uploadToken")){
      const auto upload=pending_uploads_.find(config->GetString("uploadToken"));
      if(upload==pending_uploads_.end()){Complete(id,false,"Native upload token is missing or expired");return;}
      count=upload->second.paths.size();
    }else count=config->GetList("files")->GetSize();
    if(config->HasKey("uploadToken")&&state->GetBool("directory")){Complete(id,false,"[webkitdirectory] input requires passing a path to a directory");return;}
    const auto files=config->GetList("files");
    const bool folder=files&&files->GetSize()==1&&files->GetDictionary(0)->GetBool("directory");
    if(folder&&!state->GetBool("directory")){Complete(id,false,"File input does not support directories, pass individual files instead");return;}
    if(!folder&&count&&state->GetBool("directory")){Complete(id,false,"[webkitdirectory] input requires passing a path to a directory");return;}
    if(count>1&&!state->GetBool("multiple")){Complete(id,false,"Non-multiple file input can only accept single file");return;}
    FormUpload(id,generation,config);
  });
}

void FormResolveNode(int id,uint64_t generation,Dict config,std::function<void(Dict)> done) {
  WithFrame(id,FrameId(config),[this,id,generation,config,done=std::move(done)](int context) mutable {
    const auto finish=[this,id,generation,config,done=std::move(done)](bool,const std::string& json) mutable {
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      auto remote=result?result->GetDictionary("result"):nullptr;
      if(!remote||result->HasKey("exceptionDetails")){FormError(id,config->GetString("action"),result);return;}
      if(remote->GetType("objectId")!=VTYPE_STRING){
        locator_waits_[id]="element to be attached";
        CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,config]{self->FormAction(id,generation,config);}),20);return;
      }
      auto pinned=config->Copy(false);pinned->SetString("formNode",remote->GetString("objectId"));
      pinned->SetString("frame",operation_frames_[id]);
      if(config->GetString("action")!="setChecked"){done(pinned);return;}
      auto params=CefDictionaryValue::Create();params->SetString("objectId",remote->GetString("objectId"));params->SetBool("returnByValue",false);
      params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));params->SetString("functionDeclaration","function(){return this instanceof HTMLLabelElement&&this.control?this.control:this;}");
      DevTools(id,"Runtime.callFunctionOn",params,[this,id,pinned,done=std::move(done)](bool,const std::string& json) mutable {
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto control=result?result->GetDictionary("result"):nullptr;
        if(!control||control->GetType("objectId")!=VTYPE_STRING){Complete(id,false,"Native checkbox node unavailable");return;}
        pinned->SetString("formControl",control->GetString("objectId"));done(pinned);
      },std::nullopt,false,kMaxResponseBytes,true);
    };
    evaluation_groups_[id].insert(operation_sessions_[id]);
    auto params=CefDictionaryValue::Create();params->SetBool("returnByValue",false);
    params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
    if(config->HasKey("nativeHandle")){
      const auto handle=handles_.find(config->GetInt("nativeHandle"));
      if(handle==handles_.end()){Complete(id,false,MissingHandle(config->GetInt("nativeHandle")));return;}
      auto describe=CefDictionaryValue::Create();describe->SetString("objectId",handle->second.node_id);
      DevTools(id,"DOM.describeNode",describe,[this,id,context,params,config,finish=std::move(finish)](bool,const std::string& json) mutable {
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("node"):nullptr;
        if(!node){Complete(id,false,"Native DOM node unavailable");return;}
        auto resolve=CefDictionaryValue::Create();resolve->SetInt("backendNodeId",node->GetInt("backendNodeId"));resolve->SetInt("executionContextId",context);
        resolve->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
        DevTools(id,"DOM.resolveNode",resolve,[this,id,params,config,finish=std::move(finish)](bool,const std::string& json) mutable {
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("object"):nullptr;
          if(!node||node->GetType("objectId")!=VTYPE_STRING){Complete(id,false,"Native DOM node unavailable");return;}
          params->SetString("objectId",node->GetString("objectId"));params->SetString("functionDeclaration","function(){return "+std::string(config->GetString("action")=="setChecked"?"this":"this instanceof HTMLLabelElement&&this.control?this.control:this")+";}");
          DevTools(id,"Runtime.callFunctionOn",params,std::move(finish),std::nullopt,false,kMaxResponseBytes,true);
        });
      });
    }else{
      const auto frame=frame_states_.find(FrameId(config));if(frame==frame_states_.end()){Complete(id,false,"Native form frame detached");return;}
      const bool main=QueryNeedsMainWorld(config)&&!QueryUsesMultipleWorlds(config);const int selected=main?frame->second.context:context;
      const auto loader=frame->second.loader,session=frame->second.session;
      params->SetInt("contextId",selected);params->SetString("expression","(()=>{const nodes="+QueryExpression(config)+";if(nodes.length>1&&"+std::string(config->GetType("strict")==VTYPE_BOOL&&!config->GetBool("strict")?"false":"true")+")throw new Error('Strict locator matched multiple elements');let e=nodes[0];return "+std::string(config->GetString("action")=="setChecked"?"e":"e instanceof HTMLLabelElement&&e.control?e.control:e")+";})()");
      if(QueryUsesMultipleWorlds(config)){
        QueryInspect(id,config,"if(nodes.length>1&&"+std::string(config->GetType("strict")==VTYPE_BOOL&&!config->GetBool("strict")?"false":"true")+")throw new Error('Strict locator matched multiple elements');let e=nodes[0];return "+std::string(config->GetString("action")=="setChecked"?"e":"e instanceof HTMLLabelElement&&e.control?e.control:e")+";",true,false,std::move(finish));return;
      }
      if(!main){DevTools(id,"Runtime.evaluate",params,std::move(finish),std::nullopt,false,kMaxResponseBytes,true);return;}
      DevTools(id,"Runtime.evaluate",params,[this,id,generation,config,context,selected,loader,session,finish=std::move(finish)](bool ok,const std::string& json) mutable {
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
        if(!ok||!result||result->HasKey("exceptionDetails")||!remote||remote->GetType("objectId")!=VTYPE_STRING){finish(ok,json);return;}
        SuiteCutQueryNodeTransfer::AdoptNode(
          [this,id](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result result){DevTools(id,method,params,std::move(result));},
          [this,id,generation,config,context,selected,loader,session]{const auto frame=frame_states_.find(FrameId(config));return operations_.IsCurrent(id,generation)&&frame!=frame_states_.end()&&frame->second.context==selected&&frame->second.utility_context==context&&frame->second.loader==loader&&frame->second.session==session;},
          [this,id,generation](const std::string& error){if(operations_.IsCurrent(id,generation))Complete(id,false,error);},remote->GetString("objectId").ToString(),context,"suitecut-automation-"+std::to_string(id),
          [finish=std::move(finish)](Dict node) mutable {auto result=CefDictionaryValue::Create();result->SetDictionary("result",node);finish(true,Json(result));});
      },std::nullopt,false,kMaxResponseBytes,true);
    }
  },true);
}

void FormError(int id,const std::string& action,Dict result) {
  auto details=result?result->GetDictionary("exceptionDetails"):nullptr;auto exception=details?details->GetDictionary("exception"):nullptr;
  auto message=exception?exception->GetString("description").ToString():"Native form evaluation failed";
  message=message.substr(0,message.find('\n'));Complete(id,false,action+": "+message);
}

void FormReleaseNode(int id,Dict config) {
  if(!config->HasKey("formNode"))return;
  std::set<std::string> objects{config->GetString("formNode").ToString()};
  if(config->HasKey("formControl"))objects.insert(config->GetString("formControl").ToString());
  for(const auto& object:objects){auto params=CefDictionaryValue::Create();params->SetString("objectId",object);SendDevTools("Runtime.releaseObject",params,operation_sessions_[id]);}
}

struct FormNativeSelectionWork {
  int owner=0;uint64_t generation=0;bool directory=false,submitted=false,canceled=false,completion_ready=false,completion_ok=false,retirement_failed=false;
  std::string object,session,key,group,completion_error;Dict config;
};
std::shared_ptr<FormNativeSelectionWork> native_file_selection_;
bool FormFileCurrent(int id,uint64_t generation) {
  if(!operations_.IsCurrent(id,generation))return false;
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Timeout exceeded: native file selection exceeded its original action deadline");return false;}
  return true;
}
void FormUpload(int id,uint64_t generation,Dict config) {
  if(!operations_.IsCurrent(id,generation))return;
  if(config->HasKey("uploadToken")){FormPayloadStart(id,generation,config);return;}
  if(config->GetList("files")->GetSize()==1&&config->GetList("files")->GetDictionary(0)->GetBool("directory")){FormUploadFolder(id,generation,config);return;}
  if(config->GetList("files")->GetSize()==0){FormNativeSelection(id,generation,config,false);return;}
  auto describe=CefDictionaryValue::Create();describe->SetString("objectId",config->GetString("formNode"));
  DevTools(id,"DOM.describeNode",describe,[this,id,generation,config](bool,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("node"):nullptr;
    if(!node){Complete(id,false,"Native file input unavailable");return;}
    const auto key=operation_sessions_[id]+":"+std::to_string(node->GetInt("backendNodeId"));
    auto staged=std::make_shared<SuiteCutUploadFiles>();auto paths=CefListValue::Create();
    size_t bytes=0;bool memory=false;std::string token;
    try{
      if(config->HasKey("uploadToken")){
        token=config->GetString("uploadToken");const auto found=pending_uploads_.find(token);
        if(found==pending_uploads_.end())throw std::invalid_argument("Native upload token is missing or expired");
        for(size_t i=0;i<found->second.paths.size();++i){
          if(found->second.received[i]!=found->second.expected[i])throw std::invalid_argument("Native upload has incomplete file bytes");
          paths->SetString(i,found->second.paths[i]);
        }
        staged=found->second.files;bytes=found->second.bytes;memory=true;
      }else{
        const auto files=config->GetList("files");
        for(size_t i=0;i<files->GetSize();++i){
          const auto file=files->GetDictionary(i);const auto path=file->GetString("path").ToString();
          if(path.find('\0')!=std::string::npos||!std::filesystem::path(path).is_absolute()||!std::filesystem::is_regular_file(path))throw std::invalid_argument("Upload path must be an absolute regular file");
          paths->SetString(i,path);
        }
      }
      if(memory&&!upload_store_.CanReplace(key,bytes))throw std::invalid_argument("Native temporary upload capacity exceeded (128 MiB / 1024 inputs)");
    }catch(const std::exception& error){Complete(id,false,error.what());return;}
    form_uploads_[id]=staged;
    auto params=CefDictionaryValue::Create();params->SetList("files",paths);params->SetInt("backendNodeId",node->GetInt("backendNodeId"));
    context_retries_.erase(id);
    FormFileMutation(id,generation,"DOM.setFileInputFiles",params,[this,id,generation,config,key,bytes,memory,staged,token](bool ok,const std::string& json){
      if(!ok){Complete(id,false,"Native file upload command failed: "+json.substr(0,1024));return;}
      if(!operations_.IsCurrent(id,generation))return;
      upload_store_.Replace(key,FrameId(config),bytes,memory?staged:nullptr);
      if(!token.empty())DropPendingUpload(token);
      Complete(id,true);
    });
  });
}

void DropPendingUpload(const std::string& token) {
  const auto found=pending_uploads_.find(token);if(found==pending_uploads_.end())return;
  pending_upload_bytes_-=found->second.bytes;pending_uploads_.erase(found);
}
void PumpFormUploads() {
  const auto now=Now();
  for(auto it=expired_upload_tokens_.begin();it!=expired_upload_tokens_.end();)if(now>=it->second)it=expired_upload_tokens_.erase(it);else ++it;
  std::vector<std::string> expired;
  for(const auto& [token,upload]:pending_uploads_)if(now>=upload.expires||(upload.deadline&&now>=upload.deadline))expired.push_back(token);
  for(const auto& token:expired){
    const auto found=pending_uploads_.find(token);if(found==pending_uploads_.end())continue;
    const auto owner=found->second.active_owner;const auto generation=found->second.active_generation;
    if(expired_upload_tokens_.size()>=128)expired_upload_tokens_.erase(expired_upload_tokens_.begin());
    expired_upload_tokens_[token]=now+60000;
    if(operations_.IsCurrent(owner,generation))Complete(owner,false,"Timeout exceeded: native upload exceeded its original action or idle deadline");
    DropPendingUpload(token);
  }
}
void FormStageCommand(int id,Dict message) {
  PumpFormUploads();const auto action=message->GetString("action").ToString();
  if(action!="uploadAbort"&&message->GetType("uploadToken")==VTYPE_STRING&&expired_upload_tokens_.contains(message->GetString("uploadToken"))){Complete(id,false,"Timeout exceeded: native upload exceeded its original action or idle deadline");return;}
  if(action=="uploadBegin"){
    const auto metadata=message->GetList("files");
    if(!metadata||metadata->GetSize()==0||metadata->GetSize()>1000||pending_uploads_.size()>=64){Complete(id,false,"Invalid native upload metadata or staging group capacity exceeded");return;}
    SuiteCutPendingUpload upload;upload.files=std::make_shared<SuiteCutUploadFiles>();
    if(const auto entry=operations_.Get(id))upload.deadline=entry->deadline_ms;
    try{
      for(size_t i=0;i<metadata->GetSize();++i){
        const auto file=metadata->GetDictionary(i);
        if(!file||file->GetType("name")!=VTYPE_STRING||file->GetType("mimeType")!=VTYPE_STRING||file->GetString("mimeType").length()>255||file->GetType("size")!=VTYPE_INT||file->GetInt("size")<0)throw std::invalid_argument("Invalid native upload metadata");
        const auto size=static_cast<size_t>(file->GetInt("size"));
        if(size>128*1048576-upload.bytes)throw std::invalid_argument("Native upload staging exceeds 128 MiB");
        upload.bytes+=size;upload.expected.push_back(size);upload.received.push_back(0);
        upload.names.push_back(file->GetString("name"));upload.mime_types.push_back(file->GetString("mimeType"));
        upload.paths.push_back(upload.files->Stage(file->GetString("name"),""));
      }
      if(upload.bytes>128*1048576-upload_store_.Bytes()-pending_upload_bytes_)throw std::invalid_argument("Native upload staging capacity exceeded (128 MiB)");
      if(upload.deadline&&Now()>=upload.deadline)throw std::invalid_argument("Timeout exceeded: native upload exceeded its original action deadline");
    }catch(const std::exception& error){Complete(id,false,error.what());return;}
    upload.expires=Now()+60000;const auto token=HandleNamespace();pending_upload_bytes_+=upload.bytes;
    pending_uploads_.emplace(token,std::move(upload));Complete(id,true,"",Literal(token));return;
  }
  if(message->GetType("uploadToken")!=VTYPE_STRING||message->GetString("uploadToken").length()>128){Complete(id,false,"Invalid native upload token");return;}
  const auto token=message->GetString("uploadToken").ToString();
  if(action=="uploadAbort"){DropPendingUpload(token);Complete(id,true);return;}
  const auto found=pending_uploads_.find(token);
  if(found==pending_uploads_.end()){Complete(id,false,"Native upload token is missing or expired");return;}
  if(message->GetType("fileIndex")!=VTYPE_INT||message->GetInt("fileIndex")<0||static_cast<size_t>(message->GetInt("fileIndex"))>=found->second.paths.size()||message->GetType("base64")!=VTYPE_STRING||message->GetString("base64").length()>32768){Complete(id,false,"Invalid native upload chunk");return;}
  const auto data=CefBase64Decode(message->GetString("base64"));
  const auto index=static_cast<size_t>(message->GetInt("fileIndex"));
  if(!data||data->GetSize()>24576||data->GetSize()>found->second.expected[index]-found->second.received[index]){Complete(id,false,"Native upload chunk exceeds declared file size");return;}
  std::string contents(data->GetSize(),'\0');if(!contents.empty())data->GetData(contents.data(),contents.size(),0);
  try{found->second.files->Append(found->second.paths[index],contents);}catch(const std::exception& error){DropPendingUpload(token);Complete(id,false,error.what());return;}
  found->second.received[index]+=contents.size();found->second.expires=Now()+60000;Complete(id,true);
}

struct FormPayloadWork {
  int owner=0;
  uint64_t generation=0;
  Dict config;
  std::string token,object;
  SuiteCutPendingUpload upload;
  size_t index=0,offset=0;
  std::ifstream stream;
};
void FormPayloadStart(int id,uint64_t generation,Dict config) {
  const auto token=config->GetString("uploadToken").ToString();const auto found=pending_uploads_.find(token);
  if(found==pending_uploads_.end()){Complete(id,false,"Native upload token is missing or expired");return;}
  for(size_t i=0;i<found->second.paths.size();++i)if(found->second.received[i]!=found->second.expected[i]){Complete(id,false,"Native upload has incomplete file bytes");return;}
  auto work=std::make_shared<FormPayloadWork>();work->owner=id;work->generation=generation;work->config=config;work->token=token;work->upload=found->second;
  form_uploads_[id]=work->upload.files;context_retries_.erase(id);
  auto metadata=CefListValue::Create();
  for(size_t i=0;i<work->upload.paths.size();++i){auto file=CefDictionaryValue::Create();file->SetString("name",work->upload.names[i]);file->SetString("mimeType",work->upload.mime_types[i]);metadata->SetDictionary(i,file);}
  auto params=CefDictionaryValue::Create();params->SetString("objectId",config->GetString("formNode"));
  auto arguments=CefListValue::Create();auto files=CefDictionaryValue::Create();files->SetList("value",metadata);arguments->SetDictionary(0,files);params->SetList("arguments",arguments);
  params->SetString("functionDeclaration","function(metadata){return {input:this,files:metadata.map(file=>({...file,chunks:[]}))};}");
  params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));params->SetBool("returnByValue",false);
  DevTools(id,"Runtime.callFunctionOn",params,[this,work](bool,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
    if(!remote||remote->GetType("objectId")!=VTYPE_STRING||result->HasKey("exceptionDetails")){FormError(work->owner,"setInputFiles",result);return;}
    work->object=remote->GetString("objectId");FormPayloadNext(work);
  });
}
void FormPayloadNext(std::shared_ptr<FormPayloadWork> work) {
  const int id=work->owner;if(!operations_.IsCurrent(id,work->generation))return;
  if(work->upload.deadline&&Now()>=work->upload.deadline){DropPendingUpload(work->token);Complete(id,false,"Timeout exceeded: native upload exceeded its original action deadline");return;}
  const auto pending=pending_uploads_.find(work->token);if(pending!=pending_uploads_.end())pending->second.expires=Now()+60000;
  if(work->index==work->upload.paths.size()){
    auto params=CefDictionaryValue::Create();params->SetString("objectId",work->object);params->SetBool("returnByValue",true);
    params->SetString("functionDeclaration","function(){const transfer=new DataTransfer();for(const file of this.files)transfer.items.add(new File(file.chunks,file.name,{type:file.mimeType}));this.input.files=transfer.files;this.input.dispatchEvent(new Event('input',{bubbles:true,composed:true}));this.input.dispatchEvent(new Event('change',{bubbles:true}));return true;}");
    FormFileMutation(id,work->generation,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string& json){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")){FormError(work->owner,"setInputFiles",result);return;}
      DropPendingUpload(work->token);Complete(work->owner,true);
    });return;
  }
  const auto size=work->upload.expected[work->index];
  if(work->offset==size){
    if(work->stream.is_open())work->stream.close();
    ++work->index;work->offset=0;
    CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this),work]{self->FormPayloadNext(work);}));return;
  }
  if(!work->stream.is_open()){
    work->stream.open(work->upload.paths[work->index],std::ios::binary);
    if(!work->stream){Complete(id,false,"Cannot read staged native upload file");return;}
  }
  const auto count=std::min<size_t>(24576,size-work->offset);std::string chunk(count,'\0');
  work->stream.read(chunk.data(),static_cast<std::streamsize>(count));
  if(static_cast<size_t>(work->stream.gcount())!=count){Complete(id,false,"Staged native upload file is incomplete");return;}
  work->offset+=count;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->object);params->SetBool("returnByValue",true);
  params->SetString("functionDeclaration","function(index,base64){const text=atob(base64),bytes=new Uint8Array(text.length);for(let i=0;i<text.length;++i)bytes[i]=text.charCodeAt(i);this.files[index].chunks.push(bytes);}");
  auto arguments=CefListValue::Create();auto index=CefDictionaryValue::Create();index->SetInt("value",static_cast<int>(work->index));arguments->SetDictionary(0,index);
  auto data=CefDictionaryValue::Create();data->SetString("value",CefBase64Encode(chunk.data(),chunk.size()));arguments->SetDictionary(1,data);params->SetList("arguments",arguments);
  DevTools(id,"Runtime.callFunctionOn",params,[this,work](bool,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
    if(!result||result->HasKey("exceptionDetails")){FormError(work->owner,"setInputFiles",result);return;}
    FormPayloadNext(work);
  });
}

struct FormFolderObserver { std::string object,session; };
void FormCancelFolder(int id) {
  if(native_file_selection_&&native_file_selection_->owner==id){
    // The dedicated object group and retained native ACK survive logical
    // cancellation. A late native enumeration must not unlock a newer action.
    native_file_selection_->canceled=true;return;
  }
  const auto observer=folder_observers_.find(id);
  if(observer!=folder_observers_.end()){
    auto params=CefDictionaryValue::Create();params->SetString("objectId",observer->second.object);
    params->SetString("functionDeclaration","function(){this.cancel();}");params->SetBool("returnByValue",true);
    SendDevTools("Runtime.callFunctionOn",params,observer->second.session);folder_observers_.erase(observer);
  }
  // If Chromium has not delivered the native dialog callback yet, retain only
  // this small request marker so that the late callback is canceled safely.
  if(folder_operation_==id&&folder_continued_){folder_operation_=0;folder_path_.clear();folder_continued_=false;}
}
bool FormFileDialog(cef_file_dialog_mode_t mode,CefRefPtr<CefFileDialogCallback> callback) {
  if(!folder_operation_)return false;
  const int id=folder_operation_;
  if(mode!=FILE_DIALOG_OPEN_FOLDER||!operations_.IsCurrent(id,folder_generation_)){
    callback->Cancel();folder_operation_=0;folder_path_.clear();folder_continued_=false;
    if(operations_.Contains(id))Complete(id,false,"Native directory picker was canceled");
    return true;
  }
  folder_continued_=true;callback->Continue({folder_path_});return true;
}
// These callbacks own actual CEF DevTools ACKs independently of the logical
// command. No timeout, observer cancellation or object-group release stands in
// for accepted native enumeration completion.
void FormNativeControl(std::shared_ptr<FormNativeSelectionWork> work,const std::string& method,Dict params,DevToolsCallback callback) {
  const int wire=AllocateDevToolsId();
  if(!wire||devtools_.size()>=1024){callback(false,"Native file-selection control capacity exceeded");return;}
  devtools_.emplace(wire,DevToolsCall{work->owner,suitecut::BindNativeLocatorHandlerAsync(std::move(callback)),work->session,true,65536,true});
  if(!SubmitDevTools(wire,method,params,work->session)){
    const auto found=devtools_.find(wire);if(found!=devtools_.end()){auto done=std::move(found->second.callback);devtools_.erase(found);done(false,"Native file-selection control dispatch was refused");}
  }
}
// All file mutations, including already-staged ordinary uploads, enter the
// same page-local lane at the mutation boundary. This prevents another action's
// genuine event from satisfying a retained native selection observer.
void FormFileMutation(int id,uint64_t generation,const std::string& method,Dict params,DevToolsCallback callback) {
  if(!FormFileCurrent(id,generation))return;
  if(native_file_selection_){
    if(native_file_selection_->retirement_failed){Complete(id,false,"Native file selection retirement failed; page closure is in progress");return;}
    locator_waits_[id]="previous native file selection to physically finish";
    CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,method,params,callback=std::move(callback)]()mutable{self->FormFileMutation(id,generation,method,params,std::move(callback));}),20);return;
  }
  auto work=std::make_shared<FormNativeSelectionWork>();work->owner=id;work->generation=generation;work->session=operation_sessions_[id];work->submitted=true;native_file_selection_=work;
  FormNativeControl(work,method,params,[this,work,callback=std::move(callback)](bool ok,const std::string& json)mutable{
    if(FormFileCurrent(work->owner,work->generation))callback(ok,json);
    if(native_file_selection_==work)native_file_selection_.reset();
  });
}
void FormNativeRetirementFailed(std::shared_ptr<FormNativeSelectionWork> work,const std::string& error) {
  const bool already=work->retirement_failed;work->retirement_failed=true;
  if(FormFileCurrent(work->owner,work->generation))Complete(work->owner,false,error);
  // Only actual OnBeforeClose retires an unknown accepted backend operation.
  // Request that real retirement, rather than leaving idle waits permanently
  // blocked behind a failed lane or inventing a cleanup ACK.
  if(!already&&browser_){if(closed_reason_.empty())closed_reason_=error;browser_->GetHost()->CloseBrowser(true);}
}
void FormNativeRelease(std::shared_ptr<FormNativeSelectionWork> work) {
  auto params=CefDictionaryValue::Create();params->SetString("objectGroup",work->group);
  FormNativeControl(work,"Runtime.releaseObjectGroup",params,[this,work](bool ok,const std::string&){
    if(!ok){
      FormNativeRetirementFailed(work,"Native file selection object-group release was not acknowledged; closing the page");
      return;  // The retained group/lane remains owned until actual page death.
    }
    folder_observers_.erase(work->owner);if(native_file_selection_==work)native_file_selection_.reset();
    if(work->completion_ready&&FormFileCurrent(work->owner,work->generation))Complete(work->owner,work->completion_ok,work->completion_error);
  });
}
void FormNativeAbortBeforeCompletion(std::shared_ptr<FormNativeSelectionWork> work) {
  if(work->object.empty()){FormNativeRelease(work);return;}
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->object);params->SetString("functionDeclaration","function(){this.cancel();}");params->SetBool("returnByValue",true);
  FormNativeControl(work,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string&){
    if(!ok){
      FormNativeRetirementFailed(work,"Native file selection observer cleanup was not acknowledged; closing the page");
      return;
    }
    FormNativeRelease(work);
  });
}
void FormNativeFail(std::shared_ptr<FormNativeSelectionWork> work,const std::string& error) {
  if(FormFileCurrent(work->owner,work->generation))Complete(work->owner,false,error);
  FormNativeAbortBeforeCompletion(work);
}
void FormNativeObserve(std::shared_ptr<FormNativeSelectionWork> work) {
  auto params=CefDictionaryValue::Create();params->SetString("objectId",work->object);params->SetBool("returnByValue",true);params->SetBool("awaitPromise",work->directory);
  params->SetString("functionDeclaration",work->directory?"function(){return this.promise;}":"function(){return this.inspect();}");
  FormNativeControl(work,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;auto state=remote?remote->GetDictionary("value"):nullptr;
    if(!ok||!state||result->HasKey("exceptionDetails")){
      // A failed observer command is not proof that accepted enumeration ended.
      // Keep this lane closed until real page retirement; do not fabricate an
      // ACK by canceling the observer or starting another mutation.
      FormNativeRetirementFailed(work,"Native file selection observation failed; closing the page to retire the accepted operation");
      return;
    }
    if(!FormFileCurrent(work->owner,work->generation)){
      if(state->GetString("kind")=="pending")FormNativeAbortBeforeCompletion(work);else FormNativeRelease(work);return;
    }
    const auto kind=state->GetString("kind").ToString();const auto events=state->GetList("events");
    const bool changed=kind=="changed"&&events&&events->GetSize()==2&&events->GetString(0)=="input"&&events->GetString(1)=="change"&&state->GetBool("replaced");
    const bool empty_unchanged=!work->directory&&kind=="canceled"&&events&&events->GetSize()==1&&events->GetString(0)=="cancel"&&state->GetInt("originalCount")==0&&state->GetInt("count")==0;
    std::string error;
    if(!state->GetBool("sameDocument"))error="Original native file input document changed";
    else if(work->directory&&kind=="canceled")error="Native directory selection was canceled before completion";
    else if(!changed&&!empty_unchanged)error=kind=="pending"&&!work->directory?"Pinned CEF backend does not implement genuine native empty file selection":"Native file selection did not produce the original input's trusted completion events";
    else if(work->directory&&!state->GetBool("relativePathsValid"))error="Native directory selection did not preserve browser relative paths";
    else if(!work->directory&&state->GetInt("count")!=0)error="Native file selection did not clear the original input";
    if(error.empty())upload_store_.Replace(work->key,FrameId(work->config),0,nullptr);
    work->completion_ready=true;work->completion_ok=error.empty();work->completion_error=error;
    if(kind=="pending")FormNativeAbortBeforeCompletion(work);else FormNativeRelease(work);
  });
}
void FormNativeSelection(int id,uint64_t generation,Dict config,bool directory) {
  if(!FormFileCurrent(id,generation))return;
  if(native_file_selection_){
    if(native_file_selection_->retirement_failed){Complete(id,false,"Native file selection retirement failed; page closure is in progress");return;}
    locator_waits_[id]="previous native file selection to physically finish";
    CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,config,directory]{self->FormNativeSelection(id,generation,config,directory);}),20);return;
  }
  context_retries_.erase(id);
  auto work=std::make_shared<FormNativeSelectionWork>();work->owner=id;work->generation=generation;work->config=config;work->directory=directory;work->session=operation_sessions_[id];work->group="suitecut-native-files-"+std::to_string(id)+"-"+std::to_string(generation);native_file_selection_=work;
  auto params=CefDictionaryValue::Create();params->SetString("objectId",config->GetString("formNode"));params->SetBool("returnByValue",false);params->SetString("objectGroup",work->group);
  auto arguments=CefListValue::Create();auto root=CefDictionaryValue::Create();std::string directory_root;
  if(directory){auto path=std::filesystem::path(config->GetList("files")->GetDictionary(0)->GetString("path").ToString()).lexically_normal();while(path.filename().empty()&&path.has_relative_path())path=path.parent_path();directory_root=path.filename().string();}
  root->SetString("value",directory_root);arguments->SetDictionary(0,root);params->SetList("arguments",arguments);
  params->SetString("functionDeclaration",R"JS(function(root){
    const input=this,original=input.files,owner=input.ownerDocument;
    const state={input,original,owner,events:[],settled:false,outcome:null};
    const cleanup=()=>{input.removeEventListener('input',onInput,true);input.removeEventListener('change',onChange,true);input.removeEventListener('cancel',onCancel,true);};
    let resolve;state.promise=new Promise(done=>{resolve=done;});
    const finish=kind=>{if(state.settled)return;state.settled=true;cleanup();state.outcome={kind,events:state.events.slice(),count:input.files.length,replaced:input.files!==original,originalCount:original.length,sameDocument:input.ownerDocument===owner,relativePathsValid:kind!=='changed'||(()=>{for(const file of input.files){const path=file.webkitRelativePath;if(!path||path.startsWith('/')||path.split('/').includes('..')||(root&&!path.startsWith(root+'/')))return false;}return true;})()};resolve(state.outcome);};
    const record=kind=>{if(state.events.length<3)state.events.push(kind);};
    const onInput=event=>{if(event.target===input&&event.isTrusted)record('input');};
    const onChange=event=>{if(event.target===input&&event.isTrusted){record('change');finish('changed');}};
    const onCancel=event=>{if(event.target===input&&event.isTrusted){record('cancel');finish('canceled');}};
    state.cancel=()=>finish('aborted');
    state.inspect=()=>state.outcome||{kind:'pending',events:state.events.slice(),count:input.files.length,replaced:input.files!==original,originalCount:original.length,sameDocument:input.ownerDocument===owner};
    input.addEventListener('input',onInput,true);input.addEventListener('change',onChange,true);input.addEventListener('cancel',onCancel,true);
    return state;
  })JS");
  FormNativeControl(work,"Runtime.callFunctionOn",params,[this,work](bool ok,const std::string& json){
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
    if(!ok||!remote||remote->GetType("objectId")!=VTYPE_STRING||result->HasKey("exceptionDetails")){FormNativeFail(work,"Native original file-selection observer installation failed");return;}
    work->object=remote->GetString("objectId").ToString();folder_observers_[work->owner]={work->object,work->session};
    if(work->canceled||!FormFileCurrent(work->owner,work->generation)){FormNativeAbortBeforeCompletion(work);return;}
    auto describe=CefDictionaryValue::Create();describe->SetString("objectId",work->config->GetString("formNode"));
    FormNativeControl(work,"DOM.describeNode",describe,[this,work](bool ok,const std::string& json){
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto node=result?result->GetDictionary("node"):nullptr;
      if(!ok||!node||node->GetType("backendNodeId")!=VTYPE_INT){FormNativeFail(work,"Native original file input is unavailable");return;}
      if(work->canceled||!FormFileCurrent(work->owner,work->generation)){FormNativeAbortBeforeCompletion(work);return;}
      work->key=work->session+":"+std::to_string(node->GetInt("backendNodeId"));
      auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",node->GetInt("backendNodeId"));auto paths=CefListValue::Create();
      if(work->directory)paths->SetString(0,work->config->GetList("files")->GetDictionary(0)->GetString("path"));
      params->SetList("files",paths);work->submitted=true;
      FormNativeControl(work,"DOM.setFileInputFiles",params,[this,work](bool ok,const std::string& json){
        if(!ok){FormNativeFail(work,"Native file selection command failed: "+json.substr(0,1024));return;}
        // Continue to the real native terminal event even after logical cancel.
        FormNativeObserve(work);
      });
    });
  });
}
void FormUploadFolder(int id,uint64_t generation,Dict config) {
  const auto path=config->GetList("files")->GetDictionary(0)->GetString("path").ToString();std::error_code error;
  if(path.find('\0')!=std::string::npos||!std::filesystem::path(path).is_absolute()||!std::filesystem::is_directory(path,error)){Complete(id,false,"Upload directory must be an absolute directory path");return;}
  Complete(id,false,"Pinned stock CEF Alloy does not implement native directory enumeration; the isolated directory adapter requires a verified custom CEF build");
}
