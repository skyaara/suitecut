// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <new>
#include <set>
#include "context_routes.h"
#include "context_har_routes.h"
#include "context_unroute_waits.h"
namespace suitecut {
// One UI-actor transaction. Preparation allocates complete candidate queues,
// batch map nodes, bounded immutable observations and the language receipt.
// Both real policy books change before either can invoke an installer/reply.
class NativeContextUnrouteAll {
 public:
  using Reply=NativeContextRoutes::Result;
  using Receipt=NativeContextRoutes::RemovalReceipt;
  NativeContextUnrouteAll(NativeContextRoutes&handlers,NativeContextHARRoutes&har,std::function<double()>now)
      :handlers_(handlers),har_(har),waits_(std::move(now)){}
  ~NativeContextUnrouteAll(){Clear();}
  bool Remove(int operation,int context,uint64_t generation,bool wait,double deadline,Receipt receipt,Reply reply){
    if(closed_||operation<1||!generation||!reply||entries_.contains(operation)||entries_.size()>=32)return false;
    const auto state=handlers_.states_.find(context);if(state==handlers_.states_.end())return false;
    bool committed=false;
    try{
      std::set<std::string>ids;for(const auto&[id,route]:state->second.routes){(void)route;ids.insert(id);}
      auto callbacks=GlobalRouteCallbackLeases().Capture([&](const NativeContextRouteRegistration&policy){
        if(!handlers_.OwnsPolicy(context,policy))return false;
        ids.insert(policy.id());return true;
      });
      auto selected=std::make_shared<std::vector<std::string>>(ids.begin(),ids.end());
      size_t bytes=selected->capacity()*sizeof(std::string),wire=512;
      for(const auto&id:*selected){bytes+=id.capacity();wire+=6*id.size()+4;}
      if(selected->size()>256||bytes>65536||wire>65536)return false;
      auto work=wait?GlobalHARWorkLeases().Capture([&](const auto&policy){return har_.OwnsPolicy(context,policy);}):std::optional<NativeHARWorkSnapshot>{NativeHARWorkSnapshot{}};
      if(!work)return false;
      if(!wait)callbacks.clear();
      auto entry=std::make_shared<Entry>();entry->generation=generation;entry->context=context;entry->selected=std::move(selected);entry->receipt=std::move(receipt);
      auto actual=[this,alive=alive_,operation,entry](NativeContextUnrouteWaits::Family family,bool ok,const std::string&error){
        if(!*alive||!Current(operation,entry))return;
        if(ok){if(family==NativeContextUnrouteWaits::Family::Handler)entry->handlers=true;else entry->har=true;
          if(entry->handlers&&entry->har&&entry->receipt){entry->receipt(true,*entry->selected);if(!*alive||!Current(operation,entry))return;}
        }
        waits_.Applied(operation,entry->generation,family,ok,error);
      };
      auto handlers=handlers_.PrepareRemoveAll(operation,context,deadline,[actual](bool ok,const std::string&error){actual(NativeContextUnrouteWaits::Family::Handler,ok,error);});
      if(!handlers)return false;
      auto har=har_.PrepareRemoveAll(operation,context,deadline,[actual](bool ok,const std::string&error){actual(NativeContextUnrouteWaits::Family::HAR,ok,error);});
      if(!har)return false;
      // All allocations below still precede policy deactivation. In particular,
      // failure reserving the second family's queues cannot remove the first.
      entries_.emplace(operation,entry);
      bool reserved=false;
      try{reserved=waits_.Reserve(operation,context,generation,wait,std::move(callbacks),std::move(*work),deadline,
        [this,alive=alive_,operation,entry,reply=std::move(reply)](bool ok,const std::string&error){
          if(!*alive||!Current(operation,entry)){reply(false,"Native context unrouteAll observer closed before publication");return;}
          entries_.erase(operation);reply(ok,error);
        });
      }catch(const std::bad_alloc&){entries_.erase(operation);throw;}
      if(!reserved){entries_.erase(operation);return false;}
      if(!handlers_.RemovalCurrent(*handlers)||!har_.RemovalCurrent(*har)){
        waits_.Cancel(operation,generation);entries_.erase(operation);return false;
      }
      // No callback, allocation, clock revalidation or other actor turn is
      // allowed between these two already-validated no-allocation swaps.
      handlers_.CommitRemoval(*handlers);har_.CommitRemoval(*har);committed=true;
      // The receipt may Clear or destroy this coordinator. Capture each real
      // manager's independent lifetime before delivering it; committed tasks
      // belong to those managers and must drain even without their observer.
      auto*handler_manager=&handlers_;auto*har_manager=&har_;
      auto handler_alive=handlers_.alive_,har_alive=har_.alive_;
      if(entry->receipt)entry->receipt(false,*entry->selected);
      if(*handler_alive)handler_manager->DispatchRemoval(*handlers);
      if(*har_alive)har_manager->DispatchRemoval(*har);
      return true;
    }catch(const std::bad_alloc&){if(committed)throw;return false;}
  }
  void Cancel(int operation,uint64_t generation){
    const auto found=entries_.find(operation);if(found==entries_.end()||found->second->generation!=generation)return;
    entries_.erase(found);waits_.Cancel(operation,generation);handlers_.Cancel(operation);har_.Cancel(operation);
  }
  void Cancel(int operation){const auto found=entries_.find(operation);if(found!=entries_.end())Cancel(operation,found->second->generation);}
  void Tick(){waits_.Tick();}
  void Closed(int context){waits_.Closed(context);}
  void Clear(){if(closed_)return;closed_=true;*alive_=false;entries_.clear();waits_.Clear();}
  size_t pending()const{return waits_.pending();}
 private:
  struct Entry {int context=0;uint64_t generation=0;bool handlers=false,har=false;std::shared_ptr<std::vector<std::string>>selected;Receipt receipt;};
  bool Current(int operation,const std::shared_ptr<Entry>&entry)const{const auto found=entries_.find(operation);return found!=entries_.end()&&found->second==entry;}
  NativeContextRoutes&handlers_;NativeContextHARRoutes&har_;NativeContextUnrouteWaits waits_;
  bool closed_=false;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);std::map<int,std::shared_ptr<Entry>>entries_;
};
}
