// SPDX-License-Identifier: MIT
// Include inside Source. Native CEF metadata is journaled before host delivery.
std::deque<Dict> pending_download_events_;
// The initial about:blank placeholder is not a committed author document.
// Existing popup documents retain their own native download events.
bool DownloadProvisionalPopup()const{
  if(!browser_||closing_page_)return false;
  const auto frame=frame_states_.find(main_frame_id_);
  if(frame!=frame_states_.end())return (frame->second.url.empty()||frame->second.url=="about:blank")&&frame->second.navigation==0&&!frame->second.milestones.contains("commit");
  const auto main=browser_->GetMainFrame();if(!main)return false;
  const auto url=main->GetURL().ToString();return url.empty()||url=="about:blank";
}
void FlushDownloadEvents(){
  auto pending=std::move(pending_download_events_);pending_download_events_.clear();
  for(const auto& info:pending)DownloadPublish(info);
}
void DownloadPublish(Dict info){
  if(!ready_){
    if(pending_download_events_.size()>=512){Error("Native startup download event capacity exceeded");return;}
    pending_download_events_.push_back(info->Copy(false));return;
  }
  const bool created=info->GetString("event")=="created";
  const auto json=Json(info);
  if(created){
    const auto sequence=++event_sequence_;
    JournalEvent(sequence,"download",json);
    const auto url=info->GetString("url").ToString(),original=info->GetString("originalURL").ToString();
    for(auto& [owner,wait]:action_navigations_)for(auto& [frame,intent]:wait.intents)
      if(!intent.url.empty()&&(intent.url==url||intent.url==original))intent.canceled=true;
    for(auto& [owner,navigation]:navigations_)
      if(!navigation.wait_only&&(navigation.url==url||navigation.url==original||navigation.download_abort_url==url||navigation.download_abort_url==original))navigation.download_created=true;
    DownloadNavigationChanged();
    CheckActionNavigations();
  }
  auto header=Header("download");header->SetDouble("sequence",static_cast<double>(event_sequence_));
  SendPacket(header,std::vector<uint8_t>(json.begin(),json.end()));
  if(created){
    std::vector<int> owners;for(const auto& [owner,event]:event_waiters_)if(event=="download")owners.push_back(owner);
    for(const int owner:owners)Complete(owner,true,"",json);
  }
}

// A browser abort is classified by the real response and Page.navigate result.
// Retaining an operation here keeps its original deadline and cancellation.
void DownloadNavigationChanged(){
  std::vector<std::pair<int,std::string>> settled;
  for(auto& [owner,nav]:navigations_){
    if(nav.download_abort.empty()||!nav.dispatched)continue;
    const auto classification=engine_.downloads()->NavigationClassification(page_id_,nav.frame==main_frame_id_,nav.download_abort_url,nav.native_download_after);
    if(classification.ambiguous){settled.emplace_back(owner,"Native download navigation request is ambiguous across child frames");continue;}
    if(classification.present&&classification.classification==suitecut::DownloadNavigationRequests::Classification::pending)continue;
    if(classification.present&&classification.classification==suitecut::DownloadNavigationRequests::Classification::download){
      if(nav.download_created)settled.emplace_back(owner,"Download is starting");
    }else settled.emplace_back(owner,nav.download_abort);
  }
  for(const auto& [owner,error]:settled)Complete(owner,false,error);
}
void DownloadNavigationAbort(int owner,const std::string& error,const std::string& url){
  const auto nav=navigations_.find(owner);if(nav==navigations_.end())return;
  if(error.find("ERR_ABORTED")==std::string::npos){Complete(owner,false,error);return;}
  nav->second.download_abort=error;nav->second.download_abort_url=url;
  DownloadNavigationChanged();
}
