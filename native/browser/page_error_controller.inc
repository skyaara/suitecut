// SPDX-License-Identifier: MIT
// Source's independent unsolicited error registry. No ordinary SDK request slot
// is consumed, and logical deadlines cannot recycle physical command admission.
std::unique_ptr<suitecut::PageErrorControls> page_errors_;
suitecut::PageErrorControls& PageErrors(){
  if(!page_errors_)page_errors_=std::make_unique<suitecut::PageErrorControls>(
    [this]{return AllocateDevToolsId();},
    [this](int id,const std::string& method,Dict params,const std::string& session){return !closing_page_&&SubmitDevTools(id,method,params,session);},
    [this](Dict info,Dict details,const std::string& session){
      const auto frame=details->GetDictionary("__suitecutPageErrorFrame");if(frame)info->SetDictionary("frame",frame->Copy(false));
      const auto retained=Json(info);auto value=CefValue::Create();value->SetDictionary(info->Copy(false));std::string json;
      if(!suitecut::DevToolsLosslessJSON(value,json,1048576)){Error("Native page error payload limit exceeded");return;}
      const auto sequence=++event_sequence_;page_error_journal_.Push(sequence,retained);JournalEvent(sequence,"pageerror",retained);
      auto header=Header("pageerror");header->SetDouble("sequence",static_cast<double>(sequence));SendPacket(header,std::vector<uint8_t>(json.begin(),json.end()));
      std::vector<int> waiters;for(const auto& [id,event]:event_waiters_)if(event=="pageerror")waiters.push_back(id);for(const int id:waiters)Complete(id,true,"",json);
    },
    [this](const std::string& error){Error(error.c_str());if(browser_)browser_->GetHost()->CloseBrowser(true);},
    [this](Dict remote){return ConsolePreview(remote);},[]{return Now();});
  return *page_errors_;
}
