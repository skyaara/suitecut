// SPDX-License-Identifier: MIT
#pragma once
#include "init_startup_receiver.h"
#include <functional>

namespace suitecut {
// Browser/UI-thread-only UNUSED data adapter. It never touches a V8 context,
// renderer ledger, or renderer clock, and never sends an execution permit.
class InitStartupReadiness {
 private:
  struct Identity {};
 public:
  using Peer=InitStartupBrowserPeer::Ptr;
  struct Hooks {std::function<double()>now;std::function<bool(int,uint64_t)>current;};
  class Operation {
   private:
    friend class InitStartupReadiness;
    Operation(std::shared_ptr<const Identity>identity,int command,uint64_t generation,double deadline):identity_(std::move(identity)),command_(command),generation_(generation),deadline_(deadline){}
    std::shared_ptr<const Identity>identity_;int command_;uint64_t generation_;double deadline_;
  };
  class CredentialLease {
   private:
    friend class InitStartupReadiness;
    CredentialLease(Peer peer,Operation operation,uint64_t generation,int version):peer_(std::move(peer)),operation_(std::move(operation)),generation_(generation),version_(version){}
    Peer peer_;Operation operation_;uint64_t generation_;int version_;bool completed_=false,ready_=false;
  };
  class Ticket {
   private:
    friend class InitStartupReadiness;
    Ticket(uint64_t id,Peer peer,Operation operation,InitSnapshotPin pin,uint64_t reset,int version,bool installed):
      id_(id),peer_(std::move(peer)),operation_(std::move(operation)),pin_(std::move(pin)),reset_(reset),version_(version),installed_(installed){}
    uint64_t id_;Peer peer_;Operation operation_;InitSnapshotPin pin_;uint64_t reset_;int version_;bool installed_,observed_=false,semantic_=false,published_=false;
  };
  class Eligibility {
   private:
    friend class InitStartupReadiness;
    explicit Eligibility(std::shared_ptr<Ticket>ticket):ticket_(std::move(ticket)){}
    std::shared_ptr<Ticket>ticket_;
  };
  explicit InitStartupReadiness(Hooks hooks):hooks_(std::move(hooks)){}
  InitStartupReadiness(const InitStartupReadiness&)=delete;InitStartupReadiness&operator=(const InitStartupReadiness&)=delete;
  std::optional<Operation>ObserveOperation(int command,uint64_t generation,double original_deadline){
    if(command<=0||!generation||!std::isfinite(original_deadline)||original_deadline<0)return std::nullopt;
    Operation operation(identity_,command,generation,original_deadline);return Current(operation)?std::optional(operation):std::nullopt;
  }
  std::shared_ptr<CredentialLease>ResetCredentials(const Peer&peer,const Operation&operation,uint64_t reset_generation,int credential_version){
    if(!peer||!peer->Current()||!Current(operation)||!reset_generation||credential_version<0)return {};
    const auto key=Key(peer->Owner());auto old=credentials_.find(key);
    if((old==credentials_.end()&&credentials_.size()>=64)||(old!=credentials_.end()&&(old->second->peer_!=peer||old->second->generation_>=reset_generation)))return {};
    auto lease=std::shared_ptr<CredentialLease>(new CredentialLease(peer,operation,reset_generation,credential_version));
    if(old==credentials_.end())credentials_.emplace(key,lease);else old->second=lease;return lease;
  }
  // Invoke only from the exact native credential callback, not transport send.
  // A late real callback is observed once but cannot create a positive fact.
  bool CredentialsCompleted(const std::shared_ptr<CredentialLease>&lease,bool actual_success){
    if(!lease||lease->operation_.identity_!=identity_||lease->completed_)return false;
    lease->completed_=true;
    auto actual=credentials_.find(Key(lease->peer_->Owner()));
    if(actual==credentials_.end()||actual->second!=lease||!lease->peer_->Current()||!Current(lease->operation_)||!actual_success)return false;
    lease->ready_=true;return true;
  }
  std::shared_ptr<Ticket>Reserve(const Peer&peer,const Operation&operation,const InitSnapshotPin&expected,uint64_t reset_generation,int credential_version,bool credentials_installed){
    if(!peer||!peer->Current()||!Current(operation)||expected.owner!=peer->Owner()||!InitSnapshotWire::Size(expected,1)||pending_.size()>=64||next_==UINT64_MAX)return {};
    auto credential=credentials_.find(Key(peer->Owner()));
    if(credential==credentials_.end()||credential->second->peer_!=peer||credential->second->generation_!=reset_generation||credential->second->version_!=credential_version)return {};
    for(const auto&[id,ticket]:pending_)if(ticket->pin_==expected)return {};
    auto ticket=std::shared_ptr<Ticket>(new Ticket(next_+1,peer,operation,expected,reset_generation,credential_version,credentials_installed));
    pending_.emplace(next_+1,ticket);++next_;return ticket;
  }
  bool Received(const Peer&peer,CefProcessId sender,CefRefPtr<CefFrame>actual_frame,CefRefPtr<CefProcessMessage>message){
    if(!peer||sender!=PID_RENDERER||!peer->Channel(actual_frame))return false;
    InitSnapshotPin pin;int credential_version=0;bool credentials_installed=false;
    if(!InitStartupSemanticMessage::Read(message,pin,credential_version,credentials_installed)||pin.owner!=peer->Owner())return false;
    for(auto&[id,ticket]:pending_)if(ticket->peer_==peer&&ticket->pin_==pin){
      ticket->observed_=true;
      if(!Current(ticket->operation_)||ticket->version_!=credential_version||ticket->installed_!=credentials_installed)return false;
      ticket->semantic_=true;return true;
    }
    return false;
  }
  std::shared_ptr<const Eligibility>Publish(const std::shared_ptr<Ticket>&ticket){
    if(!Valid(ticket)||ticket->published_||!Ready(ticket))return {};
    auto result=std::shared_ptr<const Eligibility>(new Eligibility(ticket));
    // Actual live registry/original expiry query at final publication, after
    // allocation. No sampled bool is substituted for this original lease.
    if(!Valid(ticket)||!Ready(ticket))return {};
    ticket->published_=true;return result;
  }
  bool Current(const std::shared_ptr<const Eligibility>&eligibility)const{return eligibility&&Valid(eligibility->ticket_)&&eligibility->ticket_->published_&&Ready(eligibility->ticket_);}
  bool Forget(const std::shared_ptr<Ticket>&ticket){if(!ticket)return false;auto found=pending_.find(ticket->id_);if(found==pending_.end()||found->second!=ticket)return false;pending_.erase(found);return true;}
  bool Retire(const Peer&peer){
    if(!peer)return false;
    peer->Retire();bool removed=false;
    for(auto it=pending_.begin();it!=pending_.end();)if(it->second->peer_==peer){it=pending_.erase(it);removed=true;}else ++it;
    auto credential=credentials_.find(Key(peer->Owner()));if(credential!=credentials_.end()&&credential->second->peer_==peer){credentials_.erase(credential);removed=true;}return removed;
  }
  size_t Pending()const{return pending_.size();}
  bool Observed(const std::shared_ptr<Ticket>&ticket)const{return ticket&&ticket->operation_.identity_==identity_&&ticket->observed_;}
 private:
  using OwnerKey=std::tuple<uint64_t,uint64_t,uint64_t,std::string>;
  static OwnerKey Key(const InitSnapshotOwner&owner){return {owner.context,owner.browser,owner.renderer_epoch,owner.frame};}
  bool Current(const Operation&operation)const{
    if(operation.identity_!=identity_||!hooks_.now||!hooks_.current)return false;
    const auto now=hooks_.now();return std::isfinite(now)&&now>=0&&(!operation.deadline_||now<operation.deadline_)&&hooks_.current(operation.command_,operation.generation_);
  }
  bool Valid(const std::shared_ptr<Ticket>&ticket)const{
    if(!ticket||!ticket->peer_->Current()||!Current(ticket->operation_))return false;
    auto found=pending_.find(ticket->id_);return found!=pending_.end()&&found->second==ticket;
  }
  bool Ready(const std::shared_ptr<Ticket>&ticket)const{
    if(!ticket->semantic_)return false;
    const auto actual=credentials_.find(Key(ticket->peer_->Owner()));
    return actual!=credentials_.end()&&actual->second->peer_==ticket->peer_&&actual->second->generation_==ticket->reset_&&actual->second->version_==ticket->version_&&actual->second->ready_;
  }
  Hooks hooks_;std::shared_ptr<const Identity>identity_=std::make_shared<Identity>();uint64_t next_=0;
  std::map<OwnerKey,std::shared_ptr<CredentialLease>>credentials_;std::map<uint64_t,std::shared_ptr<Ticket>>pending_;
};
} // namespace suitecut
