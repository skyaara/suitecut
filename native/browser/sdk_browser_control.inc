// SPDX-License-Identifier: MIT
// Included inside Source. These SDK aliases use existing native CEF ownership,
// download receipts, artifact media propagation and bounded HAR body readers.
bool sdk_har_owned_=false,sdk_har_sealed_=false;
std::string sdk_har_content_="embed";

bool SDKBrowserFields(int id,Dict message,std::set<std::string> allowed) {
  allowed.insert({"version","id","command","pageId","timeoutMs","action","api","frame","framePath","locatorHandlerOrigin"});
  CefDictionaryValue::KeyList names;message->GetKeys(names);
  for(const auto& name:names) if(!allowed.contains(name.ToString())) {
    Complete(id,false,"Unsupported native SDK browser control field");return false;
  }
  if(FrameId(message)!=main_frame_id_ || (message->HasKey("framePath")&&
      (message->GetType("framePath")!=VTYPE_LIST||message->GetList("framePath")->GetSize()!=0))) {
    Complete(id,false,"Native SDK browser controls require the main page");return false;
  }
  return true;
}
bool SDKBrowserNumber(Dict message,const char* name,double& output,bool optional=false) {
  if(!message->HasKey(name))return optional;
  if(message->GetType(name)!=VTYPE_INT&&message->GetType(name)!=VTYPE_DOUBLE)return false;
  output=Number(message,name);return std::isfinite(output);
}
bool SDKBrowserText(Dict message,const char* name,size_t limit,bool required=true) {
  if(!message->HasKey(name))return !required;
  if(message->GetType(name)!=VTYPE_STRING)return false;
  const auto text=message->GetString(name).ToString();
  return text.size()<=limit&&std::none_of(text.begin(),text.end(),[](unsigned char c){return c<0x20||c==0x7f;});
}
Dict SDKBrowserDownloadInfo(Dict input) {
  auto result=input->Copy(false);result->SetString("downloadId",result->GetString("id"));
  const auto state=result->GetString("state").ToString();
  if(state=="complete")result->SetString("state","completed");
  else if(state=="failed"&&result->GetString("failure")=="canceled")result->SetString("state","canceled");
  return result;
}
void SDKBrowserHARWait(int id,uint64_t generation) {
  if(!operations_.IsCurrent(id,generation))return;
  if(closing_page_||!browser_||!sdk_har_owned_) {Complete(id,false,"Native in-memory HAR owner is closed");return;}
  if(operations_.IsExpired(id,generation,Now())) {Complete(id,false,"Native HAR original deadline exceeded");return;}
  const int context=engine_.contexts()->ContextId(page_id_);
  if(!engine_.network_archive().Ready(context)) {
    CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation]{self->SDKBrowserHARWait(id,generation);},
      suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()),10);return;
  }
  auto result=engine_.network_archive().Export(context);
  if(!result) {Complete(id,false,"Native HAR capture is unavailable");return;}
  // The native recorder admits one consumer per context. Its actual `_owner`
  // identity lets the Page facade publish only records observed by this Source.
  // Do not infer ownership from URL, timing, worker proximity or frame guesses.
  auto log=result->GetDictionary("log");auto entries=log->GetList("entries");
  auto owned_entries=CefListValue::Create();std::set<std::string> page_references;
  const auto owner="page:"+std::to_string(page_id_);
  for(size_t i=0;i<entries->GetSize();++i) {
    auto entry=entries->GetDictionary(i);
    if(!entry||entry->GetString("_owner")!=owner)continue;
    if(entry->GetType("pageref")==VTYPE_STRING)page_references.insert(entry->GetString("pageref"));
    owned_entries->SetDictionary(owned_entries->GetSize(),entry->Copy(false));
  }
  log->SetList("entries",owned_entries);
  auto pages=log->GetList("pages"),owned_pages=CefListValue::Create();
  if(pages)for(size_t i=0;i<pages->GetSize();++i) {
    auto page=pages->GetDictionary(i);
    if(page&&page_references.contains(page->GetString("id")))owned_pages->SetDictionary(owned_pages->GetSize(),page->Copy(false));
  }
  log->SetList("pages",owned_pages);
  if(sdk_har_content_=="omit") {
    auto entries=result->GetDictionary("log")->GetList("entries");
    for(size_t i=0;i<entries->GetSize();++i) {
      auto entry=entries->GetDictionary(i);auto response=entry?entry->GetDictionary("response"):nullptr;
      auto content=response?response->GetDictionary("content"):nullptr;
      if(content)for(const char* key:{"text","encoding","_file","_sha1"})content->Remove(key);
    }
  }
  auto limits=CefDictionaryValue::Create();limits->SetString("scope","page");limits->SetString("content",sdk_har_content_);
  limits->SetString("admissionScope","context");limits->SetBool("oneCapturePerContext",true);
  limits->SetInt("responseBodyLimitBytes",2097152);limits->SetInt("retainedResponseBodyBudgetBytes",16777216);
  limits->SetString("budgetScope","engine");result->SetDictionary("_suitecut",limits);
  Complete(id,true,"",Json(result));
}
void SDKBrowserControlClosed() {
  if(sdk_har_owned_)engine_.network_archive().Stop(engine_.contexts()->ContextId(page_id_));
  sdk_har_owned_=false;sdk_har_sealed_=false;
}
bool SDKBrowserControl(int id,uint64_t generation,Dict message,const std::string& command,double timeout) {
  const auto action=message->GetString("action").ToString();
  const bool har=command=="network"&&(action=="harStart"||action=="harStop"||action=="exportHar");
  if(command!="download"&&command!="emulation"&&!har)return false;
  if(operations_.IsExpired(id,generation,Now())) {Complete(id,false,"Native SDK browser control deadline exceeded");return true;}
  const int context=engine_.contexts()->ContextId(page_id_);
  if(context<=0||!engine_.contexts()->RequestContextForContext(context)) {Complete(id,false,"Native SDK browser context is closed");return true;}
  if(command=="download") {
    if(!SDKBrowserFields(id,message,{"path","downloadId"}))return true;
    if(action=="setDirectory"||action=="configure") {
      if(message->GetType("path")!=VTYPE_STRING) {Complete(id,false,"Native download directory requires a path");return true;}
      std::string path,error;
      if(!engine_.downloads()->SDKSetDirectory(context,message->GetString("path"),path,error)) {Complete(id,false,error);return true;}
      auto result=CefDictionaryValue::Create();result->SetString("path",path);Complete(id,true,"",Json(result));return true;
    }
    if(action=="list") {
      auto list=engine_.downloads()->SDKList(page_id_,context);
      for(size_t i=0;i<list->GetSize();++i)list->SetDictionary(i,SDKBrowserDownloadInfo(list->GetDictionary(i)));
      auto value=CefValue::Create();value->SetList(list);Complete(id,true,"",CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString());return true;
    }
    if(action!="status"&&action!="path"&&action!="saveAs"&&action!="cancel") {Complete(id,false,"Unsupported native SDK download action");return true;}
    if(message->GetType("downloadId")!=VTYPE_STRING||
        !engine_.downloads()->SDKOwns(message->GetString("downloadId"),page_id_,context)) {
      Complete(id,false,"Native download is unavailable to this page and context");return true;
    }
    engine_.downloads()->Handle(id,message,operations_.Get(id)->deadline_ms,
      [self=CefRefPtr<Source>(this),id,generation,action](bool ok,Dict result,const std::string& error) {
        if(!self->operations_.IsCurrent(id,generation))return;
        if(ok&&result&&action=="status")result=self->SDKBrowserDownloadInfo(result);
        self->Complete(id,ok,error,result?Json(result):"{}");
      });return true;
  }
  if(har) {
    if(!SDKBrowserFields(id,message,action=="harStart"?std::set<std::string>{"content"}:std::set<std::string>{}))return true;
    if(action=="harStart") {
      const auto content=message->HasKey("content")?message->GetString("content").ToString():"embed";
      if((message->HasKey("content")&&message->GetType("content")!=VTYPE_STRING)||(content!="embed"&&content!="omit")) {
        Complete(id,false,"Native HAR supports content embed or omit");return true;
      }
      if(sdk_har_owned_) {
        if(!sdk_har_sealed_) {Complete(id,false,"Stop the existing native HAR capture before restarting");return true;}
        engine_.network_archive().Stop(context);sdk_har_owned_=false;
      }
      if(!engine_.network_archive().Start(context)) {Complete(id,false,"Native HAR context already has an owner or exceeds capture capacity");return true;}
      sdk_har_owned_=true;sdk_har_sealed_=false;sdk_har_content_=content;Complete(id,true,"","{}");return true;
    }
    if(!sdk_har_owned_) {Complete(id,false,"Start native HAR capture before exporting it");return true;}
    if(action=="harStop") {engine_.network_archive().SealRecording(context);sdk_har_sealed_=true;}
    SDKBrowserHARWait(id,generation);return true;
  }
  if(action=="media") {
    if(!SDKBrowserFields(id,message,{"media","colorScheme","reducedMotion","forcedColors","contrast"}))return true;
    auto config=message->Copy(false);config->SetString("action","emulateMedia");config->SetString("command","artifact");
    DispatchCommand(id,config,"artifact",timeout);return true;
  }
  auto params=CefDictionaryValue::Create();std::string method;
  if(action=="geolocation") {
    if(!SDKBrowserFields(id,message,{"latitude","longitude","accuracy"}))return true;
    double latitude=0,longitude=0,accuracy=0;
    if(!SDKBrowserNumber(message,"latitude",latitude)||!SDKBrowserNumber(message,"longitude",longitude)||
        !SDKBrowserNumber(message,"accuracy",accuracy,true)||latitude < -90||latitude>90||longitude < -180||longitude>180||accuracy<0) {
      Complete(id,false,"Invalid native emulated geolocation");return true;
    }
    params->SetDouble("latitude",latitude);params->SetDouble("longitude",longitude);params->SetDouble("accuracy",accuracy);
    method="Emulation.setGeolocationOverride";
  } else if(action=="clearGeolocation") {
    if(!SDKBrowserFields(id,message,{}))return true;
    method="Emulation.clearGeolocationOverride";
  } else if(action=="timezone") {
    if(!SDKBrowserFields(id,message,{"timezoneId"}))return true;
    if(!SDKBrowserText(message,"timezoneId",256)) {Complete(id,false,"Invalid native emulated timezone");return true;}
    params->SetString("timezoneId",message->GetString("timezoneId"));method="Emulation.setTimezoneOverride";
  } else if(action=="userAgent") {
    if(!SDKBrowserFields(id,message,{"userAgent","acceptLanguage","platform"}))return true;
    if(!SDKBrowserText(message,"userAgent",4096)||!SDKBrowserText(message,"acceptLanguage",4096,false)||!SDKBrowserText(message,"platform",256,false)) {
      Complete(id,false,"Invalid native emulated user agent");return true;
    }
    for(const char* key:{"userAgent","acceptLanguage","platform"})if(message->HasKey(key))params->SetString(key,message->GetString(key));
    method="Emulation.setUserAgentOverride";
  } else {Complete(id,false,"Unsupported native SDK emulation action");return true;}
  DevTools(id,method,params,[self=CefRefPtr<Source>(this),id,generation](bool ok,const std::string&) {
    if(!self->operations_.IsCurrent(id,generation))return;
    self->Complete(id,ok,ok?"":"Native emulation command failed",ok?"{}":"");
  });return true;
}
