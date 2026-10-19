// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include "page_route_callback_selection.h"
#include "route_callback_waits.h"
namespace suitecut {
// UI-actor observer transactions. Apply owns the real bounded native control
// callback; queued reservation, logical cancellation and real ACK are distinct.
class NativePageRouteRemovals {
 public:
  using Reply=std::function<void(bool,const std::string&)>;
  using Apply=std::function<void(double,Reply)>;
  using Emit=std::function<void(bool,const std::string&,const std::vector<std::string>&,const NativePageRouteCallbackSelection&)>;
  NativePageRouteRemovals(uint64_t scope,std::function<double()>now,Emit emit):scope_(scope),now_(std::move(now)),emit_(std::move(emit)),waits_(now_){}
  ~NativePageRouteRemovals(){Clear();}
  NativePageRouteRemovals(const NativePageRouteRemovals&)=delete;
  NativePageRouteRemovals&operator=(const NativePageRouteRemovals&)=delete;
  bool Remove(int operation,int context,uint64_t generation,NativePageRouteCallbackSelection selected,std::vector<std::string>ids,std::string mutation,bool wait,double deadline,std::function<void()>deactivate,Apply apply,Reply reply){
    if(closed_||!scope_||operation<1||context<1||!generation||!reply||!apply||!deactivate||!emit_||!std::isfinite(deadline)||deadline<0||(deadline&&now_()>=deadline)||active_.contains(operation)||physical_->receipts.size()>=64||serial_==UINT64_MAX||selected.scope!=scope_||selected.tokens.size()!=selected.receipts.size()||selected.tokens.size()>128||ids.size()>256||mutation.empty()||mutation.size()>128||mutation.find('\0')!=std::string::npos)return false;
    uint64_t previous=0;for(size_t index=0;index<selected.tokens.size();++index){if(selected.tokens[index]<=previous||!selected.receipts[index])return false;previous=selected.tokens[index];}
    size_t wire=512+6*mutation.size()+128*24;std::set<std::string>unique;for(const auto&id:ids){if(id.empty()||id.size()>128||id.find('\0')!=std::string::npos||!unique.insert(id).second)return false;wire+=6*id.size()+4;}if(wire>65536)return false;
    auto snapshot=std::move(selected.receipts);
    auto task=std::make_shared<Task>();task->operation=operation;task->generation=generation;task->receipt=serial_+1;task->selected=std::move(selected);task->ids=std::move(ids);task->mutation=std::move(mutation);task->reply=std::move(reply);task->wait=wait;task->deadline=deadline;
    if(!task->Admit())return false;
    const auto weak=std::weak_ptr<bool>(alive_);
    if(wait&&!waits_.Reserve(operation,context,generation,std::move(snapshot),deadline,[this,weak,task](bool ok,const std::string&error){auto alive=weak.lock();if(alive&&*alive)Complete(task,ok,error);}))return false;
    ++serial_;physical_->receipts.insert(task->receipt);active_.emplace(operation,task);
    deactivate();
    if(!Current(this,weak,task))return true;
    emit_(false,task->mutation,task->ids,task->selected);
    if(!Current(this,weak,task))return true;
    // A reentrant cancellation before this point cannot dispatch a side effect.
    if(deadline&&now_()>=deadline){physical_->receipts.erase(task->receipt);if(wait)waits_.Failed(operation,generation,"Native unrouteAll original deadline exceeded");else Complete(task,false,"Native unrouteAll original deadline exceeded");return true;}
    task->dispatched=true;auto physical=physical_;
    apply(deadline,[this,weak,physical,task](bool ok,const std::string&error){
      if(!physical->receipts.erase(task->receipt))return;
      if(!Current(this,weak,task))return;
      if(!ok){if(task->wait)waits_.Failed(task->operation,task->generation,error);else Complete(task,false,error);return;}
      if(task->deadline&&now_()>=task->deadline){if(task->wait)waits_.Failed(task->operation,task->generation,"Native unrouteAll original deadline exceeded");else Complete(task,false,"Native unrouteAll original deadline exceeded");return;}
      emit_(true,task->mutation,task->ids,task->selected);
      if(!Current(this,weak,task))return;
      task->DropMetadata();
      if(task->wait)waits_.Applied(task->operation,task->generation);else Complete(task,true,"");
    });return true;
  }
  void Cancel(int operation){
    auto found=active_.find(operation);if(found==active_.end())return;auto task=std::move(found->second);active_.erase(found);task->canceled=true;task->reply={};task->DropMetadata();waits_.Cancel(operation);if(!task->dispatched)physical_->receipts.erase(task->receipt);
  }
  void Tick(){
    auto weak=std::weak_ptr<bool>(alive_);std::vector<std::shared_ptr<Task>>expired;const auto now=now_();for(const auto&[operation,task]:active_)if(!task->wait&&task->deadline&&now>=task->deadline)expired.push_back(task);
    waits_.Tick();for(const auto&task:expired)if(Current(this,weak,task))Complete(task,false,"Native unrouteAll original deadline exceeded");
  }
  void Clear(){
    if(closed_)return;
    closed_=true;*alive_=false;
    std::vector<Reply>replies;for(auto&[operation,task]:active_){task->canceled=true;if(task->reply)replies.push_back(std::move(task->reply));task->DropMetadata();if(!task->dispatched)physical_->receipts.erase(task->receipt);}active_.clear();waits_.Clear();
    for(auto&reply:replies)reply(false,"Native Page route removal observer disposed before callback settlement");
  }
  size_t pending()const{return active_.size();}
  size_t retained_applies()const{return physical_->receipts.size();}
  static size_t retained_tasks(){return Task::count;}
  static size_t retained_payload(){return Task::total_bytes;}
 private:
  struct Physical{std::set<uint64_t>receipts;};
  struct Task{
    int operation=0;uint64_t generation=0,receipt=0;NativePageRouteCallbackSelection selected;std::vector<std::string>ids;std::string mutation;Reply reply;double deadline=0;bool wait=false,dispatched=false,canceled=false,charged=false;size_t bytes=0;
    static inline size_t count=0,total_bytes=0;
    size_t Bytes()const{size_t value=sizeof(Task)+selected.bytes()+ids.capacity()*sizeof(std::string)+mutation.capacity();for(const auto&id:ids)value+=id.capacity();return value;}
    bool Admit(){bytes=Bytes();if(count>=128||bytes>4194304-total_bytes)return false;charged=true;++count;total_bytes+=bytes;return true;}
    void DropMetadata(){std::vector<std::string>().swap(ids);std::string().swap(mutation);std::vector<uint64_t>().swap(selected.tokens);NativeRouteCallbackSnapshot().swap(selected.receipts);if(charged){const auto current=Bytes();total_bytes-=bytes-current;bytes=current;}}
    ~Task(){if(charged){--count;total_bytes-=bytes;}}
  };
  static bool Current(NativePageRouteRemovals*self,const std::weak_ptr<bool>&weak,const std::shared_ptr<Task>&task){auto alive=weak.lock();if(!alive||!*alive||task->canceled)return false;const auto found=self->active_.find(task->operation);return found!=self->active_.end()&&found->second==task;}
  void Complete(const std::shared_ptr<Task>&task,bool ok,const std::string&error){
    const auto found=active_.find(task->operation);if(found==active_.end()||found->second!=task||task->canceled)return;
    auto reply=std::move(task->reply);active_.erase(found);if(!task->dispatched)physical_->receipts.erase(task->receipt);task->canceled=true;task->DropMetadata();if(reply)reply(ok,error);
  }
  uint64_t scope_,serial_=0;std::function<double()>now_;Emit emit_;NativeRouteCallbackWaits waits_;std::map<int,std::shared_ptr<Task>>active_;std::shared_ptr<Physical>physical_=std::make_shared<Physical>();std::shared_ptr<bool>alive_=std::make_shared<bool>(true);bool closed_=false;
};
}  // namespace suitecut
