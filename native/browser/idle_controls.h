// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace suitecut {
// UI-thread-only. Logical cancellation never retires a dispatched override.
// The sender retains its callback until actual CDP reply/target destruction.
class NativeIdleControls:public std::enable_shared_from_this<NativeIdleControls>{
 public:
  struct State{bool user_active=false,screen_unlocked=false;bool operator==(const State&)const=default;};
  using Result=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(int,const std::string&,std::optional<State>,Result)>;
  using Sessions=std::function<std::vector<std::string>()>;
  using Live=std::function<bool(const std::string&)>;
  // Source uses this only for a retained native callback. Its destructor is
  // reached on actual target close, never logical operation cancellation.
  class Receipt{
   public:
    explicit Receipt(Result done):done_(std::move(done)){}
    ~Receipt(){Settle(false,"Actual native idle callback retired with its target");}
    void Settle(bool ok,const std::string&error){auto done=std::move(done_);if(done)done(ok,error);}
   private:Result done_;
  };
  NativeIdleControls(Send send,Sessions sessions,Live live,std::function<double()>now)
    :send_(std::move(send)),sessions_(std::move(sessions)),live_(std::move(live)),now_(std::move(now)){}
  bool Apply(int owner,uint64_t generation,std::optional<State>state,double deadline,Result done){
    if(owner<1||generation==0||closed_||!ValidDeadline(deadline)||!done||Size()>=64||Owns(owner,generation))return false;
    auto work=std::make_shared<Work>();work->owner=owner;work->generation=generation;work->state=state;work->deadline=deadline;work->done=std::move(done);queue_.push_back(work);Pump();return true;
  }
  bool Child(const std::string&session,double deadline,Result done){
    if(session.empty()||session.size()>128||closed_||!ValidDeadline(deadline)||!done||Size()>=64)return false;
    auto work=std::make_shared<Work>();work->child=true;work->sessions.push_back(session);work->deadline=deadline;work->done=std::move(done);queue_.push_back(work);Pump();return true;
  }
  void Cancel(int owner,uint64_t generation){
    if(active_&&active_->owner==owner&&active_->generation==generation){active_->canceled=true;active_->done={};}
    for(auto it=queue_.begin();it!=queue_.end();)if((*it)->owner==owner&&(*it)->generation==generation)it=queue_.erase(it);else ++it;
    Pump();
  }
  void Close(){
    closed_=true;queue_.clear();if(active_){active_->canceled=true;active_->done={};if(!physical_)active_.reset();}
  }
  bool Pending()const{return physical_||active_||!queue_.empty();}
  bool Physical()const{return physical_;}
  bool DispatchCurrent()const{return physical_&&active_&&!closed_&&!active_->canceled&&!Expired(*active_)&&active_->next>0&&live_(active_->sessions.at(active_->next-1));}
  size_t Size()const{return queue_.size()+(active_?1:0);}
  std::optional<State> Acknowledged()const{return state_;}
 private:
  struct Work{int owner=0;uint64_t generation=0;bool child=false,canceled=false;std::optional<State>state;double deadline=0;Result done;std::vector<std::string>sessions;size_t next=0;};
  static bool ValidDeadline(double deadline){return std::isfinite(deadline)&&deadline>=0;}
  bool Expired(const Work&work)const{return work.deadline!=0&&now_()>=work.deadline;}
  bool Owns(int owner,uint64_t generation)const{
    if(active_&&active_->owner==owner&&active_->generation==generation)return true;
    return std::any_of(queue_.begin(),queue_.end(),[&](const auto&work){return work->owner==owner&&work->generation==generation;});
  }
  void Finish(bool ok,const std::string&error){auto work=std::move(active_);auto done=std::move(work->done);if(done)done(ok,error);}
  void Pump(){
    if(pumping_||closed_)return;
    pumping_=true;struct Restore{bool&value;~Restore(){value=false;}}restore{pumping_};
    while(!closed_&&!physical_){
      if(!active_){
        if(queue_.empty())return;
        active_=std::move(queue_.front());queue_.pop_front();
        if(active_->child)active_->state=state_;
        else{active_->sessions=sessions_();active_->sessions.insert(active_->sessions.begin(),"");}
      }
      const auto work=active_;
      if(work->canceled||Expired(*work)){Finish(false,"Native idle original operation deadline expired or canceled");continue;}
      if(work->next==work->sessions.size()){Finish(true,"");continue;}
      const auto session=work->sessions[work->next++];
      if(!live_(session)){
        if(!session.empty()&&!work->child)continue;
        Finish(false,"Native idle target is closed");continue;
      }
      physical_=true;
      send_(work->owner,session,work->state,[self=shared_from_this(),work,session](bool ok,const std::string&error){
        if(self->active_!=work)return;
        self->physical_=false;
        // Even an expired/canceled root command can genuinely have applied.
        // Record that native fact before admitting the next serialized change.
        if(ok&&session.empty()&&!self->closed_)self->state_=work->state;
        if(self->closed_){self->active_.reset();return;}
        if(!ok&&(session.empty()||work->child||self->live_(session)))self->Finish(false,error);
        self->Pump();
      });
    }
  }
  Send send_;Sessions sessions_;Live live_;std::function<double()>now_;
  std::deque<std::shared_ptr<Work>>queue_;std::shared_ptr<Work>active_;
  std::optional<State>state_;bool physical_=false,pumping_=false,closed_=false;
};
}
