// SPDX-License-Identifier: MIT
// Included inside Source after snapshot-reference document/group helpers.
// Native domain commands are controller-owned, not ordinary operation-owned.
std::shared_ptr<suitecut::DOMDomainLeases> native_dom_domains_;
std::shared_ptr<suitecut::LocatorHighlights> native_highlights_;
struct NativeHighlightPhysical {std::string session;double retirement=0;bool closing=false;};
std::map<int,NativeHighlightPhysical> native_highlight_calls_;
std::set<std::string> native_raw_overlay_sessions_;
bool native_highlight_closing_=false;
static size_t& NativeHighlightProcessPhysical(){static size_t value=0;return value;}
bool NativeHighlightSend(const std::string&session,const std::string&method,Dict params,double deadline,suitecut::DOMDomainLeases::Ready reply){
  if(!browser_||closing_page_||native_highlight_closing_||!reply||!std::isfinite(deadline)||deadline<0||(deadline&&Now()>=deadline)||native_highlight_calls_.size()>=64||NativeHighlightProcessPhysical()>=128||(!session.empty()&&!sessions_.contains(session)))return false;
  const auto tool=AllocateDevToolsId();if(!tool)return false;
  native_highlight_calls_.emplace(tool,NativeHighlightPhysical{session,(deadline?deadline:Now())+30000,false});++NativeHighlightProcessPhysical();
  auto callback=[this,tool,session,reply=std::move(reply)](bool ok,const std::string&json){
    const auto found=native_highlight_calls_.find(tool);if(found==native_highlight_calls_.end())return;
    native_highlight_calls_.erase(found);--NativeHighlightProcessPhysical();
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
    const auto error=!ok&&result&&result->GetType("message")==VTYPE_STRING?result->GetString("message").ToString():ok?std::string():"Native highlight Inspector command failed";
    if(!ok&&!session.empty()&&error=="Session with given id not found.")NativeTargetSessionDetached(session);
    reply(ok&&bool(result),error);
  };
  // Actual late ACKs survive caller cancel/retry/expiry; channel teardown below
  // is the only alternative retirement boundary. No author by-value projection.
  devtools_.emplace(tool,DevToolsCall{0,std::move(callback),session,true,65536,true,false});
  if(!SubmitDevTools(tool,method,params,session)){
    const auto found=devtools_.find(tool);if(found!=devtools_.end()){auto actual=std::move(found->second.callback);devtools_.erase(found);actual(false,"{\"message\":\"Native highlight submission was not accepted\"}");}
  }
  WakeDeadlines();return true;
}
std::shared_ptr<suitecut::DOMDomainLeases> NativeDOMDomains(){
  if(native_dom_domains_)return native_dom_domains_;
  suitecut::DOMDomainLeases::Hooks hooks;hooks.now=[]{return Now();};
  hooks.send=[this](const std::string&session,const std::string&method,double deadline,auto reply){return NativeHighlightSend(session,method,nullptr,deadline,std::move(reply));};
  hooks.fatal=[this](const std::string&,const std::string&error){native_highlight_closing_=true;NetworkFatal(error);};
  native_dom_domains_=std::make_shared<suitecut::DOMDomainLeases>(std::move(hooks));return native_dom_domains_;
}
std::shared_ptr<suitecut::LocatorHighlights> NativeHighlights(){
  if(native_highlights_)return native_highlights_;
  suitecut::LocatorHighlights::Hooks hooks;hooks.now=[]{return Now();};hooks.wake=[this]{WakeDeadlines();};hooks.dom=NativeDOMDomains();hooks.name_space=handle_namespace_;
  hooks.current=[this](const SnapshotReferenceDocument&document){const auto actual=ReferenceDocument(document.frame);return !closing_page_&&actual&&*actual==document;};
  hooks.send=[this](const std::string&session,const std::string&method,Dict params,double deadline,auto reply){return NativeHighlightSend(session,method,params,deadline,std::move(reply));};
  hooks.fatal=[this](const std::string&error){native_highlight_closing_=true;NetworkFatal(error);};native_highlights_=std::make_shared<suitecut::LocatorHighlights>(std::move(hooks));return native_highlights_;
}
bool NativeHighlightCurrent(int owner,uint64_t generation,const SnapshotReferenceDocument&document){
  if(!operations_.IsCurrent(owner,generation))return false;
  if(operations_.IsExpired(owner,generation,Now())){Complete(owner,false,"Native operation timed out");return false;}
  const auto actual=ReferenceDocument(document.frame);if(!actual||*actual!=document){Complete(owner,false,"Native highlight document changed");return false;}return true;
}
void NativeHighlightCommand(int owner,Dict message){
  const auto operation=operations_.Get(owner);if(!operation)return;
  if(operations_.IsExpired(owner,operation->generation,Now())){Complete(owner,false,"Native operation timed out");return;}
  for(const char*field:{"nativeQueryNodes","nativeRootHandle","nativeSelectorWaitState","nativeAccessibilitySnapshot","nativeSemanticSnapshotJson","formNode"})if(message->HasKey(field)){Complete(owner,false,"Invalid reserved native highlight field");return;}
  if(message->HasKey("style")){Complete(owner,false,"Native Chromium Overlay highlighting does not support arbitrary CSS style");return;}
  const auto action=message->GetString("action").ToString();
  if(action!="show"&&action!="hide"&&action!="claim"){Complete(owner,false,"Invalid native highlight action");return;}
  if(message->HasKey("token")){
    if((action!="hide"&&action!="claim")||message->GetType("token")!=VTYPE_STRING){Complete(owner,false,"Invalid native highlight disposal identity");return;}
    const auto token=message->GetString("token").ToString();const auto split=token.rfind('_');if(token.size()>90||token.size()<4||token.front()!='h'||split==std::string::npos||split<2||split+1==token.size()||token[split+1]=='0'||!std::all_of(token.begin()+1,token.begin()+split,[](char c){return(c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z');})||!std::all_of(token.begin()+split+1,token.end(),[](char c){return c>='0'&&c<='9';})){Complete(owner,false,"Invalid native highlight disposal identity");return;}
    // A token disposer follows its actual Source-owned session, rather than
    // reacquiring a locator/frame that may now refer to a different document.
    auto controls=NativeHighlights();auto reply=[this,owner,generation=operation->generation](bool ok,const std::string&error,const std::string&json){if(!operations_.IsCurrent(owner,generation))return;if(operations_.IsExpired(owner,generation,Now())){Complete(owner,false,"Native operation timed out");return;}Complete(owner,ok,error,json);};
    if(action=="claim")controls->Claim(token,std::move(reply));else controls->Hide(controls->SessionForToken(token),token,operation->deadline_ms,std::move(reply));return;
  }
  if(action=="claim"){Complete(owner,false,"Native highlight claim requires its actual token");return;}
  if(!SelectorWaits().Admit(owner,operation->generation)){Complete(owner,false,"Native highlight selector physical capacity exceeded");return;}
  if(const auto path=message->GetList("framePath");path&&path->GetSize()){ResolveFramePath(owner,operation->generation,message,0,FrameId(message),[this,owner](Dict resolved){NativeHighlightCommand(owner,resolved);});return;}
  const auto frame=FrameId(message);
  WithFrame(owner,frame,[this,owner,generation=operation->generation,message,frame](int){
    const auto document=ReferenceDocument(frame);if(!document){Complete(owner,false,"Native highlight document identity unavailable");return;}
    if(!NativeHighlightCurrent(owner,generation,*document))return;
    auto reply=[this,owner,generation,document=*document](bool ok,const std::string&error,const std::string&json){if(!NativeHighlightCurrent(owner,generation,document))return;Complete(owner,ok,error,json);};
    if(message->GetString("action")=="hide"){NativeHighlights()->Hide(document->session,"",operations_.Get(owner)->deadline_ms,std::move(reply));return;}
    if(native_raw_overlay_sessions_.contains(document->session)){Complete(owner,false,"Native highlight conflicts with an explicitly enabled raw Overlay owner; disable that raw Overlay first");return;}
    QueryInspect(owner,message,"if(nodes.length>1)throw new Error('Native Chromium Overlay does not support arbitrary multiple locator matches');return nodes[0];",true,false,
      [this,owner,generation,document=*document,reply=std::move(reply)](bool ok,const std::string&json)mutable{
      if(!NativeHighlightCurrent(owner,generation,document))return;
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;const auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote){const auto details=result?result->GetDictionary("exceptionDetails"):nullptr;const auto exception=details?details->GetDictionary("exception"):nullptr;const auto error=exception?suitecut::DevToolsString(exception->GetValue("description")):"Native highlight selection failed";Complete(owner,false,error.substr(0,4096));return;}
      const auto operation=operations_.Get(owner);if(!operation)return;
      if(remote->GetString("type")=="undefined"){NativeHighlights()->Empty(document.session,operation->deadline_ms,std::move(reply));return;}
      if(remote->GetString("subtype")!="node"||remote->GetType("objectId")!=VTYPE_STRING){Complete(owner,false,"Native highlight selected value is not an Element");return;}
      if(!ReferenceTemporaryGroups(owner)){Complete(owner,false,"Native highlight temporary group ownership unavailable");return;}
      auto params=CefDictionaryValue::Create();params->SetString("objectId",remote->GetString("objectId"));
      DevTools(owner,"DOM.describeNode",params,[this,owner,generation,document,reply=std::move(reply)](bool ok,const std::string&json)mutable{
        if(!NativeHighlightCurrent(owner,generation,document))return;
        const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;const auto node=result?result->GetDictionary("node"):nullptr;
        if(!ok||!node||node->GetType("backendNodeId")!=VTYPE_INT||node->GetInt("backendNodeId")<1||node->GetInt("nodeType")!=1){Complete(owner,false,"Native highlight actual Element backend identity unavailable");return;}
        const auto operation=operations_.Get(owner);if(!operation)return;NativeHighlights()->Show(owner,generation,document,node->GetInt("backendNodeId"),operation->deadline_ms,std::move(reply));
      },document.session,false,262144,true);
    });
  },true);
}
// Coverage retains its existing physical actual-ACK contract and original
// publication deadlines. Shared DOM release can keep another genuine consumer.
bool NativeCoverageDOM(const std::string&session,const std::string&method,double deadline,suitecut::CoverageControls::ProtocolReply reply){
  if(method!="DOM.enable"&&method!="DOM.disable")return false;
  auto ready=[reply=std::move(reply)](bool ok,const std::string&error){reply(ok,nullptr,error);};
  if(method=="DOM.enable")NativeDOMDomains()->Acquire(session,"coverage",deadline,std::move(ready));
  else NativeDOMDomains()->Release(session,"coverage",std::move(ready));
  return true;
}
bool NativeRawDOM(int owner,uint64_t generation,const std::string&session,const std::string&method,double deadline){
  if(method!="DOM.enable"&&method!="DOM.disable")return false;
  auto domains=NativeDOMDomains();auto ready=[this,owner,generation](bool ok,const std::string&error){if(!operations_.IsCurrent(owner,generation))return;if(operations_.IsExpired(owner,generation,Now())){Complete(owner,false,"Native operation timed out");return;}Complete(owner,ok,error,ok?"{}":"");};
  if(method=="DOM.enable"){
    if(domains->OwnerReady(session,"raw")){ready(true,"");return true;}
    domains->Acquire(session,"raw",deadline,std::move(ready));return true;
  }
  // A raw disable never receives a fabricated positive ACK while another
  // native owner keeps the actual domain enabled.
  if(domains->OtherOwners(session,"raw")){ready(false,"Raw DOM.disable conflicts with native DOM-domain ownership");return true;}
  if(domains->HasOwner(session,"raw"))domains->Release(session,"raw",std::move(ready));
  else if(domains->Owned(session))ready(false,"Raw DOM.disable conflicts with pending actual domain work");
  else domains->DisableUnused(session,deadline,std::move(ready));
  return true;
}
bool NativeRawOverlayConflict(const std::string&session,const std::string&method){
  if(!method.starts_with("Overlay.")||method.starts_with("Overlay.get"))return false;
  return native_highlights_&&native_highlights_->SessionOwned(session);
}
void NativeHighlightTick(){
  if(native_highlight_closing_)return;
  if(native_highlights_)native_highlights_->Tick();
  for(auto&[id,call]:native_highlight_calls_)if(!call.closing&&Now()>=call.retirement){(void)id;call.closing=true;native_highlight_closing_=true;NetworkFatal("Native highlight/DOM domain command exceeded physical settlement grace");}
}
bool NativeHighlightNeedsTick()const{return !native_highlight_closing_&&((native_highlights_&&native_highlights_->NeedsTick())||std::any_of(native_highlight_calls_.begin(),native_highlight_calls_.end(),[](const auto&entry){return !entry.second.closing;}));}
void NativeHighlightsFrameChanged(const std::string&frame){if(native_highlights_)native_highlights_->InvalidateFrame(frame);}
void NativeHighlightDetached(const std::string&session){
  if(native_highlights_)native_highlights_->Detached(session);
  if(native_dom_domains_)native_dom_domains_->Destroyed(session);
  native_raw_overlay_sessions_.erase(session);
  for(auto it=native_highlight_calls_.begin();it!=native_highlight_calls_.end();){if(it->second.session==session){devtools_.erase(it->first);--NativeHighlightProcessPhysical();it=native_highlight_calls_.erase(it);}else ++it;}
}
void NativeHighlightClosed(){
  native_highlight_closing_=true;if(native_highlights_)native_highlights_->Closed();if(native_dom_domains_)native_dom_domains_->Closed();native_raw_overlay_sessions_.clear();
  for(const auto&[id,call]:native_highlight_calls_){(void)call;devtools_.erase(id);--NativeHighlightProcessPhysical();}native_highlight_calls_.clear();
}
