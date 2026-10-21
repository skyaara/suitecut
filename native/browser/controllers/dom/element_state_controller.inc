// SPDX-License-Identifier: MIT
std::unique_ptr<suitecut::ElementStateControls> element_states_;
suitecut::ElementStateControls& ElementStates(){
  if(!element_states_)element_states_=std::make_unique<suitecut::ElementStateControls>(
    "__suitecut_element_state_"+handle_namespace_,[this]{return AllocateDevToolsId();},
    [this](int id,const std::string& method,Dict params,const std::string& session){return !closing_page_&&SubmitDevTools(id,method,params,session);},
    [this](const std::string& error){Error(error.c_str());if(browser_)browser_->GetHost()->CloseBrowser(true);},[]{return Now();});
  return *element_states_;
}
// False means either the normal native frame waiter was scheduled or the exact
// original operation was rejected. The retained handle selects the frame epoch.
bool PrepareStableElementState(int id,uint64_t generation,Dict message){
  if(message->GetType("nativeHandle")!=VTYPE_INT){Complete(id,false,"Stable element state requires a retained native handle");return false;}
  const auto handle=handles_.find(message->GetInt("nativeHandle"));
  if(handle==handles_.end()){Complete(id,false,MissingHandle(message->GetInt("nativeHandle")));return false;}
  if(handle->second.node_id.empty()){Complete(id,false,"Native handle is not a DOM element");return false;}
  const auto frame=frame_states_.find(handle->second.frame_id);
  if(frame==frame_states_.end()){Complete(id,false,"Native element-state frame detached");return false;}
  if(!frame->second.utility_context){WithFrame(id,frame->first,[this,id,generation,message](int){QueryEvaluate(id,generation,message);},true);return false;}
  const auto owner=operations_.Get(id);if(!owner||owner->generation!=generation)return false;
  if(!ElementStates().Admit(id,generation,frame->first,frame->second.utility_context,frame->second.session,owner->deadline_ms)){
    Complete(id,false,"Native element-state capacity exceeded or context changed");return false;
  }
  return true;
}
