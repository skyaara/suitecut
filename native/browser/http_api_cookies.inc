// SPDX-License-Identifier: MIT
// Independent API jars are actual native stores. Every method enters through
// the same original Engine owner as managed origin storage and private filters.
suitecut::NativeAPICookieJar::Clock SuiteCutHTTPControls::JarClock(double deadline)const{return {hooks_.now,static_cast<double>(std::time(nullptr)),deadline,false};}
bool SuiteCutHTTPControls::ValidateInternalCookies(int owner,int browser_context,CefRefPtr<CefListValue> cookies,double deadline,std::string& error){
  const auto found=std::find_if(contexts_.begin(),contexts_.end(),[browser_context](const auto& item){return item.second.browser_context==browser_context&&item.second.owned;});
  if(found==contexts_.end()||found->second.closing||!found->second.jar){error="Native independent API cookie owner is unavailable";return false;}
  const auto command=scope_commands_.find(owner);if(command==scope_commands_.end()||command->second.context!=found->first){error="Native independent API cookie validation lost its original owner";return false;}
  std::vector<suitecut::NativeAPICookieJar::Input> input;if(!suitecut::NativeAPIJarInputs(cookies,4096,input,error))return false;
  return found->second.jar->ValidateReplacement(input,JarClock(deadline),error);
}
bool SuiteCutHTTPControls::InternalCookieCommand(int owner,int browser_context,const std::string&action,Dict params,double deadline,Reply reply){
  const auto found=std::find_if(contexts_.begin(),contexts_.end(),[browser_context](const auto& item){return item.second.browser_context==browser_context&&item.second.owned;});
  if(found==contexts_.end())return false;
  if(found->second.closing||!found->second.jar){reply(false,nullptr,"Native independent API cookie context closed");return true;}
  const int context=found->first;const auto command=scope_commands_.find(owner);
  if(command==scope_commands_.end()||command->second.context!=context){reply(false,nullptr,"Native independent API cookie command lost its original owner");return true;}
  const auto jar=found->second.jar;const auto clock=JarClock(deadline);std::string error;auto output=CefDictionaryValue::Create();
  if(action=="addCookies"){
    std::vector<suitecut::NativeAPICookieJar::Input> input;
    const bool restore=params->GetBool("storageInternal");
    const bool ok=suitecut::NativeAPIJarInputs(params->GetList("cookies"),restore?4096:1024,input,error)&&(restore?jar->Reset(input,clock,error):jar->Import(input,clock,error));reply(ok,ok?output:nullptr,error);return true;
  }
  if(action=="cookies"){
    std::vector<std::string> urls;if(params->HasKey("urls")){const auto values=params->GetList("urls");if(!values||values->GetSize()>128){reply(false,nullptr,"Cookie URL filters exceed native capacity");return true;}for(size_t i=0;i<values->GetSize();++i){if(values->GetType(i)!=VTYPE_STRING||values->GetString(i).length()>8192){reply(false,nullptr,"Cookie filters require HTTP(S) URLs");return true;}urls.push_back(values->GetString(i).ToString());}}
    std::vector<suitecut::NativeAPICookieJar::Cookie> cookies;const bool ok=jar->Cookies(urls,clock,cookies,error);if(ok)output->SetList("cookies",suitecut::NativeAPIJarCookies(cookies));reply(ok,ok?output:nullptr,error);return true;
  }
  if(action!="clearCookies"){reply(false,nullptr,"Unknown native independent API cookie command");return true;}
  // Storage restore validates all input first, then atomically Reset()s its
  // authoritative jar in the following add step. Never clear before admission.
  if(params->GetBool("storageInternal")){const bool ok=jar->ValidateReplacement({},clock,error);reply(ok,ok?output:nullptr,error);return true;}
  if(params->HasKey("filter")&&params->GetType("filter")!=VTYPE_DICTIONARY){reply(false,nullptr,"Invalid native cookie filter");return true;}
  const auto filter=params->GetDictionary("filter");bool regex=false;if(!suitecut::ValidateCookieFilter(filter,regex,error)){reply(false,nullptr,error);return true;}
  if(!filter||!filter->GetSize()){const bool ok=jar->Clear(clock,error);reply(ok,ok?output:nullptr,error);return true;}
  if(!hooks_.match_cookies){reply(false,nullptr,"Native independent API cookie matcher unavailable");return true;}
  std::vector<suitecut::NativeAPICookieJar::Cookie> cookies;if(!jar->Cookies({},clock,cookies,error)){reply(false,nullptr,error);return true;}
  const auto generation=command->second.generation;const auto weak=weak_from_this();
  hooks_.match_cookies(owner,browser_context,filter,suitecut::NativeAPIJarCookies(cookies,true),deadline,[weak,owner,context,generation,deadline,jar,reply=std::move(reply)](bool ok,Dict result,const std::string&failure){
    const auto self=weak.lock();if(!self)return;const auto current=self->scope_commands_.find(owner);const auto profile=self->contexts_.find(context);
    if(current==self->scope_commands_.end()||current->second.generation!=generation)return;
    if(profile==self->contexts_.end()||profile->second.closing||profile->second.jar!=jar){reply(false,nullptr,"Native independent API cookie scope closed");return;}
    if(!ok){reply(false,nullptr,failure);return;}const auto matches=result?result->GetList("matches"):nullptr;
    if(!matches||matches->GetSize()>4096){reply(false,nullptr,"Invalid native independent cookie match receipt");return;}
    std::vector<uint64_t> identities;for(size_t i=0;i<matches->GetSize();++i){const auto item=matches->GetDictionary(i);if(!item||item->GetType("nativeIdentity")!=VTYPE_STRING){reply(false,nullptr,"Invalid native independent cookie match identity");return;}const auto value=item->GetString("nativeIdentity").ToString();uint64_t id=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),id);if(value.empty()||parsed.ec!=std::errc()||parsed.ptr!=value.data()+value.size()||!id){reply(false,nullptr,"Invalid native independent cookie match identity");return;}identities.push_back(id);}
    std::string error;const bool removed=jar->Remove(identities,self->JarClock(deadline),error);reply(removed,removed?CefDictionaryValue::Create():nullptr,error);
  });return true;
}
