// SPDX-License-Identifier: MIT
// Included inside Source. All private commands retain physical ownership until
// the actual CDP acknowledgment, regardless of logical context cancellation.
std::shared_ptr<suitecut::CredentialPage> credentials_;
void CredentialPrepare() {
  const auto store=engine_.contexts()->CredentialReadyStore();
  if(!store->Register(browser_->GetIdentifier(),browser_->GetHost()->GetRequestContext())){
    Error("Native credential readiness capacity exceeded");browser_->GetHost()->CloseBrowser(true);return;
  }
  credentials_=std::make_shared<suitecut::CredentialPage>(
    [this](const std::string&session,const std::string&method,Dict params,suitecut::CredentialPage::Result done){
      const int id=AllocateDevToolsId();
      if(!id||network_calls_.size()>=1024){done(false,"Native credential physical command capacity exceeded");return;}
      network_calls_.emplace(id,NetworkCall{session,[this,done=std::move(done)](bool ok,const std::string&json){
        if(!ok&&json=="Native network command deadline expired"){
          if(browser_){engine_.contexts()->CredentialReadyStore()->Ready(browser_->GetIdentifier(),false);browser_->GetHost()->CloseBrowser(true);}
        }
        done(ok,json);
      },Now()+30000});WakeDeadlines();
      if(!SubmitDevTools(id,method,params,session)){
        const auto call=network_calls_.find(id);if(call!=network_calls_.end()){auto callback=std::move(call->second.callback);network_calls_.erase(call);callback(false,"Native credential physical dispatch failed");}
      }
    },[this,store](bool ready){
      if(!browser_)return;
      if(ready)store->Ready(browser_->GetIdentifier(),true);
      else if(!store->Register(browser_->GetIdentifier(),browser_->GetHost()->GetRequestContext()))browser_->GetHost()->CloseBrowser(true);
    });
}
void CredentialStart(){
  if(!credentials_||!browser_)return;
  credentials_->SessionAttached("",engine_.contexts()->CredentialConfig(page_id_),[this](bool ok,const std::string&error){
    if(!ok&&browser_){engine_.contexts()->CredentialReadyStore()->Ready(browser_->GetIdentifier(),false);Error(error.c_str());browser_->GetHost()->CloseBrowser(true);}
  });
}
void CredentialClose(){
  if(browser_)engine_.contexts()->CredentialReadyStore()->Drop(browser_->GetIdentifier());
  if(credentials_)credentials_->Clear();
  credentials_.reset();
}
bool CredentialEvent(const std::string&method,Dict data,const std::string&session){
  if(!method.starts_with("WebAuthn."))return false;
  if(!credentials_||!session.empty()||!data||data->GetString("authenticatorId")!=credentials_->Authenticator(""))return true;
  if(method!="WebAuthn.credentialAdded"&&method!="WebAuthn.credentialAsserted")return true;
  const auto value=data->GetDictionary("credential");if(!value){Error("Native WebAuthn credential event missing credential");return true;}
  suitecut::VirtualCredential credential;std::string error;
  auto decode=[&](const char*name,size_t limit,std::string&output){
    if(value->GetType(name)!=VTYPE_STRING)return false;
    auto encoded=value->GetString(name).ToString();for(auto&ch:encoded){if(ch=='+')ch='-';else if(ch=='/')ch='_';}while(!encoded.empty()&&encoded.back()=='=')encoded.pop_back();
    std::vector<unsigned char>bytes;if(!suitecut::CredentialDecodeBase64URL(encoded,limit,bytes))return false;
    output=suitecut::CredentialBase64URL(bytes.data(),bytes.size());return true;
  };
  const auto type=value->GetType("signCount");const double count=type==VTYPE_INT?value->GetInt("signCount"):type==VTYPE_DOUBLE?value->GetDouble("signCount"):-1;
  if(value->GetType("rpId")!=VTYPE_STRING||!decode("credentialId",1024,credential.id)||!decode("userHandle",64,credential.user_handle)||!decode("privateKey",16384,credential.private_key)||!std::isfinite(count)||count<0||count>4294967295.0||std::floor(count)!=count){Error("Invalid native WebAuthn credential event");if(browser_)browser_->GetHost()->CloseBrowser(true);return true;}
  credential.rp_id=value->GetString("rpId");credential.sign_count=static_cast<uint32_t>(count);
  if(!suitecut::VirtualCredentialPublicKey(credential.private_key,credential.public_key,error)){
    Error(error.c_str());if(browser_)browser_->GetHost()->CloseBrowser(true);return true;
  }
  if(method=="WebAuthn.credentialAdded"&&credentials_->OwnsImportEvent(session,credential))return true;
  if(credentials_->Busy()){
    Error("Native WebAuthn ceremony overlapped credential synchronization; concurrent ceremonies require backend support");
    if(browser_)browser_->GetHost()->CloseBrowser(true);
    return true;
  }
  if(!engine_.contexts()->CredentialObserved(page_id_,credential,error)){
    Error(error.c_str());if(browser_)browser_->GetHost()->CloseBrowser(true);
  }
  return true;
}
