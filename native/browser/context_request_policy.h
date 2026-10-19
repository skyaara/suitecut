// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <string>
#include "context_settings.h"
#include "include/cef_request_context_handler.h"
#include "include/cef_request_context.h"

// Actual request-context preference application precedes its public browser.
// All methods and reads are browser UI-thread only. Auth callbacks separately
// retain the immutable profile configuration through the actual CefClient.
class SuiteCutContextRequestPolicy:public CefRequestContextHandler {
 public:
  explicit SuiteCutContextRequestPolicy(std::shared_ptr<const suitecut::ContextSettings>config):config_(std::move(config)){}
  void OnRequestContextInitialized(CefRefPtr<CefRequestContext>context)override{
    if(!config_){error_="Native context request policy is unavailable";initialized_=true;return;}
    if(config_->proxy){
      auto params=CefDictionaryValue::Create();params->SetString("mode","fixed_servers");params->SetString("server",config_->proxy->server);params->SetString("bypass_list",config_->proxy->bypass);
      auto value=CefValue::Create();value->SetDictionary(params);CefString error;
      if(!context->SetPreference("proxy",value,error))error_="Native CEF proxy preference failed: "+error.ToString();
    }
    initialized_=true;
  }
  std::string Error()const{return initialized_?error_:"Native CEF request policy initialization is incomplete";}
 private:
  std::shared_ptr<const suitecut::ContextSettings>config_;bool initialized_=false;std::string error_;
  IMPLEMENT_REFCOUNTING(SuiteCutContextRequestPolicy);
};
