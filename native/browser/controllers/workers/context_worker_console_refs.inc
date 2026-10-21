// SPDX-License-Identifier: MIT
// Included inside Source after worker_controller.inc. Console references stay in
// their actual worker Runtime session and are never adopted into a DOM frame.
struct WorkerConsoleRaw {
  Dict value;
  std::string worker, session;
  int context = 0;
  uint64_t generation = 0, sequence = 0;
  size_t bytes = 0;
  bool claimed = false, delivered = false;
};
std::map<int,WorkerConsoleRaw> worker_console_arguments_;
size_t worker_console_bytes_ = 0;

bool WorkerConsoleDrop(int handle, bool release = true) {
  auto found=worker_console_arguments_.find(handle);if(found==worker_console_arguments_.end())return false;
  const auto argument=found->second;worker_console_bytes_-=argument.bytes;worker_console_arguments_.erase(found);
  if(release&&browser_&&!closing_page_&&argument.value->GetType("objectId")==VTYPE_STRING&&worker_sessions_.contains(argument.session)) {
    auto params=CefDictionaryValue::Create();params->SetString("objectId",argument.value->GetString("objectId"));SendDevTools("Runtime.releaseObject",params,argument.session);
  }
  const auto retained=worker_handles_.find(handle);
  if(retained!=worker_handles_.end()){WorkerReleaseGroup(retained->second.group,retained->second.session);worker_handles_.erase(retained);}
  return true;
}
void WorkerConsoleClear(bool include_claimed=false) {
  std::vector<int> removed;for(const auto& [handle,argument]:worker_console_arguments_)if(include_claimed||!argument.claimed)removed.push_back(handle);
  for(const auto handle:removed)WorkerConsoleDrop(handle);
}
void WorkerConsoleRuntimeEvent(const std::string& method,Dict data,const std::string& session) {
  const auto mapped=worker_sessions_.find(session);if(mapped==worker_sessions_.end())return;
  if(method=="Runtime.executionContextsCleared"||(method=="Runtime.executionContextDestroyed"&&data->GetInt("executionContextId")==worker_targets_.at(mapped->second).context))WorkerConsoleSessionRemoved(session);
}
void WorkerConsoleSessionRemoved(const std::string& session) {
  std::vector<int> removed;for(const auto& [handle,argument]:worker_console_arguments_)if(argument.session==session)removed.push_back(handle);
  for(const auto handle:removed)WorkerConsoleDrop(handle,false);
}
void WorkerConsoleClaim(int handle) {
  const auto found=worker_console_arguments_.find(handle);if(found!=worker_console_arguments_.end())found->second.claimed=true;
}
void WorkerConsoleDelivered(uint64_t sequence) {
  for(auto& [handle,argument]:worker_console_arguments_)if(argument.sequence<=sequence)argument.delivered=true;
}
void WorkerConsoleEvicted() {
  const auto oldest=console_journal_.Oldest();std::vector<int> removed;
  for(const auto& [handle,argument]:worker_console_arguments_)if(!argument.claimed&&argument.delivered&&(!oldest||argument.sequence<oldest))removed.push_back(handle);
  for(const auto handle:removed)WorkerConsoleDrop(handle);
}
Dict WorkerConsoleRetain(Dict value,const std::string& session,int context) {
  const auto mapped=worker_sessions_.find(session);if(mapped==worker_sessions_.end()||!value)return nullptr;
  const auto& target=worker_targets_.at(mapped->second);
  if(target.context!=context||context<=0||worker_handles_.size()>=1024||next_handle_==std::numeric_limits<int>::max())return nullptr;
  const auto bytes=Json(value).size();if(bytes>1048576||worker_console_bytes_+bytes>4*1048576)return nullptr;
  const int handle=++next_handle_;const auto group="suitecut-worker-console-"+handle_namespace_+"-"+std::to_string(handle);
  worker_handles_.emplace(handle,WorkerHandle{"",group,target.id,session,context,target.generation});
  worker_console_arguments_.emplace(handle,WorkerConsoleRaw{value->Copy(false),target.id,session,context,target.generation,event_sequence_+1,bytes,false,false});worker_console_bytes_+=bytes;
  auto result=CefDictionaryValue::Create();result->SetInt("handle",handle);result->SetString("worker",target.id);result->SetBool("node",false);
  result->SetDictionary("workerControl",WorkerControlInfo(target));
  result->SetString("guid","suitecut-worker-"+handle_namespace_+":"+std::to_string(handle));suitecut::DevToolsSetString(result,"preview",suitecut::DevToolsTruncateWTF8(ConsolePreview(value),8192));
  return result;
}
void WorkerConsoleMetadata(Dict info,const std::string& session) {
  const auto mapped=worker_sessions_.find(session);if(mapped==worker_sessions_.end())return;
  worker_targets_.at(mapped->second).observed=true;
  info->SetString("worker",mapped->second);info->SetDictionary("workerInfo",WorkerInfo(worker_targets_.at(mapped->second)));
}
bool WorkerConsolePrepare(int owner,Dict message) {
  const auto operation=operations_.Get(owner);if(!operation)return true;
  const auto action=message->GetString("action").ToString();
  if(action=="dispose") {WorkerConsoleDrop(message->GetInt("handle"));return false;}
  if(action=="list"||action=="waitClose")return false;
  std::vector<int> ids;if(message->GetType("handle")==VTYPE_INT)ids.push_back(message->GetInt("handle"));
  auto arguments=message->GetList("handles");if(arguments)for(size_t i=0;i<arguments->GetSize();++i)if(arguments->GetType(i)==VTYPE_INT)ids.push_back(arguments->GetInt(i));
  const auto needed=std::find_if(ids.begin(),ids.end(),[this](int handle){return worker_console_arguments_.contains(handle);});
  if(needed==ids.end())return false;
  const int handle=*needed;auto& raw=worker_console_arguments_.at(handle);
  const auto worker=message->GetString("worker").ToString();const auto target=worker_targets_.find(worker);
  if(raw.worker!=worker||target==worker_targets_.end()||target->second.generation!=raw.generation||target->second.context!=raw.context){Complete(owner,false,"Native worker console argument context was destroyed or belongs to another worker");return true;}
  if(worker_work_.contains(owner)){Complete(owner,false,"Native worker console preparation already active");return true;}
  const auto argument=raw;raw.claimed=true;
  // A new group per native generation prevents late canceled adoption from
  // freeing a newer wrapper created for the same retained console argument.
  const auto group="suitecut-worker-console-adopt-"+handle_namespace_+"-"+std::to_string(handle)+"-"+std::to_string(operation->generation);
  worker_work_[owner]=WorkerWork{worker,group,operation->generation,target->second.generation,target->second.context,false,false};operation_sessions_[owner]=argument.session;
  auto params=CefDictionaryValue::Create();params->SetInt("executionContextId",argument.context);params->SetString("functionDeclaration","function(value){return {value};}");
  params->SetString("objectGroup",group);params->SetBool("returnByValue",false);params->SetBool("awaitPromise",true);
  auto values=CefListValue::Create();auto value=CefDictionaryValue::Create();
  if(argument.value->HasKey("objectId"))value->SetString("objectId",argument.value->GetString("objectId"));
  else if(argument.value->HasKey("unserializableValue"))value->SetString("unserializableValue",argument.value->GetString("unserializableValue"));
  else if(argument.value->GetString("type")=="string"&&argument.value->GetType("value")==VTYPE_DICTIONARY){std::u16string units;if(!suitecut::DevToolsReadTag(argument.value->GetValue("value"),units)){Complete(owner,false,"Invalid native worker console string sidecar");return true;}params->SetString("functionDeclaration","function(){return {value:"+suitecut::DevToolsQuoteUnits(units)+"};}");}
  else if(argument.value->HasKey("value"))value->SetValue("value",argument.value->GetValue("value"));
  values->SetDictionary(0,value);params->SetList("arguments",values);
  WorkerRequest(owner,"Runtime.callFunctionOn",params,[this,owner,handle,group,argument,message](bool ok,const std::string& json){
    if(!WorkerCurrent(owner))return;
    auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
    const auto retained=worker_handles_.find(handle);
    if(!ok||!remote||result->HasKey("exceptionDetails")||remote->GetType("objectId")!=VTYPE_STRING||retained==worker_handles_.end()){Complete(owner,false,"Native worker console argument adoption failed");return;}
    retained->second.object=remote->GetString("objectId");retained->second.group=group;
    const auto raw=worker_console_arguments_.find(handle);
    if(raw!=worker_console_arguments_.end()){worker_console_bytes_-=raw->second.bytes;worker_console_arguments_.erase(raw);}
    if(argument.value->GetType("objectId")==VTYPE_STRING){auto release=CefDictionaryValue::Create();release->SetString("objectId",argument.value->GetString("objectId"));SendDevTools("Runtime.releaseObject",release,argument.session);}
    worker_work_.erase(owner);WorkerDispatch(owner,message);
  },argument.session);
  return true;
}
void WorkerDispatch(int owner,Dict message) {if(!WorkerValidateControl(owner,message))return;if(!WorkerConsolePrepare(owner,message))WorkerRun(owner,message);}
