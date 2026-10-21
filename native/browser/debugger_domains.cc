// SPDX-License-Identifier: MIT
#include "debugger_domains.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
namespace suitecut {
class DebuggerDomains::Impl : public std::enable_shared_from_this<Impl> {
 public:
  struct Lease {
    uint64_t id=0;Kind kind=Kind::Coverage;double deadline=0,cleanup=0;
    bool acquired=false,releasing=false,release_requested=false;size_t holds=0;
    Ready ready,arm_ready,release_ready;Dict arm;Paused paused;std::string breakpoint;
    DeveloperDeadline developer_deadline;DeveloperPaused developer_paused;DeveloperExpired developer_expired;DeveloperEligibility developer_eligible;
  };
  struct PacketBudget {size_t packets=0;};
  struct PausePacket {Dict data;std::shared_ptr<PacketBudget> budget;~PausePacket(){data=nullptr;--budget->packets;}};
  struct Capture {std::shared_ptr<PausePacket> packet;uint64_t id=0,lease=0;double deadline=0;bool requested=false,expired=false,actual_resumed=false;Ready reply;std::string continuation="Debugger.resume";double continuation_deadline=0;};
  struct Session {
    std::string id;bool enabled=false,busy=false,closing=false;std::optional<bool> skip, bounded_script_cache;
    std::map<uint64_t,std::shared_ptr<Lease>> leases;std::optional<Capture> capture;std::shared_ptr<PausePacket> deferred_pause;double deferred_deadline=0;bool deferred_expired=false,deferred_rejected=false;uint64_t deferred_developer=0;double deferred_developer_deadline=0;DeveloperPauseDisposition deferred_disposition=DeveloperPauseDisposition::Foreign;
  };
  struct Call {std::shared_ptr<Session> session;std::string method;double retirement=0;Reply done;};
  struct RawState {bool tainted=false,closing=false;size_t pending=0;};
  struct RawCall {std::string session,method;double retirement=0;};
  struct Receipt {std::shared_ptr<Session> session;std::shared_ptr<Lease> lease;};
  static constexpr size_t kSessions=2049,kLeases=4098,kPhysical=64,kReceipts=128;
  explicit Impl(Hooks hooks):hooks_(std::move(hooks)){}
  std::shared_ptr<PacketBudget> packet_budget_=std::make_shared<PacketBudget>();
  Hooks hooks_;std::map<std::string,std::shared_ptr<Session>> sessions_;
  std::map<uint64_t,std::shared_ptr<Session>> owners_;std::map<uint64_t,Call> calls_;
  std::map<std::string,RawState> raw_;std::map<uint64_t,RawCall> raw_calls_;
  std::map<uint64_t,Receipt> receipts_;uint64_t identity_=0,last_lease_=0;bool pumping_=false,again_=false,closed_=false;size_t classifying_=0;
  int unowned_pause_events_=0,deferred_unowned_pause_events_=0;
  static void ObservePause(int& count){if(count<std::numeric_limits<int>::max())++count;}
  static bool ScanValue(CefRefPtr<CefValue> value,size_t depth,size_t& nodes,size_t& units){
    if(!value||depth>24||++nodes>4096)return false;
    switch(value->GetType()){
      case VTYPE_STRING:{const auto size=value->GetString().length();if(size>131072-units)return false;units+=size;return true;}
      case VTYPE_DICTIONARY:return ScanDictionary(value->GetDictionary(),depth,nodes,units);
      case VTYPE_LIST:{auto list=value->GetList();if(!list||list->GetSize()>4096-nodes)return false;for(size_t i=0;i<list->GetSize();++i)if(!ScanValue(list->GetValue(i),depth+1,nodes,units))return false;return true;}
      case VTYPE_NULL:case VTYPE_BOOL:case VTYPE_INT:case VTYPE_DOUBLE:return true;
      default:return false;
    }
  }
  static bool ScanDictionary(Dict dictionary,size_t depth,size_t& nodes,size_t& units){
    if(!dictionary||depth>24||dictionary->GetSize()>4096-nodes)return false;
    CefDictionaryValue::KeyList keys;dictionary->GetKeys(keys);
    for(const auto& key:keys){const auto size=key.length();if(size>131072-units)return false;units+=size;if(!ScanValue(dictionary->GetValue(key),depth+1,nodes,units))return false;}return true;
  }
  std::shared_ptr<PausePacket> Packet(Dict data){
    size_t nodes=1,units=0;if(packet_budget_->packets>=64||!ScanDictionary(data,0,nodes,units))return {};
    ++packet_budget_->packets;auto packet=std::make_shared<PausePacket>();packet->budget=packet_budget_;packet->data=data->Copy(false);return packet;
  }
  void RejectedPause(const std::shared_ptr<Session>& s,Dict params,const std::string& error){
    std::vector<uint64_t> leases;for(const auto& [id,l]:s->leases)if(l->kind==Kind::Factory&&!l->releasing)leases.push_back(id);
    for(const auto lease:leases)if(hooks_.pause_expired)hooks_.pause_expired(lease,params,error);
    const auto developer=Developer(s);if(developer&&developer->developer_expired)developer->developer_expired(developer->id,params,error);
  }
  void ExpirePause(uint64_t id,Dict packet,const std::string& error){
    auto [session,lease]=Find(id);
    if(lease&&lease->kind==Kind::Developer){if(lease->developer_expired)lease->developer_expired(id,packet,error);}
    else if(hooks_.pause_expired)hooks_.pause_expired(id,packet,error);
  }
  std::shared_ptr<Lease> Developer(const std::shared_ptr<Session>& s)const{
    for(const auto&[id,lease]:s->leases)if(lease->kind==Kind::Developer&&!lease->releasing)return lease;
    return {};
  }
  DeveloperPauseDisposition Eligible(const std::shared_ptr<Session>&s,const std::shared_ptr<Lease>&lease,Dict packet){
    struct Guard{size_t&depth;explicit Guard(size_t&value):depth(value){++depth;}~Guard(){--depth;}}guard(classifying_);
    const auto result=lease->developer_eligible?lease->developer_eligible(packet):DeveloperPauseDisposition::Owned;
    if(!Current(s))return DeveloperPauseDisposition::Foreign;
    const auto found=s->leases.find(lease->id);
    if(found==s->leases.end()||found->second!=lease||lease->releasing)return DeveloperPauseDisposition::Foreign;
    switch(result){case DeveloperPauseDisposition::Owned:case DeveloperPauseDisposition::Foreign:case DeveloperPauseDisposition::Unsupported:return result;}
    return DeveloperPauseDisposition::Unsupported;
  }
  void DeferredDeveloper(const std::shared_ptr<Session>& s,Dict packet){
    const auto lease=Developer(s);
    const auto disposition=lease?Eligible(s,lease,packet):DeveloperPauseDisposition::Foreign;
    if(!Current(s))return;
    s->deferred_developer=lease?lease->id:0;s->deferred_disposition=disposition;
    s->deferred_developer_deadline=lease&&disposition==DeveloperPauseDisposition::Owned?lease->developer_deadline(packet):0;
    if(lease&&(!std::isfinite(s->deferred_developer_deadline)||s->deferred_developer_deadline<0)){
      Close(s,"Native developer Debugger pause has no valid original budget");
    }
  }
  bool Current(const std::shared_ptr<Session>& s)const{auto it=sessions_.find(s->id);return !closed_&&it!=sessions_.end()&&it->second==s;}
  bool Expired(double deadline)const{return deadline>0&&hooks_.now()>=deadline;}
  // Moving a small libc++ std::function can leave its source callable. Consume
  // the stored completion before invocation, including through reentrant close.
  static void Finish(Ready& ready,bool ok,const std::string& error){auto reply=std::exchange(ready,{});if(reply)reply(ok,error);}
  void Wake(){if(hooks_.wake)hooks_.wake();}
  uint64_t Next(){return identity_>=9007199254740991ULL?0:++identity_;}
  void Close(const std::shared_ptr<Session>& s,const std::string& error){if(!Current(s)||s->closing)return;s->closing=true;std::vector<std::shared_ptr<Lease>> leases;for(const auto& [id,l]:s->leases)leases.push_back(l);for(const auto& l:leases){Finish(l->ready,false,error);Finish(l->arm_ready,false,error);Finish(l->release_ready,false,error);}if(s->capture)Finish(s->capture->reply,false,error);if(hooks_.close_channel)hooks_.close_channel(s->id,error);}
  void Fail(const std::shared_ptr<Lease>& lease,const std::string& error){
    lease->releasing=true;if(!lease->cleanup)lease->cleanup=(lease->deadline>0?lease->deadline:hooks_.now())+30000;
    Finish(lease->ready,false,error);Finish(lease->arm_ready,false,error);
  }
  std::pair<std::shared_ptr<Session>,std::shared_ptr<Lease>> Find(uint64_t id)const{
    auto found=owners_.find(id);if(found==owners_.end())return {};
    auto lease=found->second->leases.find(id);if(lease==found->second->leases.end())return {};
    return {found->second,lease->second};
  }
  double Budget(const std::shared_ptr<Session>& s)const{
    double deadline=0;for(const auto& [id,l]:s->leases){const auto value=l->releasing?l->cleanup:((l->ready||l->arm_ready)?l->deadline:0);if(value>0&&(!deadline||value<deadline))deadline=value;}
    return deadline?deadline:hooks_.now()+30000;
  }
  void Send(const std::shared_ptr<Session>& s,const std::string& method,Dict params,double deadline,Reply done){
    if(!Current(s)||s->busy||s->closing)return;
    if(calls_.size()+raw_calls_.size()>=kPhysical)return;
    const auto token=Next();if(!token){Close(s,"Native Debugger receipt identity exhausted");return;}
    s->busy=true;calls_.emplace(token,Call{s,method,(deadline>0?deadline:hooks_.now()+30000)+30000,std::move(done)});
    auto weak=weak_from_this();Reply reply=[weak,token](bool ok,Dict data,const std::string& error){if(auto self=weak.lock()){
      auto found=self->calls_.find(token);if(found==self->calls_.end())return;auto call=std::move(found->second);self->calls_.erase(found);call.session->busy=false;
      if(self->Current(call.session)&&call.done)call.done(ok,data,error);
      self->Tick(self->hooks_.now());
    }};
    if(!hooks_.send(s->id,method,params,deadline,reply)){reply(false,nullptr,"Native Debugger dispatch was not accepted");}Wake();
  }
  void Acquire(const std::string& session,uint64_t id,Kind kind,double deadline,Ready ready,DeveloperDeadline developer_deadline={},DeveloperPaused developer_paused={},DeveloperExpired developer_expired={},DeveloperEligibility developer_eligible={}){
    if(!ready)return;
    if((kind==Kind::Developer&&(!developer_deadline||!developer_paused||!developer_expired))||closed_||!id||id<=last_lease_||owners_.contains(id)||session.size()>128||!std::isfinite(deadline)||deadline<0||Expired(deadline)||owners_.size()>=kLeases){ready(false,"Invalid, duplicate, expired or full native Debugger lease");return;}
    if(raw_.contains(session)){ready(false,"Native Debugger lease conflicts with raw Inspector ownership; an actual uncontended Debugger.disable ACK is required");return;}
    auto found=sessions_.find(session);if(found==sessions_.end()){
      if(sessions_.size()>=kSessions){ready(false,"Native Debugger session capacity exceeded");return;}
      auto s=std::make_shared<Session>();s->id=session;found=sessions_.emplace(session,std::move(s)).first;
    }
    const auto s=found->second;if(s->closing){ready(false,"Native Debugger channel closure is pending");return;}
    for(const auto& [key,l]:s->leases)if(l->kind==kind&&!l->releasing){ready(false,"Native Debugger session already has this consumer kind");return;}
    auto l=std::make_shared<Lease>();l->id=id;l->kind=kind;l->deadline=deadline;l->ready=std::move(ready);l->developer_deadline=std::move(developer_deadline);l->developer_paused=std::move(developer_paused);l->developer_expired=std::move(developer_expired);l->developer_eligible=std::move(developer_eligible);s->leases.emplace(id,l);owners_.emplace(id,s);last_lease_=id;Pump();Wake();
  }
  void Arm(uint64_t id,Dict params,Paused paused,Ready ready){
    if(!ready){return;}auto [s,l]=Find(id);
    if(!s||!l||l->kind!=Kind::Factory||l->releasing||!l->acquired||l->arm||!l->breakpoint.empty()||!params||!paused||Expired(l->deadline)){
      ready(false,"Invalid or expired native Debugger factory arm");return;
    }
    // Factory breakpoint provenance is verified again by the consumer before
    // capture; this bounded channel permits only an exact URL+line breakpoint.
    CefDictionaryValue::KeyList keys;params->GetKeys(keys);const auto url=params->GetString("url").ToString();
    if(keys.size()!=3||params->GetType("url")!=VTYPE_STRING||url.empty()||url.size()>256||params->GetType("lineNumber")!=VTYPE_INT||params->GetInt("lineNumber")<0||params->GetType("columnNumber")!=VTYPE_INT||params->GetInt("columnNumber")<0){ready(false,"Invalid exact native Debugger breakpoint parameters");return;}
    l->arm=params->Copy(false);l->paused=std::move(paused);l->arm_ready=std::move(ready);Pump();Wake();
  }
  void Release(uint64_t id,double deadline,Ready ready){
    if(!ready){return;}auto [s,l]=Find(id);
    if(!s||!l||l->release_requested||!std::isfinite(deadline)||deadline<0){ready(false,"Invalid native Debugger lease release");return;}
    l->releasing=true;l->release_requested=true;l->cleanup=deadline>0?deadline:hooks_.now()+30000;l->release_ready=std::move(ready);
    Finish(l->ready,false,"Native Debugger lease released during acquisition");Finish(l->arm_ready,false,"Native Debugger lease released during breakpoint arm");
    Pump();Wake();
  }
  uint64_t Hold(uint64_t id){
    auto [s,l]=Find(id);if(!s||!l||(l->kind!=Kind::Factory&&l->kind!=Kind::Developer)||!l->acquired||s->closing||receipts_.size()>=kReceipts||(l->releasing&&!l->holds))return 0;
    auto receipt=Next();if(!receipt)return 0;receipts_.emplace(receipt,Receipt{s,l});++l->holds;Wake();return receipt;
  }
  void Retire(uint64_t receipt){auto found=receipts_.find(receipt);if(found==receipts_.end())return;--found->second.lease->holds;receipts_.erase(found);Pump();Wake();}
  void ResumeCapture(const std::shared_ptr<Session>& s,uint64_t capture,Ready ready){
    if(!Current(s)||!s->capture||s->capture->id!=capture||s->capture->requested){if(ready)ready(false,"Native Debugger pause resume is stale or already requested");return;}
    s->capture->requested=true;s->capture->reply=std::move(ready);Pump();Wake();
  }
  void ContinueCapture(const std::shared_ptr<Session>& s,uint64_t capture,Continuation kind,double deadline,Ready ready){
    if(!Current(s)||!s->capture||s->capture->id!=capture||s->capture->requested||!std::isfinite(deadline)||deadline<0||Expired(deadline)){
      if(ready){ready(false,"Native developer Debugger continuation is stale, duplicate or expired");}return;
    }
    std::string method;
    switch(kind){case Continuation::Resume:method="Debugger.resume";break;case Continuation::StepOver:method="Debugger.stepOver";break;case Continuation::StepInto:method="Debugger.stepInto";break;case Continuation::StepOut:method="Debugger.stepOut";break;default:if(ready)ready(false,"Unsupported native developer Debugger continuation");return;}
    const auto owner=s->leases.find(s->capture->lease);
    if(owner==s->leases.end()||owner->second->kind!=Kind::Developer||owner->second->releasing){if(ready)ready(false,"Native developer Debugger pause owner is unavailable");return;}
    s->capture->continuation=std::move(method);s->capture->continuation_deadline=deadline;
    s->capture->requested=true;s->capture->reply=std::move(ready);Pump();Wake();
  }
  bool HasOwnedHit(const std::shared_ptr<Session>&s,Dict params)const{
    auto hits=params?params->GetList("hitBreakpoints"):nullptr;
    if(!hits)return false;
    for(size_t i=0;i<std::min<size_t>(hits->GetSize(),64);++i)if(hits->GetType(i)==VTYPE_STRING){const auto hit=hits->GetString(i).ToString();for(const auto&[id,lease]:s->leases)if(!lease->releasing&&!lease->breakpoint.empty()&&lease->breakpoint==hit)return true;}
    return false;
  }
  bool Event(const std::string& method,Dict params,const std::string& session,std::optional<double> original=std::nullopt,bool expiry_reported=false,std::shared_ptr<PausePacket> retained_packet={},std::optional<double> original_developer=std::nullopt,uint64_t original_developer_id=0,std::optional<DeveloperPauseDisposition> original_disposition=std::nullopt){
    if(method!="Debugger.paused"&&method!="Debugger.resumed"){return false;}auto found=sessions_.find(session);if(found==sessions_.end()||(!params&&method=="Debugger.paused"))return false;auto s=found->second;
    if(method=="Debugger.resumed"){
      if(!s->capture)return true;
      const bool pending_resume=std::any_of(calls_.begin(),calls_.end(),[&](const auto& item){return item.second.session==s&&(item.second.method=="Debugger.resume"||item.second.method=="Debugger.stepOver"||item.second.method=="Debugger.stepInto"||item.second.method=="Debugger.stepOut");});
      if(pending_resume){s->capture->actual_resumed=true;return true;}
      // This event proves execution resumed, but not that any queued factory
      // command retired or captured intrinsics before author execution.
      auto pause=std::move(*s->capture);s->capture.reset();
      Finish(pause.reply,false,"Native Debugger execution resumed before owned capture completed");
      if(pause.lease)ExpirePause(pause.lease,pause.packet?pause.packet->data:nullptr,"Native Debugger execution resumed before owned capture completed");
      Pump();Wake();return true;
    }
    if(s->deferred_pause||s->deferred_rejected){Close(s,"Native Debugger delivered more than one deferred pause");return true;}
    if(s->capture){
      if(s->capture->actual_resumed){
        // Only an actual owned breakpoint needs document capture authority.
        // An unowned author pause resumes under a bounded background budget.
        const auto deadline=HasOwnedHit(s,params)&&hooks_.pause_deadline?hooks_.pause_deadline(session,params):hooks_.now()+30000;
        if(!std::isfinite(deadline)||deadline<0){Close(s,"Native Debugger next-document pause has no valid original budget");return true;}
        s->deferred_pause=Packet(params);s->deferred_rejected=!s->deferred_pause;s->deferred_deadline=deadline;s->deferred_expired=false;DeferredDeveloper(s,params);if(!Current(s))return true;if(s->deferred_rejected)RejectedPause(s,params,"Native Debugger pause metadata capacity exceeded before copy");Wake();return true;
      }
      Close(s,"Native Debugger delivered a second pause before actual execution resumed");return true;
    }
    const bool pending_arm=std::any_of(calls_.begin(),calls_.end(),[&](const auto& item){return item.second.session==s&&item.second.method=="Debugger.setBreakpointByUrl";});
    if(pending_arm){
      const auto deadline=hooks_.pause_deadline?hooks_.pause_deadline(session,params):hooks_.now()+30000;
      if(!std::isfinite(deadline)||deadline<0){Close(s,"Native Debugger pending-arm pause has no valid document budget");return true;}
      s->deferred_pause=Packet(params);s->deferred_rejected=!s->deferred_pause;s->deferred_deadline=deadline;s->deferred_expired=false;DeferredDeveloper(s,params);if(!Current(s))return true;if(s->deferred_rejected)RejectedPause(s,params,"Native Debugger pause metadata capacity exceeded before copy");Wake();return true;
    }
    auto hits=params->GetList("hitBreakpoints");std::shared_ptr<Lease> owner;
    if(hits)for(size_t i=0;i<std::min<size_t>(hits->GetSize(),64);++i)if(hits->GetType(i)==VTYPE_STRING){const auto hit=hits->GetString(i).ToString();for(const auto& [id,l]:s->leases)if(!l->releasing&&!l->breakpoint.empty()&&l->breakpoint==hit){if(owner&&owner!=l){Close(s,"Native Debugger pause has multiple owned breakpoint consumers");return true;}owner=l;}}
    std::shared_ptr<Lease>rejected;
    if(!owner){const auto developer=Developer(s);if(developer&&(!original||developer->id==original_developer_id)){
      auto disposition=original_disposition.value_or(DeveloperPauseDisposition::Owned);
      // A foreign/unsupported deferred packet cannot become owned by a later
      // inventory update. A formerly owned candidate must still be current.
      if(!original_disposition||disposition==DeveloperPauseDisposition::Owned)disposition=Eligible(s,developer,params);
      if(!Current(s))return true;
      if(disposition==DeveloperPauseDisposition::Owned)owner=developer;
      else if(disposition==DeveloperPauseDisposition::Unsupported)rejected=developer;
    }}
    const auto capture=Next();if(!capture){Close(s,"Native Debugger pause identity exhausted");return true;}
    const auto deadline=owner&&owner->kind==Kind::Developer?
      (original_developer?*original_developer:owner->developer_deadline(params)):
      original?*original:owner&&hooks_.pause_deadline?hooks_.pause_deadline(session,params):hooks_.now()+30000;
    if(!std::isfinite(deadline)||deadline<0){Close(s,"Native Debugger pause has no valid original document budget");return true;}
    // Count only actual Inspector pauses classified by this native arbiter.
    // Deferred replay is the same retained packet, not another browser event.
    if(!owner){ObservePause(unowned_pause_events_);if(original)ObservePause(deferred_unowned_pause_events_);}
    auto packet=owner?(retained_packet?retained_packet:Packet(params)):std::shared_ptr<PausePacket>();
    if(owner&&!packet){const auto rejected=owner->id;owner.reset();ExpirePause(rejected,params,"Native Debugger pause metadata capacity exceeded before copy");}
    s->capture=Capture{packet,capture,owner?owner->id:0,deadline,!owner,false,false,{}};
    if(rejected){ExpirePause(rejected->id,params,"Native scoped debugger pause has unsupported or stale frame ownership");if(!Current(s))return true;}
    if(owner&&Expired(deadline)){s->capture->expired=true;s->capture->requested=true;if(!expiry_reported||owner->kind==Kind::Developer)ExpirePause(owner->id,packet->data,"Native Debugger pause was already expired before capture admission");}
    else if(owner&&owner->kind==Kind::Developer){auto weak=weak_from_this();owner->developer_paused(capture,packet->data,deadline,[weak,s,capture](Continuation kind,double budget,Ready ready){if(auto self=weak.lock())self->ContinueCapture(s,capture,kind,budget,std::move(ready));else if(ready)ready(false,"Native Debugger arbiter was destroyed");});}
    else if(owner){auto weak=weak_from_this();owner->paused(packet->data,deadline,[weak,s,capture](Ready ready){if(auto self=weak.lock())self->ResumeCapture(s,capture,std::move(ready));else if(ready)ready(false,"Native Debugger arbiter was destroyed");});}
    Pump();Wake();return true;
  }
  bool DesiredSkip(const std::shared_ptr<Session>& s)const{
    for(const auto& [id,l]:s->leases){if((l->kind==Kind::Factory||l->kind==Kind::Developer)&&(!l->releasing||!l->breakpoint.empty()||l->holds||(s->capture&&s->capture->lease==id)))return false;}return true;
  }
  bool BoundedScriptCache(const std::shared_ptr<Session>& s)const{
    for(const auto&[id,l]:s->leases)if(l->kind!=Kind::Factory&&
        (!l->releasing||l->holds||(s->capture&&s->capture->lease==id)))return false;
    return true;
  }
  void Enable(const std::shared_ptr<Session>& s){
    const bool bounded=BoundedScriptCache(s);auto params=CefDictionaryValue::Create();
    if(bounded)params->SetInt("maxScriptsCacheSize",kFactoryScriptCacheBytes);
    // V8 applies maxScriptsCacheSize before its already-enabled early return.
    // Reconfiguration preserves breakpoints, paused execution, and receipt pins.
    Send(s,"Debugger.enable",params,Budget(s),[this,s,bounded](bool ok,Dict,const std::string&error){
      if(ok){s->enabled=true;s->bounded_script_cache=bounded;}
      else Close(s,"Actual native Debugger script-cache policy failed: "+error);
    });
  }
  void PumpSession(const std::shared_ptr<Session>& s){
    if(!Current(s)||s->busy||s->closing)return;
    if(s->deferred_pause||s->deferred_rejected){auto packet=std::move(s->deferred_pause);const bool rejected=s->deferred_rejected;s->deferred_rejected=false;const auto deadline=s->deferred_deadline;const bool expired=s->deferred_expired;s->deferred_expired=false;const auto developer=s->deferred_developer;const auto developer_deadline=s->deferred_developer_deadline;const auto disposition=s->deferred_disposition;s->deferred_developer=0;s->deferred_developer_deadline=0;
      if(rejected){const auto capture=Next();if(!capture){Close(s,"Native Debugger pause identity exhausted");return;}s->capture=Capture{nullptr,capture,0,deadline,true,false,false,{}};}
      else Event("Debugger.paused",packet->data,s->id,deadline,expired,packet,developer?std::optional<double>(developer_deadline):std::nullopt,developer,disposition);
      if(rejected&&expired&&s->capture){s->capture->expired=true;s->capture->requested=true;}again_=true;return;}
    const bool active=std::any_of(s->leases.begin(),s->leases.end(),[](const auto&entry){return !entry.second->releasing;});
    if(s->enabled&&active&&(!s->bounded_script_cache||*s->bounded_script_cache!=BoundedScriptCache(s))){Enable(s);return;}
    if(s->capture){
      // Profiler shutdown can finish while explicit developer execution stays
      // paused. Dropping that unrelated Coverage lease needs no Debugger state
      // mutation: the real capture still owns enabled/skip=false and its ACKs.
      // Do not let coverage.stop wait for a later user resume of that pause.
      std::vector<std::shared_ptr<Lease>> coverage_released;
      for(auto it=s->leases.begin();it!=s->leases.end();){const auto lease=it->second;
        if(lease->kind==Kind::Coverage&&lease->releasing&&lease->id!=s->capture->lease&&!lease->holds&&lease->breakpoint.empty()){
          owners_.erase(lease->id);coverage_released.push_back(lease);it=s->leases.erase(it);
        }else ++it;
      }
      for(const auto&lease:coverage_released)Finish(lease->release_ready,true,"");
      if(!Current(s)||s->busy||s->closing||!s->capture)return;
      // An actual enabled/skip=false state already satisfies a new Coverage
      // consumer alongside the paused factory/developer. No new policy ACK is
      // needed and no user resume should be required to start the Profiler.
      if(s->enabled&&s->skip&&!*s->skip){
        std::vector<std::shared_ptr<Lease>> coverage_ready;
        for(const auto&[id,lease]:s->leases)if(lease->kind==Kind::Coverage&&!lease->acquired&&!lease->releasing)coverage_ready.push_back(lease);
        for(const auto&lease:coverage_ready){
          if(Expired(lease->deadline))Fail(lease,"Native Debugger acquisition exceeded its original deadline");
          else{lease->acquired=true;lease->deadline=0;Finish(lease->ready,true,"");}
          if(!Current(s)||s->busy||s->closing||!s->capture)return;
        }
      }
      const auto acquired_owner=s->leases.find(s->capture->lease);
      if(acquired_owner!=s->leases.end()){
        const auto lease=acquired_owner->second;
        if(lease->kind==Kind::Developer&&!lease->acquired&&!lease->releasing&&!s->capture->requested){
          if(!s->enabled){Enable(s);return;}
          if(!s->skip||*s->skip){auto params=CefDictionaryValue::Create();params->SetBool("skip",false);Send(s,"Debugger.setSkipAllPauses",params,Budget(s),[this,s](bool ok,Dict,const std::string&error){if(ok)s->skip=false;else Close(s,error);});return;}
          if(Expired(lease->deadline))Fail(lease,"Native developer Debugger acquisition exceeded its original deadline");
          else{lease->acquired=true;lease->deadline=0;Finish(lease->ready,true,"");}
          again_=true;return;
        }
      }
      if(!s->capture->requested){auto owner=s->leases.find(s->capture->lease);if(owner!=s->leases.end()&&owner->second->releasing&&!owner->second->holds)s->capture->requested=true;else return;}const auto id=s->capture->id;const auto deadline=(s->capture->deadline>0?s->capture->deadline:hooks_.now())+30000;
      const auto method=s->capture->continuation;const auto continuation_deadline=s->capture->continuation_deadline;
      Send(s,method,nullptr,continuation_deadline?std::min(deadline,continuation_deadline):deadline,[this,s,id](bool ok,Dict,const std::string& error){if(!s->capture||s->capture->id!=id)return;auto pause=std::move(*s->capture);s->capture.reset();const bool expired=pause.expired||Expired(pause.deadline)||Expired(pause.continuation_deadline);if(pause.reply)pause.reply(ok&&!expired,expired?"Native Debugger pause exceeded its original document deadline":error);if(!ok)Close(s,"Actual native Debugger resume failed: "+error);});return;
    }
    // Removal cannot race a genuine factory evaluation or release-group ACK.
    for(const auto& [id,l]:s->leases)if(l->releasing&&!l->holds&&!l->breakpoint.empty()){
      auto params=CefDictionaryValue::Create();params->SetString("breakpointId",l->breakpoint);Send(s,"Debugger.removeBreakpoint",params,l->cleanup,[this,s,l](bool ok,Dict,const std::string& error){if(ok){l->breakpoint.clear();l->paused={};}else Close(s,"Actual native Debugger breakpoint removal failed: "+error);});return;
    }
    bool desired=false,held=false;for(const auto& [id,l]:s->leases){desired=desired||!l->releasing;held=held||l->holds||!l->breakpoint.empty();}
    if(!desired&&!held){
      if(s->enabled){Send(s,"Debugger.disable",nullptr,Budget(s),[this,s](bool ok,Dict,const std::string& error){if(ok){s->enabled=false;s->skip.reset();s->bounded_script_cache.reset();}else Close(s,"Actual native Debugger disable failed: "+error);});return;}
      std::vector<std::shared_ptr<Lease>> leases;for(const auto& [id,l]:s->leases){owners_.erase(id);leases.push_back(l);}s->leases.clear();sessions_.erase(s->id);for(const auto& l:leases)Finish(l->release_ready,true,"");return;
    }
    if(!s->enabled){Enable(s);return;}
    const bool skip=DesiredSkip(s);if(!s->skip||*s->skip!=skip){auto params=CefDictionaryValue::Create();params->SetBool("skip",skip);Send(s,"Debugger.setSkipAllPauses",params,Budget(s),[this,s,skip](bool ok,Dict,const std::string& error){if(ok)s->skip=skip;else Close(s,"Actual native Debugger skip policy failed: "+error);});return;}
    std::vector<std::shared_ptr<Lease>> leases;for(const auto& [id,l]:s->leases)leases.push_back(l);
    for(const auto& l:leases){
      if(!Current(s))return;
      if(l->releasing){if(!l->holds&&l->breakpoint.empty()){s->leases.erase(l->id);owners_.erase(l->id);Finish(l->release_ready,true,"");again_=true;}continue;}
      if(!l->acquired){l->acquired=true;if(Expired(l->deadline)){Fail(l,"Native Debugger acquisition exceeded its original deadline");again_=true;continue;}if(l->kind==Kind::Coverage||l->kind==Kind::Developer)l->deadline=0;Finish(l->ready,true,"");if(!Current(s)||s->busy)return;}
      if(l->arm&&l->arm_ready){
        if(Expired(l->deadline)){Fail(l,"Native Debugger breakpoint arm exceeded its original deadline");again_=true;continue;}
        Send(s,"Debugger.setBreakpointByUrl",l->arm,l->deadline,[this,s,l](bool ok,Dict data,const std::string& error){
          if(ok){const auto id=data?data->GetString("breakpointId").ToString():std::string();if(id.empty()||id.size()>512){Close(s,"Actual native Debugger breakpoint identity is invalid");return;}l->breakpoint=id;l->arm=nullptr;}
          if(!ok||Expired(l->deadline)||l->releasing){Fail(l,!ok?error:"Native Debugger breakpoint arm exceeded its original deadline");return;}
          l->deadline=0;Finish(l->arm_ready,true,"");
        });return;
      }
    }
  }
  uint64_t BeginRaw(const std::string& session,const std::string& method,double deadline){
    if(closed_||session.size()>128||method.rfind("Debugger.",0)!=0||method.size()>256||sessions_.contains(session)||!std::isfinite(deadline)||deadline<0||Expired(deadline)||calls_.size()+raw_calls_.size()>=kPhysical)return 0;
    const auto existing=raw_.find(session);if(existing!=raw_.end()&&existing->second.closing)return 0;
    if(existing==raw_.end()&&raw_.size()>=kSessions)return 0;
    const auto receipt=Next();if(!receipt)return 0;
    ++raw_[session].pending;raw_calls_.emplace(receipt,RawCall{session,method,(deadline>0?deadline:hooks_.now()+30000)+30000});Wake();return receipt;
  }
  void EndRaw(uint64_t receipt,bool ok,bool admitted){
    const auto found=raw_calls_.find(receipt);if(found==raw_calls_.end())return;const auto call=found->second;raw_calls_.erase(found);
    const auto state=raw_.find(call.session);if(state==raw_.end())return;--state->second.pending;
    if(admitted){if(ok&&call.method=="Debugger.disable"&&!state->second.pending)state->second.tainted=false;else state->second.tainted=true;}
    if(!state->second.pending&&!state->second.tainted&&!state->second.closing){raw_.erase(state);}Pump();Wake();
  }
  void Pump(){if(closed_)return;if(classifying_){again_=true;return;}if(pumping_){again_=true;return;}pumping_=true;do{again_=false;std::vector<std::shared_ptr<Session>> sessions;for(const auto& [id,s]:sessions_)sessions.push_back(s);for(const auto& s:sessions)PumpSession(s);}while(again_&&!closed_);pumping_=false;}
  void Tick(double now){
    std::vector<std::shared_ptr<Session>> sessions;for(const auto& [id,s]:sessions_)sessions.push_back(s);
    for(const auto& s:sessions){std::vector<std::shared_ptr<Lease>> leases;for(const auto& [id,l]:s->leases)leases.push_back(l);for(const auto& l:leases){if((l->ready||l->arm_ready||(l->kind==Kind::Factory&&l->breakpoint.empty()&&!l->releasing))&&l->deadline>0&&now>=l->deadline)Fail(l,"Native Debugger startup exceeded its original deadline");if(l->releasing&&l->cleanup>0&&now>=l->cleanup){Finish(l->release_ready,false,"Native Debugger physical cleanup exceeded its watchdog");Close(s,"Native Debugger physical cleanup did not retire by its watchdog");}}
      if(!Current(s)||s->closing)continue;
      if(s->deferred_pause&&!s->deferred_expired&&s->deferred_deadline>0&&now>=s->deferred_deadline){
        s->deferred_expired=true;auto packet=s->deferred_pause;
        for(const auto& l:leases)if(Current(s)&&!s->closing&&l->kind==Kind::Factory&&!l->releasing&&hooks_.pause_expired)hooks_.pause_expired(l->id,packet->data,"Native Debugger pause exceeded its original document deadline before arm ACK");
      }
      if(!Current(s)||s->closing)continue;
      const double capture_deadline=s->capture?(s->capture->deadline&&s->capture->continuation_deadline?std::min(s->capture->deadline,s->capture->continuation_deadline):s->capture->deadline?s->capture->deadline:s->capture->continuation_deadline):0;
      if(s->capture&&!s->capture->expired&&capture_deadline>0&&now>=capture_deadline){s->capture->expired=true;const auto capture=s->capture->id,lease=s->capture->lease;const bool actual_resumed=s->capture->actual_resumed;auto packet=s->capture->packet;s->capture->requested=true;auto reply=std::move(s->capture->reply);Finish(reply,false,"Native Debugger pause exceeded its original document deadline");if(Current(s)&&s->capture&&s->capture->id==capture&&lease&&!actual_resumed)ExpirePause(lease,packet?packet->data:nullptr,"Native Debugger pause exceeded its original document deadline");}
    }
    std::vector<std::string> raw_expired;for(const auto& [id,call]:raw_calls_)if(now>=call.retirement){auto state=raw_.find(call.session);if(state!=raw_.end()&&!state->second.closing){state->second.closing=true;raw_expired.push_back(call.session);}}
    for(const auto& session:raw_expired)if(hooks_.close_channel)hooks_.close_channel(session,"Raw Debugger accepted command exceeded its physical retirement grace");
    std::vector<std::shared_ptr<Session>> expired_calls;for(const auto& [id,call]:calls_){if(now>=call.retirement)expired_calls.push_back(call.session);}for(const auto& session:expired_calls)Close(session,"Native Debugger accepted command exceeded its physical retirement grace");Pump();Wake();
  }
  void Detached(const std::string& session){
    raw_.erase(session);for(auto it=raw_calls_.begin();it!=raw_calls_.end();)if(it->second.session==session)it=raw_calls_.erase(it);else ++it;
    auto found=sessions_.find(session);if(found==sessions_.end())return;auto s=found->second;sessions_.erase(found);
    for(auto it=calls_.begin();it!=calls_.end();)if(it->second.session==s)it=calls_.erase(it);else ++it;
    for(auto it=receipts_.begin();it!=receipts_.end();)if(it->second.session==s){--it->second.lease->holds;it=receipts_.erase(it);}else ++it;
    auto leases=std::move(s->leases);for(const auto& [id,l]:leases)owners_.erase(id);auto capture=std::move(s->capture);s->capture.reset();s->deferred_pause.reset();s->deferred_rejected=false;
    for(const auto& [id,l]:leases){Finish(l->ready,false,"Actual native Debugger session closed");Finish(l->arm_ready,false,"Actual native Debugger session closed");Finish(l->release_ready,false,"Actual native Debugger session closed");}if(capture)Finish(capture->reply,false,"Actual native Debugger session closed");Pump();Wake();
  }
  void Closed(){if(closed_)return;closed_=true;raw_.clear();raw_calls_.clear();std::vector<std::string> ids;for(const auto& [id,s]:sessions_)ids.push_back(id);for(const auto& id:ids)Detached(id);}
};
DebuggerDomains::DebuggerDomains(Hooks hooks):impl_(std::make_shared<Impl>(std::move(hooks))){}
DebuggerDomains::~DebuggerDomains(){impl_->Closed();}
void DebuggerDomains::Acquire(const std::string& session,uint64_t lease,Kind kind,double deadline,Ready ready){impl_->Acquire(session,lease,kind,deadline,std::move(ready));}
void DebuggerDomains::AcquireDeveloper(const std::string& session,uint64_t lease,double deadline,DeveloperDeadline pause_deadline,DeveloperPaused paused,DeveloperExpired expired,Ready ready){impl_->Acquire(session,lease,Kind::Developer,deadline,std::move(ready),std::move(pause_deadline),std::move(paused),std::move(expired));}
void DebuggerDomains::AcquireScopedDeveloper(const std::string&session,uint64_t lease,double deadline,DeveloperDeadline pause_deadline,DeveloperPaused paused,DeveloperExpired rejected,DeveloperEligibility eligible,Ready ready){
  if(!eligible){if(ready)ready(false,"Native scoped debugger requires an exact pause eligibility resolver");return;}
  impl_->Acquire(session,lease,Kind::Developer,deadline,std::move(ready),std::move(pause_deadline),std::move(paused),std::move(rejected),std::move(eligible));
}
void DebuggerDomains::Arm(uint64_t lease,Dict params,Paused paused,Ready ready){impl_->Arm(lease,params,std::move(paused),std::move(ready));}
void DebuggerDomains::Release(uint64_t lease,double deadline,Ready ready){impl_->Release(lease,deadline,std::move(ready));}
uint64_t DebuggerDomains::Hold(uint64_t lease){return impl_->Hold(lease);}
void DebuggerDomains::Retire(uint64_t receipt){impl_->Retire(receipt);}
bool DebuggerDomains::HasLease(uint64_t lease)const{return impl_->owners_.contains(lease);}
bool DebuggerDomains::IsPaused(uint64_t lease,uint64_t pause)const{auto[s,l]=impl_->Find(lease);return s&&l&&!l->releasing&&s->capture&&s->capture->lease==lease&&s->capture->id==pause&&!s->capture->requested&&!s->capture->actual_resumed&&!s->capture->expired&&!impl_->Expired(s->capture->deadline);}
bool DebuggerDomains::Event(const std::string& method,Dict params,const std::string& session){return impl_->Event(method,params,session);}
uint64_t DebuggerDomains::BeginRaw(const std::string& session,const std::string& method,double deadline){return impl_->BeginRaw(session,method,deadline);}
void DebuggerDomains::EndRaw(uint64_t receipt,bool ok,bool admitted){impl_->EndRaw(receipt,ok,admitted);}
bool DebuggerDomains::Conflicts(const std::string& session,const std::string& method)const{return method.rfind("Debugger.",0)==0&&impl_->sessions_.contains(session);}
void DebuggerDomains::Tick(double now){impl_->Tick(now);}
void DebuggerDomains::Detached(const std::string& session){impl_->Detached(session);}
void DebuggerDomains::Closed(){impl_->Closed();}
bool DebuggerDomains::NeedsTick()const{for(const auto& [id,call]:impl_->raw_calls_){const auto state=impl_->raw_.find(call.session);if(state!=impl_->raw_.end()&&!state->second.closing)return true;}for(const auto& [id,call]:impl_->calls_)if(!call.session->closing)return true;for(const auto& [id,s]:impl_->sessions_){if(s->closing)continue;if(s->capture&&s->capture->deadline>0&&!s->capture->expired)return true;for(const auto& [key,l]:s->leases)if(((l->ready||l->arm_ready||(l->kind==Kind::Factory&&l->breakpoint.empty()&&!l->releasing))&&l->deadline>0)||(l->releasing&&l->cleanup>0))return true;}return false;}
size_t DebuggerDomains::PhysicalCount()const{size_t pauses=0;for(const auto& [id,s]:impl_->sessions_)pauses+=s->capture.has_value()||bool(s->deferred_pause)||s->deferred_rejected;return impl_->calls_.size()+impl_->receipts_.size()+impl_->raw_calls_.size()+pauses;}
void DebuggerDomains::Diagnostics(Dict output)const{size_t bounded_caches=0,historical_caches=0;for(const auto&[id,s]:impl_->sessions_)if(s->enabled&&s->bounded_script_cache){if(*s->bounded_script_cache)++bounded_caches;else ++historical_caches;}output->SetInt("nativeDebuggerFactoryScriptCacheSessions",int(bounded_caches));output->SetInt("nativeDebuggerHistoricalScriptCacheSessions",int(historical_caches));size_t factory_receipts=0,developer_receipts=0;for(const auto&[id,receipt]:impl_->receipts_){if(receipt.lease->kind==Kind::Developer)++developer_receipts;else ++factory_receipts;}size_t pauses=0;for(const auto& [id,s]:impl_->sessions_)pauses+=s->capture.has_value()||bool(s->deferred_pause)||s->deferred_rejected;output->SetInt("nativeDebuggerUnownedPauses",impl_->unowned_pause_events_);output->SetInt("nativeDebuggerDeferredUnownedPauses",impl_->deferred_unowned_pause_events_);output->SetInt("nativeDebuggerPausePackets",int(impl_->packet_budget_->packets));output->SetInt("nativeDebuggerRawOwners",int(impl_->raw_.size()));output->SetInt("nativeDebuggerRawPhysical",int(impl_->raw_calls_.size()));output->SetInt("nativeDebuggerSessions",int(impl_->sessions_.size()));output->SetInt("nativeDebuggerLeases",int(impl_->owners_.size()));output->SetInt("nativeDebuggerPhysical",int(impl_->calls_.size()));output->SetInt("nativeDebuggerFactoryReceipts",int(factory_receipts));output->SetInt("nativeDebuggerDeveloperReceipts",int(developer_receipts));output->SetInt("nativeDebuggerPauses",int(pauses));}
} // namespace suitecut
