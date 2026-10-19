// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace suitecut {
// UI-thread native domain ownership. Accepted enable/disable work retires only
// on its actual Inspector ACK or genuine target-channel destruction. Callers
// keep logical deadlines; readiness truthfully reports even a late enable ACK
// so their physical cleanup still releases an acquired lease.
class DOMDomainLeases : public std::enable_shared_from_this<DOMDomainLeases> {
 public:
  using Ready=std::function<void(bool,const std::string&)>;
  struct Hooks {
    std::function<double()> now;
    std::function<bool(const std::string&,const std::string&,double,Ready)> send;
    std::function<void(const std::string&,const std::string&)> fatal;
  };
  explicit DOMDomainLeases(Hooks hooks):hooks_(std::move(hooks)){}
  void Acquire(const std::string& session,const std::string& owner,double deadline,Ready ready){
    if(closed_||owner.empty()||owner.size()>256||session.size()>256||!std::isfinite(deadline)||deadline<0||(deadline&&hooks_.now()>=deadline)||owners_>=128){ready(false,"Native DOM domain lease admission rejected");return;}
    auto found=sessions_.find(session);
    if(found==sessions_.end()){
      if(sessions_.size()>=64){ready(false,"Native DOM domain session capacity exceeded");return;}
      auto state=std::make_shared<Session>();state->id=session;found=sessions_.emplace(session,state).first;
    }
    const auto state=found->second;
    if(state->failed||state->owners.contains(owner)){ready(false,"Native DOM domain owner is duplicate or failed");return;}
    state->owners.emplace(owner,Lease{deadline,std::move(ready)});++owners_;Pump(state);
  }
  void Release(const std::string& session,const std::string& owner,Ready ready){
    const auto found=sessions_.find(session);
    if(found==sessions_.end()||!found->second->owners.contains(owner)){ready(false,"Native DOM domain lease release has no actual owner");return;}
    if(releases_>=128){ready(false,"Native DOM domain release waiter capacity exceeded");return;}
    const auto state=found->second;auto acquire=std::move(state->owners.at(owner).ready);
    state->owners.erase(owner);--owners_;
    // Reserve before any callback can reenter or destroy the actual channel.
    // The actual enable ACK also settles releases shared with a newer owner.
    state->releases.push_back(std::move(ready));++releases_;
    if(acquire)acquire(false,"Native DOM domain lease released before readiness");
    Pump(state);
  }
  void DisableUnused(const std::string& session,double deadline,Ready ready){
    if(closed_||session.size()>256||!std::isfinite(deadline)||deadline<0||(deadline&&hooks_.now()>=deadline)||sessions_.contains(session)||sessions_.size()>=64||releases_>=128){ready(false,"Native DOM disable conflicts with actual domain ownership");return;}
    auto state=std::make_shared<Session>();state->id=session;state->force_disable=true;state->forced_deadline=deadline;state->releases.push_back(std::move(ready));++releases_;sessions_.emplace(session,state);Pump(state);
  }
  bool Owned(const std::string& session)const{return sessions_.contains(session);}
  bool HasOwner(const std::string&session,const std::string&owner)const{const auto found=sessions_.find(session);return found!=sessions_.end()&&found->second->owners.contains(owner);}
  bool OwnerReady(const std::string&session,const std::string&owner)const{const auto found=sessions_.find(session);return found!=sessions_.end()&&found->second->enabled&&!found->second->busy&&found->second->owners.contains(owner);}
  bool OtherOwners(const std::string&session,const std::string&owner)const{const auto found=sessions_.find(session);if(found==sessions_.end())return false;return found->second->owners.size()>size_t(found->second->owners.contains(owner));}
  size_t Owners()const{return owners_;}size_t Physical()const{return physical_;}
  size_t PendingReleases()const{return releases_;}
  void Destroyed(const std::string& session){
    const auto found=sessions_.find(session);if(found==sessions_.end())return;
    const auto state=found->second;sessions_.erase(found);state->dead=true;
    if(state->busy){state->busy=false;--physical_;}
    owners_-=state->owners.size();auto owners=std::move(state->owners);auto releases=std::move(state->releases);
    for(auto&[id,lease]:owners){(void)id;if(lease.ready)lease.ready(false,"Native DOM target channel destroyed");}
    Replied(std::move(releases),true,"");
  }
  void Closed(){if(closed_)return;closed_=true;while(!sessions_.empty())Destroyed(sessions_.begin()->first);}
 private:
  struct Lease {double deadline=0;Ready ready;};
  struct Session {std::string id;double forced_deadline=0;bool enabled=false,busy=false,failed=false,dead=false,force_disable=false;std::map<std::string,Lease>owners;std::vector<Ready>releases;};
  void Replied(std::vector<Ready> replies,bool ok,const std::string&error){
    releases_-=replies.size();
    for(auto& reply:replies)reply(ok,error);
  }
  void Pump(const std::shared_ptr<Session>& s){
    if(s->dead||s->busy||s->failed)return;
    if(s->enabled&&!s->owners.empty()){
      auto releases=std::move(s->releases);Replied(std::move(releases),true,"");
      if(s->dead||s->busy||s->failed)return;
      std::vector<std::pair<std::string,Ready>> replies;for(auto&[id,lease]:s->owners){if(lease.ready)replies.emplace_back(id,std::move(lease.ready));}
      for(auto&[id,reply]:replies){const bool live=!s->dead&&s->owners.contains(id);reply(live,live?"":"Native DOM lease retired during readiness");}
      if(s->dead||s->busy||s->failed)return;
    }
    if(s->owners.empty()&&!s->enabled&&!s->force_disable){
      auto replies=std::move(s->releases);sessions_.erase(s->id);
      Replied(std::move(replies),true,"");
      return;
    }
    if(s->enabled&&!s->owners.empty())return;
    if(!s->enabled&&!s->force_disable){
      std::vector<std::string> expired;for(const auto&[id,lease]:s->owners)if(lease.deadline&&hooks_.now()>=lease.deadline)expired.push_back(id);
      for(const auto& id:expired){const auto found=s->owners.find(id);if(found==s->owners.end())continue;auto reply=std::move(found->second.ready);s->owners.erase(found);--owners_;if(reply)reply(false,"Native DOM original acquisition deadline expired");}
      if(s->dead||s->busy||s->failed)return;
      if(s->owners.empty()){Pump(s);return;}
    }
    const bool enable=!s->enabled&&!s->force_disable,forced=s->force_disable&&!s->enabled;
    double deadline=forced?s->forced_deadline:hooks_.now()+30000;
    if(enable){deadline=0;for(const auto&[id,lease]:s->owners){(void)id;if(lease.deadline&&(!deadline||lease.deadline<deadline))deadline=lease.deadline;}}
    s->busy=true;++physical_;const auto self=shared_from_this();
    auto settled=std::make_shared<bool>(false);
    auto reply=[self,s,enable,forced,settled](bool ok,const std::string& error){
      if(*settled)return;
      *settled=true;
      if(s->dead)return;
      s->busy=false;--self->physical_;
      if(ok){s->enabled=enable;s->force_disable=false;if(!enable){auto releases=std::move(s->releases);self->Replied(std::move(releases),true,"");}self->Pump(s);return;}
      if(!enable&&forced){s->force_disable=false;auto releases=std::move(s->releases);self->Replied(std::move(releases),false,error);self->Pump(s);return;}
      if(!enable){s->failed=true;self->hooks_.fatal(s->id,error.empty()?"Native DOM domain teardown failed":error);return;}
      auto owners=std::move(s->owners);self->owners_-=owners.size();
      for(auto&[id,lease]:owners){(void)id;if(lease.ready)lease.ready(false,error);}
      self->Pump(s);
    };
    if(!hooks_.send(s->id,enable?"DOM.enable":"DOM.disable",deadline,reply))reply(false,"Native DOM domain submission was rejected");
  }
  Hooks hooks_;std::map<std::string,std::shared_ptr<Session>>sessions_;size_t owners_=0,physical_=0,releases_=0;bool closed_=false;
};
} // namespace suitecut
