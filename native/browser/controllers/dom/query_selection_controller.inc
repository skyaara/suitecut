// SPDX-License-Identifier: MIT
// Builtin selection reads genuine DOM/CSS in the isolated world in one atomic
// helper invocation. Author callbacks still receive wrappers for those exact
// backend nodes in their original main world; no selector is rerun there.
bool QueryBuiltinNodesInWorld(int id,Dict message,bool to_utility,const std::function<void(const std::string&)>& done){
  if(QueryHasSnapshotReferences(message)||QueryNeedsMainWorld(message)||QueryNeedsIsolatedWorld(message)||message->HasKey("nativeRootHandle")||message->HasKey("nativeSelectorWaitState"))return false;
  const auto entry=operations_.Get(id);if(!entry)return true;
  const auto frame_id=FrameId(message);
  WithFrame(id,frame_id,[this,id,generation=entry->generation,frame_id,message,to_utility,done](int utility){
    if(!operations_.IsCurrent(id,generation))return;
    const auto frame=frame_states_.find(frame_id);
    if(frame==frame_states_.end()||!frame->second.context){Complete(id,false,"Native query main context unavailable");return;}
    const int main=frame->second.context,destination=to_utility?utility:main;
    const auto loader=frame->second.loader,session=frame->second.session;
    auto current=[this,id,generation,frame_id,main,utility,loader,session]{const auto frame=frame_states_.find(frame_id);return operations_.IsCurrent(id,generation)&&frame!=frame_states_.end()&&frame->second.context==main&&frame->second.utility_context==utility&&frame->second.loader==loader&&frame->second.session==session;};
    auto fail=[this,id,generation,current](const std::string& error){if(!operations_.IsCurrent(id,generation))return;if(!current()&&frame_states_.contains(operation_frames_[id]))RetryContext(id);else Complete(id,false,error);};
    const auto group="suitecut-automation-"+std::to_string(id);evaluation_groups_[id].insert(session);
    auto params=CefDictionaryValue::Create();params->SetInt("contextId",utility);params->SetBool("returnByValue",false);params->SetString("objectGroup",group);
    auto expression=QueryExpression(message);
    // Immediate first-match handle APIs need only their actual first node.
    // Strict and array/user-function APIs retain their full match set.
    if(message->GetString("action")=="create"&&!message->GetBool("all")&&!message->GetBool("strict")&&message->GetType("queryFunction")!=VTYPE_STRING)
      expression="(()=>{const nodes="+expression+";return nodes.length?[nodes[0]]:[];})()";
    params->SetString("expression",expression);
    DevTools(id,"Runtime.evaluate",params,[this,id,current,fail,utility,destination,group,session,done](bool ok,const std::string& json){
      if(!current()){fail("Native query document changed during builtin selection");return;}
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote||remote->GetString("subtype")!="array"||remote->GetType("objectId")!=VTYPE_STRING||remote->GetString("objectId").empty()){
        auto details=result?result->GetDictionary("exceptionDetails"):nullptr;auto exception=details?details->GetDictionary("exception"):nullptr;
        const auto cause=exception?suitecut::DevToolsString(exception->GetValue("description")):result?result->GetString("message").ToString():"";
        fail(cause.empty()?"Native builtin selector execution failed":cause);return;
      }
      const auto array=remote->GetString("objectId").ToString();
      if(utility==destination){done(array);return;}
      SuiteCutQueryNodeTransfer::Transfer(
        [this,id,session](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result result){const bool metadata=params&&params->HasKey("serializationOptions");DevTools(id,method,params,std::move(result),session,false,metadata?16*1048576:kMaxResponseBytes,metadata||method=="Runtime.getProperties");},
        current,fail,array,destination,group,done);
    },session,false,kMaxResponseBytes,true);
  },true);
  return true;
}
