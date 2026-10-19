// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
namespace suitecut {
// Physical debugger closure is posted outside the observer. This state remains
// pending until OnBeforeClose confirms actual owner teardown.
class NativeNetworkFatalTeardown {
 public:
  using Post=std::function<bool(std::function<void()>)>;
  NativeNetworkFatalTeardown(Post post,std::function<void(const std::string&)>report,std::function<void()>close)
      :post_(std::move(post)),report_(std::move(report)),close_(std::move(close)){}
  ~NativeNetworkFatalTeardown(){Closed();}
  void Fail(const std::string&error){
    if(closed_||pending_)return;
    pending_=true;error_=error.substr(0,8192);Schedule();
  }
  void Tick(){if(pending_&&!requested_&&!scheduled_)Schedule();}
  bool pending()const{return pending_;}bool requested()const{return requested_;}
  void Closed(){closed_=true;pending_=false;*alive_=false;}
 private:
  void Schedule(){
    scheduled_=true;
    if(!post_([this,alive=alive_]{if(!*alive)return;scheduled_=false;if(closed_||requested_)return;requested_=true;auto report=report_;auto close=close_;auto error=error_;report(error);close();}))scheduled_=false;
  }
  Post post_;std::function<void(const std::string&)>report_;std::function<void()>close_;
  bool pending_=false,requested_=false,scheduled_=false,closed_=false;
  std::string error_;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
};
}
