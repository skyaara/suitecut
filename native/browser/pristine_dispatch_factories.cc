// SPDX-License-Identifier: MIT
#include "pristine_dispatch_factories.h"
#include "pristine_dispatch_factory.h"
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>
namespace suitecut {
class PristineDispatchFactories::Impl:public std::enable_shared_from_this<Impl>{
 public:
  struct Waiter{double deadline=0;Ready ready;};
  struct Authorization{Context owner;Barrier barrier;double deadline=0;bool consumed=false;};
  struct Record{
    uint64_t id=0;Context owner;std::string object,group,error;
    std::vector<Waiter>waiters;bool retired=false,capturing=true,releasing=false,failed_release=false,group_possible=false,realm_destroyed=false;
  };
  struct Call{std::shared_ptr<Record>record;Reply reply;double watchdog=0;bool closing=false;};
  static constexpr size_t kContexts=1024,kCalls=64,kWaiters=64;
  Hooks hooks;uint64_t next_record=0,next_call=0,next_ticket=0;bool closed=false;
  std::map<Ticket,Authorization>authorizations;
  std::map<uint64_t,std::shared_ptr<Record>>records;std::map<uint64_t,Call>calls;
  explicit Impl(Hooks h):hooks(std::move(h)){}
  static bool Valid(const Context&c){return c.context>0&&!c.frame.empty()&&c.frame.size()<=128&&!c.identity.empty()&&c.identity.size()<=256&&c.loader.size()<=128&&c.session.size()<=256;}
  static bool Expired(double deadline,double now){return deadline!=0&&now>=deadline;}
  bool Current(const std::shared_ptr<Record>&r)const{return !closed&&!r->retired&&hooks.current(r->owner);}
  size_t WaiterCount()const{size_t count=0;for(const auto&[id,r]:records)count+=r->waiters.size();return count;}
  void FailWaiters(const std::shared_ptr<Record>&r,const std::string&error){auto waiters=std::move(r->waiters);r->waiters.clear();for(auto&w:waiters)w.ready(false,std::nullopt,error);}
  void Retire(const std::shared_ptr<Record>&r,const std::string&error){
    if(r->retired)return;
    r->retired=true;r->error=error.substr(0,1024);FailWaiters(r,r->error);
    if(!r->capturing)Release(r);
  }
  bool Send(const std::shared_ptr<Record>&r,const std::string&method,Dict params,double deadline,Reply reply){
    if(closed||calls.size()>=kCalls||next_call==std::numeric_limits<uint64_t>::max()){reply(false,nullptr,"Native pristine physical capacity exceeded or channel closed");return false;}
    const auto id=++next_call;calls.emplace(id,Call{r,std::move(reply),method=="Runtime.releaseObjectGroup"?deadline:(deadline?deadline+30000:hooks.now()+30000)});hooks.wake();
    auto settled=[self=shared_from_this(),id](bool ok,Dict result,const std::string&error){
      const auto found=self->calls.find(id);if(found==self->calls.end())return;
      auto callback=std::move(found->second.reply);self->calls.erase(found);
      callback(ok,result,error);
    };
    if(method=="Runtime.evaluate")r->group_possible=true;
    if(!hooks.send(r->owner,method,params,deadline,settled)){if(method=="Runtime.evaluate")r->group_possible=false;settled(false,nullptr,"Native pristine command was not accepted");return false;}
    return true;
  }
  void Release(const std::shared_ptr<Record>&r){
    if(closed||r->capturing||r->releasing||r->failed_release||!records.contains(r->id))return;
    if(!r->group_possible){records.erase(r->id);return;}
    r->releasing=true;auto params=CefDictionaryValue::Create();params->SetString("objectGroup",r->group);
    // This is a physical cleanup watchdog, never a renewed caller budget. A
    // failed ACK requires real channel closure; it cannot retire the group.
    Send(r,"Runtime.releaseObjectGroup",params,hooks.now()+30000,[self=shared_from_this(),r](bool ok,Dict,const std::string&error){
      r->releasing=false;
      if(ok||r->realm_destroyed){self->records.erase(r->id);return;}
      r->failed_release=true;self->hooks.close_channel(r->owner.session,error.empty()?"Native pristine group release failed":error.substr(0,1024));
    });
  }
  Ticket Expect(Context owner,Barrier barrier,double deadline){
    if(closed||!Valid(owner)||!std::isfinite(deadline)||deadline<0||Expired(deadline,hooks.now())||!hooks.current(owner)||
      (barrier!=Barrier::InitialSettingsHold&&barrier!=Barrier::NewDocumentBootstrap)||authorizations.size()>=kContexts||next_ticket==std::numeric_limits<uint64_t>::max())return 0;
    for(const auto&[id,authorization]:authorizations)if(authorization.owner==owner)return 0;
    const auto ticket=++next_ticket;authorizations.emplace(ticket,Authorization{std::move(owner),barrier,deadline});return ticket;
  }
  void Capture(Ticket ticket,Ready ready){
    const auto authorization=authorizations.find(ticket);
    if(authorization==authorizations.end()){ready(false,std::nullopt,"Native pristine bootstrap admission is unavailable");return;}
    if(Expired(authorization->second.deadline,hooks.now())){ready(false,std::nullopt,"Native pristine capture original deadline exceeded");return;}
    if(authorization->second.consumed){
      const auto owner=authorization->second.owner;for(const auto&[id,r]:records)if(r->owner==owner&&!r->retired){
        if(!r->object.empty()&&Current(r)){ready(true,Factory{r->owner,r->object,r->group},"");return;}
        if(r->capturing&&Current(r)&&WaiterCount()<kWaiters){r->waiters.push_back({authorization->second.deadline,std::move(ready)});return;}
      }
      ready(false,std::nullopt,"Native pristine bootstrap admission was already consumed");return;
    }
    authorization->second.consumed=true;
    CaptureOwned(authorization->second.owner,authorization->second.deadline,std::move(ready));
  }
  void CaptureOwned(Context owner,double deadline,Ready ready){
    if(closed||!Valid(owner)||!std::isfinite(deadline)||deadline<0||!hooks.current(owner)){ready(false,std::nullopt,"Native pristine context is unavailable");return;}
    if(Expired(deadline,hooks.now())){ready(false,std::nullopt,"Native pristine capture original deadline exceeded");return;}
    for(const auto&[id,r]:records)if(r->owner==owner&&!r->retired){
      if(!r->object.empty()){ready(true,Factory{r->owner,r->object,r->group},"");return;}
      if(WaiterCount()>=kWaiters){ready(false,std::nullopt,"Native pristine waiter capacity exceeded");return;}
      r->waiters.push_back({deadline,std::move(ready)});return;
    }
    if(records.size()>=kContexts||calls.size()>=kCalls||WaiterCount()>=kWaiters||next_record==std::numeric_limits<uint64_t>::max()){ready(false,std::nullopt,"Native pristine capture capacity exceeded");return;}
    auto r=std::make_shared<Record>();r->id=++next_record;r->owner=std::move(owner);r->group="suitecut-pristine-dispatch-"+std::to_string(r->id);r->waiters.push_back({deadline,std::move(ready)});records.emplace(r->id,r);
    auto params=CefDictionaryValue::Create();params->SetString("uniqueContextId",r->owner.identity);params->SetString("expression",kSuiteCutPristineDispatchFactory);params->SetString("objectGroup",r->group);params->SetBool("returnByValue",false);params->SetBool("silent",true);params->SetBool("disableBreaks",true);
    Send(r,"Runtime.evaluate",params,deadline,[self=shared_from_this(),r](bool ok,Dict result,const std::string&error){
      r->capturing=false;
      if(!self->Current(r)){self->Retire(r,"Native pristine document changed");self->Release(r);return;}
      auto remote=result?result->GetDictionary("result"):nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")||!remote||remote->GetString("type")!="function"||remote->GetType("objectId")!=VTYPE_STRING||remote->GetString("objectId").ToString().empty()||remote->GetString("objectId").ToString().size()>4096){self->Retire(r,error.empty()?"Native pristine function capture failed":error);return;}
      r->object=remote->GetString("objectId").ToString();auto waiters=std::move(r->waiters);r->waiters.clear();size_t admitted=0;
      for(auto&w:waiters){
        if(self->Expired(w.deadline,self->hooks.now()))w.ready(false,std::nullopt,"Native pristine capture original deadline exceeded");
        else if(!self->Current(r))w.ready(false,std::nullopt,"Native pristine document changed");
        else{++admitted;w.ready(true,Factory{r->owner,r->object,r->group},"");}
      }
      if(!admitted)self->Retire(r,"Native pristine capture has no live bootstrap owner");
    });
  }
  std::optional<Factory>Find(const Context&owner)const{
    for(const auto&[id,r]:records)if(r->owner==owner&&Current(r)&&!r->object.empty())return Factory{r->owner,r->object,r->group};
    return std::nullopt;
  }
  void Invalidate(const Context&owner,const std::string&reason){for(auto&[id,a]:authorizations)if(a.owner==owner)a.consumed=true;std::vector<std::shared_ptr<Record>>selected;for(const auto&[id,r]:records)if(r->owner==owner)selected.push_back(r);for(const auto&r:selected)Retire(r,reason);}
  void Clear(const std::string&session,const std::string&reason){for(auto it=authorizations.begin();it!=authorizations.end();)if(it->second.owner.session==session)it=authorizations.erase(it);else ++it;std::vector<std::shared_ptr<Record>>selected;for(const auto&[id,r]:records)if(r->owner.session==session)selected.push_back(r);for(const auto&r:selected)Retire(r,reason);}
  void RealmDestroyed(const std::string&session,uint64_t epoch,const std::string&reason){
    for(auto it=authorizations.begin();it!=authorizations.end();)if(it->second.owner.session==session&&it->second.owner.source_epoch==epoch)it=authorizations.erase(it);else ++it;
    std::vector<std::shared_ptr<Record>>selected;
    for(const auto&[id,r]:records)if(r->owner.session==session&&r->owner.source_epoch==epoch)selected.push_back(r);
    // Mark every affected record before invoking any reentrant waiter. This is
    // actual V8 destruction, never synthesized session or command settlement.
    for(const auto&r:selected){r->realm_destroyed=true;r->group_possible=false;}
    for(const auto&r:selected){r->retired=true;r->error=reason.substr(0,1024);FailWaiters(r,r->error);records.erase(r->id);}
    // calls deliberately remains unchanged, including an accepted release.
  }
  void SessionClosed(const std::string&session){
    for(auto it=authorizations.begin();it!=authorizations.end();)if(it->second.owner.session==session)it=authorizations.erase(it);else ++it;
    std::vector<std::shared_ptr<Record>>selected;for(const auto&[id,r]:records)if(r->owner.session==session)selected.push_back(r);
    for(const auto&r:selected){r->retired=true;FailWaiters(r,"Native pristine session closed");records.erase(r->id);}
    for(auto it=calls.begin();it!=calls.end();)if(it->second.record->owner.session==session)it=calls.erase(it);else ++it;
  }
  void Tick(double now){
    std::vector<std::string>shutdown;for(auto&[id,call]:calls)if(!call.closing&&call.watchdog!=0&&now>=call.watchdog){call.closing=true;shutdown.push_back(call.record->owner.session);}
    for(const auto&session:shutdown)hooks.close_channel(session,"Native pristine physical command settlement deadline exceeded");
    std::vector<std::shared_ptr<Record>>selected;for(const auto&[id,r]:records)selected.push_back(r);
    for(const auto&r:selected){
      if(r->retired)continue;
      if(!hooks.current(r->owner)){Retire(r,"Native pristine document changed");continue;}
      auto waiters=std::move(r->waiters);r->waiters.clear();
      for(auto&w:waiters){if(Expired(w.deadline,now))w.ready(false,std::nullopt,"Native pristine capture original deadline exceeded");else if(r->retired)w.ready(false,std::nullopt,r->error);else r->waiters.push_back(std::move(w));}
      if(r->capturing&&r->waiters.empty())Retire(r,"Native pristine capture original deadline exceeded");
    }
  }
};
PristineDispatchFactories::PristineDispatchFactories(Hooks h):impl_(std::make_shared<Impl>(std::move(h))){}
PristineDispatchFactories::~PristineDispatchFactories()=default;
PristineDispatchFactories::Ticket PristineDispatchFactories::Expect(Context c,Barrier b,double d){return impl_->Expect(std::move(c),b,d);}
void PristineDispatchFactories::Capture(Ticket t,Ready r){impl_->Capture(t,std::move(r));}
std::optional<PristineDispatchFactories::Factory>PristineDispatchFactories::Find(const Context&c)const{return impl_->Find(c);}
void PristineDispatchFactories::Invalidate(const Context&c,const std::string&e){impl_->Invalidate(c,e);}
void PristineDispatchFactories::ContextDestroyed(const Context&c,const std::string&e){for(auto it=impl_->authorizations.begin();it!=impl_->authorizations.end();)if(it->second.owner==c)it=impl_->authorizations.erase(it);else ++it;impl_->Invalidate(c,e);}
void PristineDispatchFactories::ContextsCleared(const std::string&s,const std::string&e){impl_->Clear(s,e);}
void PristineDispatchFactories::RealmDestroyed(const std::string&s,uint64_t epoch,const std::string&e){impl_->RealmDestroyed(s,epoch,e);}
void PristineDispatchFactories::SessionClosed(const std::string&s){impl_->SessionClosed(s);}
void PristineDispatchFactories::Closed(){impl_->closed=true;std::vector<std::string>sessions;for(const auto&[id,r]:impl_->records)sessions.push_back(r->owner.session);for(const auto&s:sessions)impl_->SessionClosed(s);impl_->authorizations.clear();impl_->calls.clear();impl_->records.clear();}
void PristineDispatchFactories::Tick(double n){impl_->Tick(n);}
bool PristineDispatchFactories::NeedsTick()const{for(const auto&[id,call]:impl_->calls)if(!call.closing)return true;for(const auto&[id,r]:impl_->records)for(const auto&w:r->waiters)if(w.deadline!=0)return true;return false;}
size_t PristineDispatchFactories::PhysicalCount()const{return impl_->calls.size();}
void PristineDispatchFactories::Diagnostics(Dict out)const{size_t current=0,pending=0,retiring=0;for(const auto&[id,r]:impl_->records){if(r->capturing)++pending;if(r->retired)++retiring;else if(!r->object.empty())++current;}out->SetInt("pristineDispatchBootstrapTickets",impl_->authorizations.size());out->SetInt("pristineDispatchContexts",current);out->SetInt("pristineDispatchGroups",impl_->records.size());out->SetInt("pristineDispatchPendingCaptures",pending);out->SetInt("pristineDispatchRetiringGroups",retiring);out->SetInt("pristineDispatchPhysical",impl_->calls.size());}
} // namespace suitecut
