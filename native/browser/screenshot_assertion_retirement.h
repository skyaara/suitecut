// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <new>
#include <string>
#include <vector>
#include <utility>
namespace suitecut {
// Native-only UI actor. Tickets are issued at real native submission/CPU/IO
// boundaries. Dropping a callback or logical owner NEVER acknowledges a ticket.
// Source supplies actual channel death explicitly, not renderer navigation.
class ScreenshotAssertionRetirement {
 public:
  static constexpr size_t kOwners=16,kPhysical=128;
  enum class Work {Renderer,Restoration,Groups,Pixels,Files,Cleanup,View,Validation};
  enum class Outcome {Pending,Passed,ObservedFailure,Deadline,HardFailure,Canceled};
  struct Identity {int page=0,owner=0;uint64_t generation=0;bool operator==(const Identity&)const=default;};
  struct Entry {
    Identity identity;double original_deadline=0,cleanup_deadline=0;
    bool frozen=false,observed=false,capture_submitted=false,validated=false,cleanup_closed=false,canceled=false,watchdog=false;
    Outcome requested=Outcome::Pending;std::string error;size_t physical=0;
  };
  struct Ticket {uint64_t serial=0;Identity identity;Work kind=Work::Renderer;int command=0;std::string session;bool operator==(const Ticket&)const=default;};
  bool Admit(Identity identity,double original_deadline){
    if(identity.page<1||identity.owner<1||!identity.generation||!std::isfinite(original_deadline)||original_deadline<0||entries_.size()>=kOwners||entries_.contains(identity.owner))return false;
    try{Entry entry;entry.identity=identity;entry.original_deadline=original_deadline;entry.error.reserve(4096);entries_.emplace(identity.owner,std::move(entry));return true;}catch(const std::bad_alloc&){return false;}
  }
  const Entry* Get(Identity identity)const{const auto found=entries_.find(identity.owner);return found!=entries_.end()&&found->second.identity==identity?&found->second:nullptr;}
  bool Contains(int owner)const{return entries_.contains(owner);}
  std::optional<Ticket> Begin(Identity identity,Work kind,int command,const std::string&session,double now){
    auto*entry=Find(identity);if(!entry||entry->cleanup_closed||!std::isfinite(now)||command<0||session.size()>128||tickets_.size()>=kPhysical||serial_==UINT64_MAX||
       (entry->frozen&&(kind==Work::Renderer||kind==Work::Pixels))||
       (entry->frozen&&now>=entry->cleanup_deadline)||
       (!entry->frozen&&entry->original_deadline&&now>=entry->original_deadline))return {};
    if((kind==Work::Renderer||kind==Work::Restoration||kind==Work::Groups)!=(command>0))return {};
    if(kind==Work::Validation&&(!entry->frozen||!entry->capture_submitted||entry->canceled||!entry->error.empty()||!entry->original_deadline||now<entry->original_deadline))return {};
    try{Ticket ticket{serial_+1,identity,kind,command,session};tickets_.emplace(ticket.serial,Pending{ticket,false});serial_=ticket.serial;++entry->physical;return ticket;}catch(const std::bad_alloc&){return {};}
  }
  // A successful submission can acknowledge synchronously before submission
  // returns. Only an actually issued native Page.captureScreenshot is marked.
  bool CaptureSubmitted(Identity identity){auto*entry=Find(identity);if(!entry||entry->frozen)return false;entry->capture_submitted=true;return true;}
  bool Observe(Identity identity,double now){auto*entry=Find(identity);if(!entry||entry->frozen||!std::isfinite(now)||(entry->original_deadline&&now>=entry->original_deadline))return false;entry->observed=true;entry->validated=true;return true;}
  bool Validated(Identity identity){auto*entry=Find(identity);if(!entry)return false;entry->validated=true;return true;}
  bool EnterSubmission(const Ticket&ticket){const auto found=tickets_.find(ticket.serial);if(found==tickets_.end()||found->second.ticket!=ticket||found->second.entered)return false;found->second.entered=true;return true;}
  // Called for the exact real ACK or explicit no-submission rejection. Wrong
  // command/session/generation cannot retire another physical receipt.
  bool Acknowledge(const Ticket&ticket,bool ok,const std::string&error,double now){
    const auto found=tickets_.find(ticket.serial);if(found==tickets_.end()||found->second.ticket!=ticket||!found->second.entered)return false;
    auto*entry=Find(ticket.identity);if(!entry)return false;
    tickets_.erase(found);--entry->physical;if(!ok)Fail(ticket.identity,error.empty()?"Native screenshot physical operation failed":error,now);return true;
  }
  // Explicit withdrawal before CEF/worker submission is different from an ACK.
  // A dropped callback cannot call this; Source invokes it only at its proven
  // pre-dispatch original-clock guard.
  bool WithdrawUnsubmitted(const Ticket&ticket){
    const auto found=tickets_.find(ticket.serial);if(found==tickets_.end()||found->second.ticket!=ticket||found->second.entered)return false;
    auto*entry=Find(ticket.identity);if(!entry)return false;tickets_.erase(found);--entry->physical;return true;
  }
  bool Freeze(Identity identity,bool passed,double now){
    auto*entry=Find(identity);if(!entry||entry->frozen||!std::isfinite(now))return false;
    entry->frozen=true;entry->cleanup_deadline=now+2000;
    const bool expired=entry->original_deadline&&now>=entry->original_deadline;
    if(passed&&expired){entry->requested=Outcome::HardFailure;entry->error="Native screenshot successful comparison publication exceeded its original deadline";}
    else if(passed&&entry->observed)entry->requested=Outcome::Passed;
    else if(entry->observed)entry->requested=Outcome::ObservedFailure;
    else if(expired&&entry->capture_submitted)entry->requested=Outcome::Deadline;
    else{entry->requested=Outcome::HardFailure;entry->error="Native screenshot ended without an admitted comparison or dispatched capture";}
    return true;
  }
  bool Fail(Identity identity,const std::string&error,double now){
    auto*entry=Find(identity);if(!entry||error.empty()||!std::isfinite(now))return false;
    if(entry->error.empty())entry->error.assign(error.data(),std::min<size_t>(4096,error.size()));
    if(!entry->frozen){entry->frozen=true;entry->cleanup_deadline=now+2000;}
    entry->requested=Outcome::HardFailure;return true;
  }
  bool Cancel(Identity identity,const std::string&reason,double now){auto*entry=Find(identity);if(!entry)return false;Fail(identity,reason.empty()?"Native screenshot assertion canceled":reason,now);entry->canceled=true;return true;}
  // Actual restoration dispatch and file admission are closed only once every
  // native dirty resource is represented by an acknowledged/held ticket.
  bool CleanupClosed(Identity identity){auto*entry=Find(identity);if(!entry||!entry->frozen)return false;entry->cleanup_closed=true;return true;}
  Outcome Ready(Identity identity,double now)const{
    const auto*entry=Get(identity);if(!entry||!entry->frozen||entry->physical||!entry->cleanup_closed||!std::isfinite(now))return Outcome::Pending;
    if(entry->canceled)return Outcome::Canceled;
    if(entry->requested==Outcome::Passed&&entry->original_deadline&&now>=entry->original_deadline)return Outcome::HardFailure;
    if(!entry->error.empty())return Outcome::HardFailure;
    if(entry->requested==Outcome::Deadline&&!entry->validated)return Outcome::HardFailure;
    return entry->requested;
  }
  bool Watchdog(Identity identity,double now){auto*entry=Find(identity);if(!entry||!entry->frozen||entry->watchdog||!std::isfinite(now)||now<entry->cleanup_deadline)return false;entry->watchdog=true;return true;}
  bool Retire(Identity identity,double now){const auto*entry=Get(identity);return entry&&Ready(identity,now)!=Outcome::Pending&&entries_.erase(identity.owner);}
  // Exact native OnBeforeClose: this actual transport cannot deliver any more
  // ACKs. CPU/file jobs still require their independent actual completion.
  // Logical Complete, timeout, navigation or dropped closures cannot call.
  void ChannelClosed(int page,double now){
    for(auto&[owner,entry]:entries_)if(entry.identity.page==page){(void)owner;Fail(entry.identity,"Actual native screenshot channel closed",now);entry.cleanup_closed=true;}
    for(auto it=tickets_.begin();it!=tickets_.end();)if(it->second.ticket.identity.page==page&&(it->second.ticket.command>0||it->second.ticket.kind==Work::Cleanup||it->second.ticket.kind==Work::View)){if(auto*entry=Find(it->second.ticket.identity))--entry->physical;it=tickets_.erase(it);}else ++it;
  }
  size_t Physical()const{return tickets_.size();}
  size_t Physical(Identity identity,Work kind)const{size_t count=0;for(const auto&[serial,pending]:tickets_){(void)serial;if(pending.ticket.identity==identity&&pending.ticket.kind==kind)++count;}return count;}
  size_t Size()const{return entries_.size();}
  std::vector<Identity> Owners()const{std::vector<Identity>out;for(const auto&[owner,entry]:entries_){(void)owner;out.push_back(entry.identity);}return out;}
 private:
  Entry* Find(Identity identity){const auto found=entries_.find(identity.owner);return found!=entries_.end()&&found->second.identity==identity?&found->second:nullptr;}
  struct Pending{Ticket ticket;bool entered=false;};
  uint64_t serial_=0;std::map<int,Entry>entries_;std::map<uint64_t,Pending>tickets_;
};
}
