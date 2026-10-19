// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <string>

// One genuine physical dispatch at a time. Cancellation retires a logical
// owner immediately, but retains its physical callback until Chromium replies.
class SuiteCutTouchControls {
 public:
  enum class Kind { Configure, Start, End, Cancel };
  struct Packet { Kind kind; bool enabled = false; double x = 0, y = 0; };
  using Reply = std::function<void(bool, const std::string&)>;
  using Send = std::function<void(Packet, Reply)>;
  struct Hooks { Send send; std::function<void(int,bool,const std::string&)> complete; };
  explicit SuiteCutTouchControls(Hooks hooks):hooks_(std::move(hooks)){}
  void Configure(int owner,bool enabled){ Enqueue(owner,true,enabled,0,0); }
  void ConfigureBootstrap(bool enabled,Reply done){Enqueue(0,true,enabled,0,0,std::move(done));}
  void Tap(int owner,double x,double y){
    if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>1000000||std::abs(y)>1000000){
      hooks_.complete(owner,false,"Invalid native touch coordinate");return;
    }
    Enqueue(owner,false,false,x,y);
  }
  void Cancel(int owner){
    queue_.erase(std::remove_if(queue_.begin(),queue_.end(),[owner](const auto& w){return w->owner==owner;}),queue_.end());
    if(active_&&active_->owner==owner)active_->owner=0;
  }
  void Close(){
    closed_=true;
    auto pending=std::move(queue_);queue_.clear();
    for(auto& work:pending){if(work->done){auto done=std::move(work->done);done(false,"Native touch page is closed");}if(work->owner)hooks_.complete(work->owner,false,"Native touch page is closed");}
    if(active_){if(active_->done){auto done=std::move(active_->done);done(false,"Native touch page is closed");}int owner=active_->owner;active_->owner=0;if(owner)hooks_.complete(owner,false,"Native touch page is closed");}
  }
  bool enabled()const{return enabled_;}
  size_t queued()const{return queue_.size();}
  size_t physical()const{return physical_?1:0;}
  bool held()const{return held_;}
 private:
  struct Work{int owner;bool configure,enabled;double x,y;Reply done;};
  Hooks hooks_;std::deque<std::shared_ptr<Work>> queue_;std::shared_ptr<Work> active_;
  bool enabled_=false,physical_=false,held_=false,closed_=false;
  void Enqueue(int owner,bool configure,bool enabled,double x,double y,Reply done={}){
    if(closed_){if(done)done(false,"Native touch page is closed");else hooks_.complete(owner,false,"Native touch page is closed");return;}
    if(queue_.size()+(active_?1:0)>=64){if(done)done(false,"Native touch operation capacity exceeded");else hooks_.complete(owner,false,"Native touch operation capacity exceeded");return;}
    queue_.push_back(std::make_shared<Work>(Work{owner,configure,enabled,x,y,std::move(done)}));Pump();
  }
  void Finish(const std::shared_ptr<Work>& work,bool ok,const std::string& error){
    if(active_!=work)return;
    int owner=work->owner;work->owner=0;active_.reset();
    auto done=std::move(work->done);if(done)done(ok,error);
    if(owner)hooks_.complete(owner,ok,error);
    Pump();
  }
  void Dispatch(Packet packet,Reply reply){
    physical_=true;
    hooks_.send(packet,[this,reply=std::move(reply)](bool ok,const std::string& error){physical_=false;reply(ok,error);});
  }
  void Release(const std::shared_ptr<Work>& work,bool ok,const std::string& error){
    Dispatch({Kind::Cancel},[this,work,ok,error](bool released,const std::string& release_error){
      if(released)held_=false;
      Finish(work,ok&&released,release_error.empty()?error:release_error);
    });
  }
  void Pump(){
    if(closed_||active_||queue_.empty())return;
    auto work=queue_.front();queue_.pop_front();active_=work;
    if(work->configure){
      Dispatch({Kind::Configure,work->enabled},[this,work](bool ok,const std::string& error){
        if(ok)enabled_=work->enabled;
        Finish(work,ok,error);
      });return;
    }
    if(!enabled_){Finish(work,false,"Touchscreen requires enabled native touch emulation (hasTouch)");return;}
    if(held_){Release(work,false,"Previous native touch contact could not be released");return;}
    Dispatch({Kind::Start,false,work->x,work->y},[this,work](bool ok,const std::string& error){
      // A failed transport acknowledgment does not prove the renderer did not
      // receive the start; cancel authentic native contacts before proceeding.
      held_=true;
      if(!ok||!work->owner||closed_){Release(work,false,error);return;}
      Dispatch({Kind::End},[this,work](bool ended,const std::string& end_error){
        if(ended){held_=false;Finish(work,true,"");}
        else Release(work,false,end_error);
      });
    });
  }
};
