// Included inside Source. Replaces only the real, untouched CEF startup entry.
// The browser still performs a genuine Location.replace navigation; no history
// counter, History API state, or lifecycle notification is manufactured.
bool initial_history_checked_=false,initial_history_check_pending_=false,initial_history_candidate_=false,initial_history_dirty_=false;
int initial_history_entry_=0,initial_execution_context_=0;uint64_t initial_history_navigation_=0;
bool PrepareInitialHistory(){
  if(!initial_empty_document_||initial_history_checked_)return true;
  if(initial_history_check_pending_)return false;
  initial_history_check_pending_=true;
  SettingsReadCommand("Page.getNavigationHistory",nullptr,[this](bool ok,const std::string&json){
    initial_history_check_pending_=false;initial_history_checked_=true;
    const auto value=CefParseJSON(json,JSON_PARSER_RFC);const auto result=value?value->GetDictionary():nullptr;
    const auto entries=result?result->GetList("entries"):nullptr;
    const auto entry=entries&&entries->GetSize()==1?entries->GetDictionary(0):nullptr;
    const auto frame=frame_states_.find(main_frame_id_);
    initial_history_candidate_=ok&&result&&result->GetType("currentIndex")==VTYPE_INT&&result->GetInt("currentIndex")==0&&entry&&entry->GetType("id")==VTYPE_INT&&entry->GetString("url")=="about:blank"&&initial_document_url_=="about:blank"&&initial_document_history_==1&&!initial_history_dirty_&&frame!=frame_states_.end()&&frame->second.url=="about:blank";
    if(frame!=frame_states_.end())initial_execution_context_=frame->second.context;
    if(initial_history_candidate_){initial_history_entry_=entry->GetInt("id");initial_history_navigation_=frame->second.navigation;}
    MaybeReady();
  });
  return false;
}
void DispatchFrameNavigation(int id,Dict params){
  DevTools(id,"Page.navigate",params,[this,id](bool ok,const std::string&json){
    const auto nav=navigations_.find(id);if(nav==navigations_.end())return;
    const auto value=CefParseJSON(json,JSON_PARSER_RFC);const auto result=value?value->GetDictionary():nullptr;
    if(!ok||!result){Complete(id,false,result&&result->GetType("message")==VTYPE_STRING?result->GetString("message").ToString():"Native navigation result unavailable");return;}
    if(result->GetBool("isDownload")){Complete(id,false,"Download is starting");return;}
    nav->second.dispatched=true;
    if(result->GetType("errorText")==VTYPE_STRING&&!result->GetString("errorText").empty()){DownloadNavigationAbort(id,result->GetString("errorText").ToString(),nav->second.url);return;}
    if(!nav->second.download_abort.empty()){DownloadNavigationChanged();return;}
    nav->second.loader=result->GetString("loaderId").ToString();nav->second.same_document=nav->second.loader.empty();CheckNavigations();
  },std::string{});
}
bool ReplaceInitialNavigation(int id,Dict navigation_params){
  const auto nav=navigations_.find(id);if(nav==navigations_.end()||nav->second.frame!=main_frame_id_)return false;
  const bool candidate=initial_history_candidate_;initial_history_candidate_=false;
  // A custom referrer requires Page.navigate's actual native referrer policy.
  // Never silently drop it to obtain startup history parity.
  if(!candidate||navigation_params->HasKey("referrer"))return false;
  // Renderer-initiated top-level data/file navigation is forbidden by Chromium
  // even when Location.replace returns normally. Preserve the privileged native
  // Page.navigate path for those schemes rather than waiting for a nonexistent
  // renderer navigation or silently changing browser security policy.
  CefURLParts parts;
  if(!CefParseURL(nav->second.url,parts))return false;
  const auto scheme=CefString(&parts.scheme).ToString();
  if(scheme!="http"&&scheme!="https")return false;
  const auto frame=frame_states_.find(main_frame_id_);
  if(frame==frame_states_.end()||frame->second.navigation!=initial_history_navigation_||initial_history_dirty_||!browser_||browser_->GetMainFrame()->GetIdentifier()!=initial_cef_frame_)return false;
  DevTools(id,"Page.getNavigationHistory",nullptr,[this,id,navigation_params](bool ok,const std::string&json){
    const auto nav=navigations_.find(id);if(nav==navigations_.end())return;
    const auto value=CefParseJSON(json,JSON_PARSER_RFC);const auto result=value?value->GetDictionary():nullptr;const auto entries=result?result->GetList("entries"):nullptr;
    const auto entry=entries&&entries->GetSize()==1?entries->GetDictionary(0):nullptr;const auto frame=frame_states_.find(nav->second.frame);
    if(!ok||!result||result->GetInt("currentIndex")!=0||!entry||entry->GetType("id")!=VTYPE_INT||entry->GetInt("id")!=initial_history_entry_||entry->GetString("url")!="about:blank"||initial_history_dirty_||frame==frame_states_.end()||frame->second.navigation!=initial_history_navigation_){DispatchFrameNavigation(id,navigation_params);return;}
    nav->second.next=true;nav->second.replace_initial=true;nav->second.previous_loader=frame->second.loader;
    auto params=CefDictionaryValue::Create();params->SetString("frameId",nav->second.frame);params->SetString("worldName","__suitecut_initial_navigation");
    DevTools(id,"Page.createIsolatedWorld",params,[this,id](bool ok,const std::string&json){
      auto nav=navigations_.find(id);if(nav==navigations_.end())return;
      const auto value=CefParseJSON(json,JSON_PARSER_RFC);const auto result=value?value->GetDictionary():nullptr;
      if(!ok||!result||result->GetType("executionContextId")!=VTYPE_INT){Complete(id,false,"Native initial navigation realm is unavailable");return;}
      auto params=CefDictionaryValue::Create();params->SetInt("executionContextId",result->GetInt("executionContextId"));params->SetString("functionDeclaration","function(url){location.replace(url);}");params->SetBool("returnByValue",true);
      auto arguments=CefListValue::Create();auto argument=CefDictionaryValue::Create();argument->SetString("value",nav->second.url);arguments->SetDictionary(0,argument);params->SetList("arguments",arguments);
      DevTools(id,"Runtime.callFunctionOn",params,[this,id](bool ok,const std::string&json){
        auto nav=navigations_.find(id);if(nav==navigations_.end())return;
        const auto value=CefParseJSON(json,JSON_PARSER_RFC);const auto result=value?value->GetDictionary():nullptr;const auto frame=frame_states_.find(nav->second.frame);
        const bool observed_navigation=frame!=frame_states_.end()&&frame->second.navigation>nav->second.after;
        if((!ok||!result||result->HasKey("exceptionDetails"))&&!observed_navigation){Complete(id,false,"Native initial replacement navigation failed");return;}
        nav->second.dispatched=true;CheckNavigations();
      },std::nullopt,true);
    },std::nullopt,true);
  });
  return true;
}
// A BFCache restoration does not emit a second load event. Read the actual
// restored DOM through its native isolated realm to recover existing readiness.
void ObserveRestoredDocument(const std::string&frame_id){
  const auto frame=frame_states_.find(frame_id);if(frame==frame_states_.end())return;
  const auto session=frame->second.session,loader=frame->second.loader;const auto navigation=frame->second.navigation;
  auto params=CefDictionaryValue::Create();params->SetString("frameId",frame_id);params->SetString("worldName","__suitecut_dom__");
  const auto current=[this,frame_id,session,loader,navigation]{const auto frame=frame_states_.find(frame_id);return !closing_page_&&frame!=frame_states_.end()&&frame->second.session==session&&frame->second.loader==loader&&frame->second.navigation==navigation;};
  // SettingsReadCommand is root-scoped; restored OOPIFs require their exact
  // existing native session rather than silently retargeting to the root.
  const auto send=[this,session](const std::string&method,Dict params,suitecut::ContextSettingsPage::Result done){
    const int id=AllocateDevToolsId();if(!id||network_calls_.size()>=1024||closing_page_){done(false,"Native restored document observation capacity exceeded");return;}
    network_calls_.emplace(id,NetworkCall{session,std::move(done),Now()+30000});WakeDeadlines();
    if(!SubmitDevTools(id,method,params,session)){const auto entry=network_calls_.find(id);if(entry!=network_calls_.end()){auto done=std::move(entry->second.callback);network_calls_.erase(entry);done(false,"Native restored document observation dispatch failed");}}
  };
  send("Page.createIsolatedWorld",params,[this,frame_id,current,send](bool ok,const std::string&json){
    if(!current())return;
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
    if(!ok||!result||result->GetType("executionContextId")!=VTYPE_INT)return;
    auto params=CefDictionaryValue::Create();params->SetInt("executionContextId",result->GetInt("executionContextId"));params->SetString("functionDeclaration","function(){return Object.getOwnPropertyDescriptor(Document.prototype,'readyState').get.call(document);}");params->SetBool("returnByValue",true);
    send("Runtime.callFunctionOn",params,[this,frame_id,current](bool ok,const std::string&json){
      if(!current())return;
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;const auto value=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!value||value->GetType("value")!=VTYPE_STRING)return;
      auto& frame=frame_states_.at(frame_id);const auto state=value->GetString("value");
      if(state=="interactive"||state=="complete")frame.milestones.insert("DOMContentLoaded");
      if(state=="complete")frame.milestones.insert("load");
      CheckNavigations();
    });
  });
}

void InitialNavigationEvent(const std::string&method,Dict data,const std::string&session){
  if(method!="Page.frameStartedNavigating"||!session.empty())return;
  const auto frame=data->GetString("frameId").ToString(),loader=data->GetString("loaderId").ToString();
  if(frame!=main_frame_id_||loader.empty())return;
  for(auto&[owner,nav]:navigations_)if(nav.replace_initial&&nav.frame==frame&&nav.loader.empty()&&nav.url==data->GetString("url").ToString())nav.loader=loader;
}
