// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "utility_browser.h"
class SuiteCutStorageControls : public std::enable_shared_from_this<SuiteCutStorageControls> {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Reply = std::function<void(bool, Dict, const std::string&)>;
  struct Hooks {
    std::function<CefRefPtr<CefRequestContext>(int)> context;
    std::function<bool(int, std::vector<std::string>&)> origins;
    std::function<void(int, int, const std::string&, Dict, Reply)> cookies;
    std::function<bool(Dict, std::string&)> validate_cookies;
    std::function<bool(int,int,Dict,std::string&)> validate_context_cookies;
    std::function<void(int, const std::string&)> record_origin;
    std::function<void(int)> settled;
    std::function<bool(Dict,std::string&)>validate_credentials;
    std::function<void(int,int,const std::string&,Dict,Reply)>credentials;
  };
  SuiteCutStorageControls(std::shared_ptr<SuiteCutUtilityBrowsers> utilities, Hooks hooks);
  void Handle(int owner, const std::string& action, Dict params, Reply reply);
  void Cancel(int owner);
  void ContextClosed(int context);
  void Clear();
  void Diagnostics(Dict output) const;
  size_t Physical()const noexcept{return physical_.size();}
  bool AcceptedWorkReady()const noexcept{return physical_.empty()&&active_.empty()&&closing_.empty()&&uploads_.empty();}
  bool PhysicalOwner(int owner)const{return physical_.contains(owner);}
  size_t PhysicalContext(int context)const{size_t count=0;for(const auto&[owner,scope]:physical_)if(scope==context)++count;return count;}
 private:
  struct Work;
  struct Upload { int context = 0; std::string json; double updated = 0; };
  static double Now();
  bool Current(const std::shared_ptr<Work>& work) const;
  void Cookies(const std::shared_ptr<Work>&work);
  void Next(const std::shared_ptr<Work>& work);
  void ReadyOrigin(const std::shared_ptr<Work>& work, const std::string& error);
  void Finish(const std::shared_ptr<Work>& work, bool success, const std::string& error);
  void WakeUploads();
  void TickUploads();
  std::shared_ptr<SuiteCutUtilityBrowsers> utilities_; Hooks hooks_;
  std::map<int, std::shared_ptr<Work>> active_, closing_;
  std::map<std::string, Upload> uploads_;
  std::map<int,int> physical_;
  size_t upload_bytes_ = 0, retained_bytes_ = 0;
  uint64_t next_upload_ = 0;
  bool upload_timer_ = false, stopping_ = false;
};
