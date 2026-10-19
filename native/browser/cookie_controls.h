// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <memory>
#include <string>
#include "cookie_control_protocol.h"

namespace suitecut {
// UI-thread coordinator. Logical cancellation suppresses publication; a sent
// browser command remains owned until its real acknowledgement or channel close.
class CookieControls : public std::enable_shared_from_this<CookieControls> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  explicit CookieControls(CookieChannel channel):channel_(std::move(channel)){}
  void Handle(int owner,int context,const std::string& action,Dict params,double deadline,Reply reply);
  void MatchOwned(int owner,int context,Dict filter,CefRefPtr<CefListValue> cookies,double deadline,Reply reply);
  void Cancel(int owner);
  void Closed(int context);
  size_t Physical() const{return jobs_.size();}
  bool PhysicalOwner(int owner)const{return jobs_.contains(owner);}
  size_t PhysicalContext(int context)const{size_t count=0;for(const auto&[owner,job]:jobs_)if(job.context==context)++count;return count;}
  static bool Partition(Dict input,std::string& error);
 private:
  struct Job {int context;std::string action;Dict params;double deadline;Reply reply;CefRefPtr<CefListValue> remove;size_t next=0;bool canceled=false;};
  void Send(int owner,CookieMethod method,Dict params);
  void Received(int owner,CookieMethod method,bool ok,Dict result,const std::string& error);
  void FilterNext(int owner);
  void Filtered(int owner,Dict result);
  void DeleteNext(int owner);
  void SelectionReady(int owner);
  void Complete(int owner,bool ok,Dict result,const std::string& error);
  CookieChannel channel_;
  std::map<int,Job> jobs_;
};
}
