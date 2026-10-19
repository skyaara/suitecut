// SPDX-License-Identifier: MIT
// Included inside Source. Ordinary handle creation and cleanup have distinct
// logical admission; real cleanup commands are capped independently of both.
std::unique_ptr<suitecut::HandleDisposalControls> handle_disposals_;
void DisposeNativeHandles(int id,Dict message){
  const auto operation=operations_.Get(id);if(!operation)return;
  std::vector<int> ids;
  if(message->GetString("action")=="disposeMany"){
    if(message->GetType("handles")!=VTYPE_LIST){Complete(id,false,"Invalid native handle disposal batch");return;}
    const auto list=message->GetList("handles");
    if(!list||!list->GetSize()||list->GetSize()>1024){Complete(id,false,"Invalid native handle disposal batch");return;}
    std::set<int> unique;
    for(size_t index=0;index<list->GetSize();++index){
      if(list->GetType(index)!=VTYPE_INT||list->GetInt(index)<=0||!unique.insert(list->GetInt(index)).second){Complete(id,false,"Invalid native handle disposal batch");return;}
      ids.push_back(list->GetInt(index));
    }
  }else{
    const int handle=message->GetInt("handle");DropConsoleArgument(handle);
    if(!handles_.contains(handle)){Complete(id,true);return;}ids.push_back(handle);
  }
  std::vector<suitecut::HandleDisposalControls::Handle> handles;
  for(const int handle:ids){const auto found=handles_.find(handle);if(found==handles_.end()){Complete(id,false,MissingHandle(handle));return;}handles.push_back({handle,found->second.group,found->second.session});}
  if(!handle_disposals_)handle_disposals_=std::make_unique<suitecut::HandleDisposalControls>(
    [this](int owner,const auto& handle,suitecut::HandleDisposalControls::Ack ack){
      const int tool=AllocateDevToolsId();
      if(!tool||!browser_||closing_page_){ack(false,!browser_||closing_page_,"Native handle disposal channel unavailable");return;}
      if(!handle.session.empty()&&!sessions_.contains(handle.session)){ack(false,true,"Native handle disposal session was destroyed");return;}
      auto params=CefDictionaryValue::Create();params->SetString("objectGroup",handle.group);
      const auto session=handle.session;
      devtools_.emplace(tool,DevToolsCall{owner,[this,session,ack=std::move(ack)](bool ok,const std::string&)mutable{
        ack(ok,!session.empty()&&!sessions_.contains(session),ok?"":"Native handle disposal failed");
      },session,true,65536,true,false});
      if(!SubmitDevTools(tool,"Runtime.releaseObjectGroup",params,session)){
        const auto found=devtools_.find(tool);
        if(found!=devtools_.end()){auto callback=std::move(found->second.callback);devtools_.erase(found);callback(false,"Native handle disposal dispatch failed");}
      }
    },
    [this](const auto& handle){const auto found=handles_.find(handle.id);if(found!=handles_.end()&&found->second.group==handle.group)handles_.erase(found);},
    [this](int owner,uint64_t generation,bool ok,const std::string& error){if(operations_.IsCurrent(owner,generation))Complete(owner,ok,error);},
    [this](const std::string& error){Error(error.c_str());if(browser_&&!closing_page_)browser_->GetHost()->CloseBrowser(true);});
  if(!handle_disposals_->Begin(id,operation->generation,std::move(handles),Now())){Complete(id,false,"Native handle disposal capacity exceeded");return;}
  WakeDeadlines();
}
