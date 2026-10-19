// SPDX-License-Identifier: MIT
#pragma once
#include "context_settings.h"
#include "network_authentication.h"
#include "include/cef_auth_callback.h"
namespace suitecut {
inline NativeNetworkAuthentication::Provider NativeContextAuthentication(std::shared_ptr<const ContextSettings> config,NativeContextAuthenticationControls::Snapshot snapshot={}) {
  if(!config)return [](NativeNetworkAuthentication::Dict){auto response=CefDictionaryValue::Create();response->SetString("response","CancelAuth");return response;};
  auto desired=snapshot?snapshot->Get():std::make_shared<const NativeContextCredentials>(config->http_credentials_all.empty()?(config->http_credentials?NativeContextCredentials{*config->http_credentials}:NativeContextCredentials{}):config->http_credentials_all);
  // An empty credential set is an explicit native denial policy. Disabling
  // Fetch here delegates challenges to Chromium's interactive auth UI, which
  // has no valid modal host in our windowless browser.
  if(!desired)return {};
  return [config=std::move(config),snapshot=std::move(snapshot),desired=std::move(desired)](NativeNetworkAuthentication::Dict input){
    auto response=CefDictionaryValue::Create();response->SetString("response","CancelAuth");
    const auto credentials=snapshot?snapshot->Get():desired;if(!credentials)return response;
    const auto challenge=input?input->GetDictionary("authChallenge"):nullptr;
    const auto request=input?input->GetDictionary("request"):nullptr;if(!challenge||!request)return response;
    if(challenge->GetString("source")=="Proxy"&&config->proxy&&config->proxy->authentication){
      response->SetString("response","ProvideCredentials");response->SetString("username",config->proxy->username);response->SetString("password",config->proxy->password);
    }else if(challenge->GetString("source")=="Server"){
      const auto origin=ContextSettings::CanonicalOrigin(request->GetString("url"));const NativeContextCredential*chosen=nullptr;
      for(const auto&item:*credentials)if(!item.origin.empty()&&item.origin==origin){chosen=&item;break;}
      if(!chosen)for(const auto&item:*credentials)if(item.origin.empty()){chosen=&item;break;}
      if(chosen){response->SetString("response","ProvideCredentials");response->SetString("username",chosen->username);response->SetString("password",chosen->password);}
    }
    return response;
  };
}
inline bool NativeContextAuthenticationCallback(std::shared_ptr<const ContextSettings> config,
    NativeContextAuthenticationControls::Snapshot snapshot,bool open,const CefString&url,
    bool proxy,CefRefPtr<CefAuthCallback> callback){
  if(!callback)return false;
  const auto credentials=snapshot?snapshot->Get():(config?std::make_shared<const NativeContextCredentials>(config->http_credentials_all.empty()&&config->http_credentials?NativeContextCredentials{*config->http_credentials}:config->http_credentials_all):nullptr);
  if(open&&config&&credentials){
    if(proxy&&config->proxy&&config->proxy->authentication){callback->Continue(config->proxy->username,config->proxy->password);return true;}
    if(!proxy){
      const auto origin=ContextSettings::CanonicalOrigin(url);const NativeContextCredential*chosen=nullptr;
      for(const auto&item:*credentials)if(!item.origin.empty()&&item.origin==origin){chosen=&item;break;}
      if(!chosen)for(const auto&item:*credentials)if(item.origin.empty()){chosen=&item;break;}
      if(chosen){callback->Continue(chosen->username,chosen->password);return true;}
    }
  }
  callback->Cancel();return true;
}

}
