// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
namespace suitecut {
// Native UI-actor bookkeeping only. No Inspector session or ACK is generated.
// Binding hooks must query actual Source/observer/default-context facts.
class FrameDebuggerScopes : public std::enable_shared_from_this<FrameDebuggerScopes> {
 public:
  struct Host {
    std::string source;
    uint64_t observer=0;
    bool operator==(const Host&)const=default;
  };
  struct Channel {
    Host host;
    std::string session;
    bool operator==(const Channel&)const=default;
    bool operator<(const Channel&other)const noexcept {
      return std::tie(host.source,host.observer,session)<std::tie(other.host.source,other.host.observer,other.session);
    }
  };
  struct Pin {
    Channel channel;
    uint64_t attachment=0,document=0;
    std::string frame,loader,unique_context;
    int context=0;
    bool root=false;
    bool operator==(const Pin&)const=default;
  };
  struct Token {
    uint64_t book=0,lease=0;
    bool operator==(const Token&)const=default;
    explicit operator bool()const{return book&&lease;}
  };
  struct Receipt {
    uint64_t book=0,id=0;
    explicit operator bool()const{return book&&id;}
  };
  struct Budget {
    size_t retained()const{return channels.size();}
    size_t physical()const{return calls;}
    size_t payload()const{return bytes;}
   private:
    friend class FrameDebuggerScopes;
    std::map<Channel,Token>channels;
    uint64_t books=0,receipts=0;
    size_t calls=0,bytes=0;
  };
  struct Hooks {
    std::function<double()>now;
    std::function<bool(const Pin&)>current;
    // This must query the shared arbiter's actual HasLease(nativeLease).
    std::function<bool(uint64_t)>lease_present;
    std::function<void(Token,const Pin&)>cleanup;
    // Must request posted host closure; the request never retires ownership.
    std::function<void(const Pin&,const std::string&)>close_channel;
    std::function<void()>wake;
  };
  static std::shared_ptr<Budget>ProcessBudget(){static const auto budget=std::make_shared<Budget>();return budget;}
  static std::shared_ptr<FrameDebuggerScopes>Create(Host host,Hooks hooks,std::shared_ptr<Budget>budget=ProcessBudget()){
    if(!Identity(host.source,128)||!host.observer||!budget||budget->books==UINT64_MAX||!hooks.now||!hooks.current||!hooks.lease_present||!hooks.cleanup||!hooks.close_channel||!hooks.wake)return {};
    auto result=std::shared_ptr<FrameDebuggerScopes>(new FrameDebuggerScopes(std::move(host),std::move(hooks),std::move(budget)));
    result->instance_=++result->budget_->books;return result;
  }
  FrameDebuggerScopes(const FrameDebuggerScopes&)=delete;
  FrameDebuggerScopes&operator=(const FrameDebuggerScopes&)=delete;
  Token Reserve(Pin pin,uint64_t actual_native_lease,double deadline,std::string&error){
    if(closed_||!actual_native_lease||actual_native_lease<=last_lease_||records_.contains(actual_native_lease)||!Valid(pin)||!std::isfinite(deadline)||deadline<0||Expired(deadline)||!hooks_.current(pin)){
      error="Native frame debugger scope has no exact current owner/document or original budget";return {};
    }
    // Exclude the actual host/session even if an old attachment with the same
    // session string is closing. Logical release cannot lend its old pause.
    if(closed_||budget_->channels.contains(pin.channel)||budget_->channels.size()>=16){error="Native debugger channel is already owned/cleaning up or scope capacity is full";return {};}
    auto record=std::make_shared<Record>();record->pin=std::move(pin);record->token={instance_,actual_native_lease};record->deadline=deadline;
    record->bytes=Bytes(record->pin);
    std::map<uint64_t,std::shared_ptr<Record>>staged_records;
    std::map<Channel,Token>staged_channels;
    staged_records.emplace(actual_native_lease,record);staged_channels.emplace(record->pin.channel,record->token);
    record->bytes+=Bytes(staged_channels.begin()->first);
    if(record->bytes>4096||record->bytes>65536-budget_->bytes){error="Native frame debugger scope identity payload exceeds its bound";return {};}
    // Fallible allocations are complete before quota/graph publication.
    records_.insert(staged_records.extract(staged_records.begin()));
    budget_->channels.insert(staged_channels.extract(staged_channels.begin()));
    budget_->bytes+=record->bytes;last_lease_=actual_native_lease;physical_self_=shared_from_this();return record->token;
  }
  // Bracket the actual AcquireDeveloper call, including synchronous refusal or
  // reentrant Ready failure. Returning from dispatch is never an engine ACK.
  bool BeginAcquire(Token token){
    auto record=Find(token);if(!record||record->issued||record->closing)return false;
    if(Expired(record->deadline)||!hooks_.current(record->pin)){Close(token);return false;}
    record->issued=true;record->dispatching=true;return true;
  }
  void AcquireReturned(Token token){auto self=shared_from_this();auto record=Find(token);if(!record)return;record->dispatching=false;TryRetire(record);}
  // A logical Ready(false) can precede real enable/removal ACKs. HasLease and
  // pending actual callbacks, rather than this callback, determine retirement.
  bool Ready(Token token,bool ok){
    auto self=shared_from_this();auto record=Find(token);if(!record||!record->issued)return false;
    if(!ok||record->closing||Expired(record->deadline)||!hooks_.current(record->pin)||!hooks_.lease_present(token.lease)){Close(token);return false;}
    record->ready=true;record->deadline=0;return true;
  }
  // Actual provenance can be checked while the real domain acquisition is
  // pending: a pause may arrive before its enable/skip ACK. This does not
  // publish logical readiness or permit ordinary caller operations.
  bool ObservedCurrent(Token token)const{const auto record=Find(token);return record&&record->issued&&!record->closing&&!Expired(record->deadline)&&hooks_.current(record->pin);}
  bool Current(Token token)const{const auto record=Find(token);return record&&record->ready&&ObservedCurrent(token);}
  bool Pins(Token token,Pin&output)const{const auto record=Find(token);if(!record)return false;output=record->pin;return true;}
  Receipt Hold(Token token){
    const auto record=Find(token);
    if(record&&!record->closing&&!hooks_.current(record->pin)){Close(token);return {};}
    if(!record||!record->issued||(!record->ready&&!record->closing)||!hooks_.lease_present(token.lease)||record->calls>=32||budget_->calls>=128||budget_->receipts==UINT64_MAX)return {};
    const uint64_t id=budget_->receipts+1;
    calls_.emplace(id,record);budget_->receipts=id;++record->calls;++budget_->calls;
    return {instance_,id};
  }
  // Only actual ACK, real dispatch refusal, or a proven exact channel boundary
  // may retire a receipt. Public cancel/deadline must call Close instead.
  bool Retire(Receipt receipt){
    auto self=shared_from_this();if(receipt.book!=instance_)return false;
    const auto found=calls_.find(receipt.id);if(found==calls_.end())return false;
    auto record=found->second;calls_.erase(found);--record->calls;--budget_->calls;TryRetire(record);return true;
  }
  void Close(Token token){
    auto self=shared_from_this();auto record=Find(token);if(!record)return;
    if(!record->closing){record->closing=true;record->ready=false;record->cleanup=hooks_.now()+30000;if(record->issued)hooks_.cleanup(token,record->pin);}
    if(Find(token)==record)TryRetire(record);
    hooks_.wake();
  }
  void Poll(Token token){
    auto self=shared_from_this();const auto record=Find(token);if(!record)return;
    if(!record->closing&&!hooks_.current(record->pin)){Close(token);return;}
    TryRetire(record);
  }
  void Tick(){
    auto self=shared_from_this();std::vector<std::shared_ptr<Record>>snapshot;for(const auto&[id,record]:records_)snapshot.push_back(record);
    for(const auto&record:snapshot){
      if(Find(record->token)!=record)continue;
      if(!record->closing&&(Expired(record->deadline)||!hooks_.current(record->pin)))Close(record->token);
      if(Find(record->token)!=record)continue;
      TryRetire(record);
      if(Find(record->token)==record&&record->closing&&!record->close_requested&&hooks_.now()>=record->cleanup){record->close_requested=true;hooks_.close_channel(record->pin,"Native frame debugger physical cleanup grace expired");}
    }
  }
  bool NeedsTick()const{for(const auto&[id,record]:records_)if((!record->closing&&record->deadline>0)||(record->closing&&!record->close_requested))return true;return false;}
  // Invoke after exact actual session callback/arbiter drain, never a frame
  // registry clear, navigation request, renderer-loss guess or user signal.
  void ChannelClosed(const Channel&channel,uint64_t actual_attachment){
    auto self=shared_from_this();std::vector<std::shared_ptr<Record>>closed;
    for(const auto&[id,record]:records_)if(record->pin.channel==channel&&record->pin.attachment==actual_attachment)closed.push_back(record);
    for(const auto&record:closed)Drop(record);
  }
  void HostClosed(const Host&actual_observer){
    if(actual_observer!=host_)return;
    auto self=shared_from_this();closed_=true;while(!records_.empty())Drop(records_.begin()->second);
  }
  size_t Retained()const{return records_.size();}
  bool Retains(Token token)const{return bool(Find(token));}
  size_t Physical()const{return calls_.size();}
  bool AcceptedWorkReady()const{
    if(closed_||!calls_.empty())return false;
    for(const auto&[id,record]:records_){(void)id;if(record->closing||record->close_requested||record->dispatching||record->calls)return false;}
    return true;
  }
 private:
  struct Record{Pin pin;Token token;double deadline=0,cleanup=0;size_t calls=0,bytes=0;bool issued=false,dispatching=false,ready=false,closing=false,close_requested=false;};
  FrameDebuggerScopes(Host host,Hooks hooks,std::shared_ptr<Budget>budget):host_(std::move(host)),hooks_(std::move(hooks)),budget_(std::move(budget)){}
  static bool Identity(const std::string&text,size_t maximum){return !text.empty()&&text.size()<=maximum&&text.find('\0')==std::string::npos;}
  bool Valid(const Pin&pin)const{return pin.channel.host==host_&&pin.channel.session.size()<=128&&pin.channel.session.find('\0')==std::string::npos&&pin.attachment&&pin.document&&Identity(pin.frame,256)&&pin.loader.size()<=256&&pin.loader.find('\0')==std::string::npos&&pin.context>0&&Identity(pin.unique_context,256)&&(!pin.root||pin.channel.session.empty());}
  static size_t Bytes(const Channel&channel){return sizeof(Channel)+channel.host.source.capacity()+channel.session.capacity();}
  static size_t Bytes(const Pin&pin){return sizeof(Pin)+pin.channel.host.source.capacity()+pin.channel.session.capacity()+pin.frame.capacity()+pin.loader.capacity()+pin.unique_context.capacity();}
  bool Expired(double deadline)const{return deadline>0&&hooks_.now()>=deadline;}
  std::shared_ptr<Record>Find(Token token)const{if(token.book!=instance_)return {};const auto found=records_.find(token.lease);return found==records_.end()?nullptr:found->second;}
  void TryRetire(const std::shared_ptr<Record>&record){if(Find(record->token)==record&&record->closing&&!record->dispatching&&!record->calls&&(!record->issued||!hooks_.lease_present(record->token.lease)))Drop(record);}
  void Drop(std::shared_ptr<Record>record){
    const auto token=record->token;if(Find(token)!=record)return;
    for(auto it=calls_.begin();it!=calls_.end();)if(it->second==record){--budget_->calls;--record->calls;it=calls_.erase(it);}else ++it;
    const auto channel=budget_->channels.find(record->pin.channel);if(channel!=budget_->channels.end()&&channel->second==token)budget_->channels.erase(channel);
    budget_->bytes-=record->bytes;records_.erase(token.lease);if(records_.empty())physical_self_.reset();
  }
  Host host_;Hooks hooks_;std::shared_ptr<Budget>budget_;uint64_t instance_=0,last_lease_=0;bool closed_=false;
  std::map<uint64_t,std::shared_ptr<Record>>records_;
  std::map<uint64_t,std::shared_ptr<Record>>calls_;
  std::shared_ptr<FrameDebuggerScopes>physical_self_;
};
}
