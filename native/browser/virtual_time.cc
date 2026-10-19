// SPDX-License-Identifier: MIT
#include "virtual_time.h"
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace suitecut {
namespace {
// Performance.Timestamp is native int64 microseconds / 1e6, transported as
// double seconds and then converted to milliseconds by Source. Below 2^50us
// all three binary64 conversions (including reconstruction) together lose
// less than half a microsecond, so
// nearest-integer conversion recovers the actual native tick. The wider 2^53
// integer bound alone cannot guarantee that round trip.
constexpr uint64_t kMaximumExactNativeMicroseconds=(uint64_t{1}<<50)-1;
constexpr double kMaximumRunForMilliseconds=static_cast<double>(kMaximumExactNativeMicroseconds/1000);
std::optional<double> NativeTicks(double milliseconds){
  if(!std::isfinite(milliseconds)||milliseconds<0)return std::nullopt;
  const double ticks=std::round(milliseconds*1000.);
  if(!std::isfinite(ticks)||ticks>static_cast<double>(kMaximumExactNativeMicroseconds))return std::nullopt;
  return ticks;
}
std::optional<double> BudgetTarget(double start,double budget){
  if(!std::isfinite(budget)||budget<1||budget>kMaximumRunForMilliseconds||std::floor(budget)!=budget)return std::nullopt;
  const auto origin=NativeTicks(start);if(!origin)return std::nullopt;
  // Both operands and their accepted sum are exact integral native ticks,
  // strictly below int64 TimeDelta saturation. No tick is advanced here.
  static_assert(kMaximumExactNativeMicroseconds<static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  const double target=*origin+budget*1000.;
  if(target>static_cast<double>(kMaximumExactNativeMicroseconds))return std::nullopt;
  return target;
}
}
std::optional<double> NativeVirtualTime::ParseDuration(std::string_view duration){
  // Each colon-delimited component contains exactly two ASCII decimal digits.
  // Minutes and seconds remain below 60; the first component of HH:MM:SS is
  // hours. Bound the input before arithmetic and before allocating any state.
  if(duration.size()!=2&&duration.size()!=5&&duration.size()!=8)return std::nullopt;
  unsigned components[3]={0,0,0};size_t count=0,start=0;
  while(start<duration.size()){
    if(count==3)return std::nullopt;
    const auto end=duration.find(':',start);
    const size_t stop=end==std::string_view::npos?duration.size():end;
    if(stop-start!=2)return std::nullopt;
    unsigned value=0;
    for(size_t i=start;i<stop;++i){
      const auto digit=duration[i];if(digit<'0'||digit>'9')return std::nullopt;
      value=value*10+static_cast<unsigned>(digit-'0');
    }
    components[count++]=value;
    if(end==std::string_view::npos)break;
    start=end+1;if(start==duration.size())return std::nullopt;
  }
  unsigned seconds=0;
  for(size_t i=0;i<count;++i){
    if((i>0||count<3)&&components[i]>=60)return std::nullopt;
    seconds=seconds*60+components[i];
  }
  const double milliseconds=static_cast<double>(seconds)*1000;
  if(milliseconds<1)return std::nullopt;
  return milliseconds;
}
bool NativeVirtualTime::IsSynchronousException(std::string_view text){
  return text=="Uncaught"||(text.starts_with("Uncaught ")&&!text.starts_with("Uncaught (in promise)"));
}
bool NativeVirtualTime::Begin(Request request,double now){
  if(closing_||request.id<=0||request.generation==0||!std::isfinite(now)||!std::isfinite(request.deadline)||request.deadline<=now||!std::isfinite(request.value))return false;
  if((request.action==Action::Advance||request.action==Action::RunFor)&&
     (request.value<1||request.value>(request.action==Action::RunFor?kMaximumRunForMilliseconds:86400000.)||std::floor(request.value)!=request.value))return false;
  if(request.action==Action::RunFor&&request.context<=0)return false;
  if(request.action==Action::InstallPaused&&std::abs(request.value)>8640000000000000.)return false;
  if(queue_.size()+(active_?1:0)>=64)return false;
  if((active_&&active_->request.id==request.id)||std::any_of(queue_.begin(),queue_.end(),[&](const Request& item){return item.id==request.id;}))return false;
  queue_.push_back(request);Start(now);return true;
}
void NativeVirtualTime::Start(double now){
  if(active_||closing_)return;
  while(!queue_.empty()){
    const auto request=queue_.front();queue_.pop_front();
    if(now>=request.deadline){hooks_.complete(request.id,request.generation,false,"Native virtual-time operation timed out (Timeout exceeded)");if(active_||closing_)return;continue;}
    if(request.action==Action::InstallPaused&&installed_){hooks_.complete(request.id,request.generation,false,"Native virtual time is already installed; Chromium cannot rebase its initial time");if(active_||closing_)return;continue;}
    if(request.action!=Action::InstallPaused&&!installed_){hooks_.complete(request.id,request.generation,false,"Native virtual time is not installed");if(active_||closing_)return;continue;}
    // Idempotent pause uses a previously genuine renderer pause ACK. Only the
    // FIFO head can take this path, after the earlier physical owner retired;
    // a queued pause never bypasses an in-flight advance or cleanup fence.
    if(request.action==Action::Pause&&paused_){
      hooks_.complete(request.id,request.generation,true,{});
      if(active_||closing_)return;
      continue;
    }
    active_=Work{request};
    if(request.action==Action::InstallPaused)Send(Kind::EnableMetrics,Phase::Enable,now);
    else if(request.action==Action::Pause)Send(Kind::Pause,Phase::Pause,now);
    else Send(Kind::ObservePerformance,Phase::StartObservation,now);
    return;
  }
}
void NativeVirtualTime::Send(Kind kind,Phase phase,double now,double budget,std::optional<double> epoch){
  if(!active_||active_->physical||closing_)return;
  if(nonce_==std::numeric_limits<uint64_t>::max()){FailClosed("Native virtual-time control nonce exhausted");return;}
  auto& work=*active_;work.phase=phase;work.physical=true;work.token=++nonce_;
  // A raw Chromium ACK must settle physically, even after logical cancellation.
  if(!work.logical&&work.cleanup_deadline==0)work.cleanup_deadline=now+1000;
  hooks_.send({work.token,work.request.id,work.request.generation,kind,budget,epoch});
}
void NativeVirtualTime::Fence(double now){
  if(!active_||active_->physical)return;
  Send(Kind::Pause,Phase::Fence,now);
}
void NativeVirtualTime::Retire(double now,bool success,const std::string& error){
  if(!active_)return;
  const auto work=std::move(*active_);active_.reset();
  if(work.logical){
    const bool exception=success&&work.request.action==Action::RunFor&&!work.exception.empty();
    hooks_.complete(work.request.id,work.request.generation,success&&!exception,exception?work.exception:error);
  }
  Start(now);
}
void NativeVirtualTime::FailClosed(const std::string& error){
  if(closing_)return;
  closing_=true;
  // Retain the physical owner and queue until genuine target destruction.
  hooks_.close(error);
}
void NativeVirtualTime::Acknowledge(uint64_t token,bool ok,double now,std::optional<double> performance,uint64_t receipt_sequence){
  if(!active_||!active_->physical||active_->token!=token||closing_)return;
  auto& work=*active_;work.physical=false;
  if(!ok){FailClosed("Native virtual-time physical command failed");return;}
  if(work.phase==Phase::Install){installed_=true;paused_=true;}
  if(!work.logical){
    if(work.phase==Phase::Fence||work.phase==Phase::Enable){if(work.phase==Phase::Fence)paused_=true;Retire(now,false);}
    else Fence(now);
    return;
  }
  if(now>=work.request.deadline){
    const auto request=work.request;work.logical=false;work.cleanup_deadline=now+1000;
    hooks_.complete(request.id,request.generation,false,"Native virtual-time operation timed out (Timeout exceeded)");
    if(active_){if(active_->phase==Phase::Enable)Retire(now,false);else Fence(now);}
    return;
  }
  switch(work.phase){
    case Phase::Enable:Send(Kind::Pause,Phase::Install,now,0,work.request.value);break;
    case Phase::Install:case Phase::Pause:paused_=true;Retire(now,true);break;
    case Phase::StartObservation:
      if(!performance||!std::isfinite(*performance)){FailClosed("Native virtual-time actual performance observation failed");return;}
      if(work.request.action==Action::RunFor){
        if(receipt_sequence<exception_sequence_){FailClosed("Native runFor exception receipt fence moved backwards");return;}
        exception_sequence_=receipt_sequence;work.receipt_begin=receipt_sequence;
      }
      if(const auto target=BudgetTarget(*performance,work.request.value))work.target=*target;
      else{Retire(now,false,"Native virtual-time observed target exceeds safe native microsecond arithmetic");return;}
      Send(Kind::Advance,Phase::Advance,now,work.request.value);break;
    case Phase::Advance:
      paused_=false;
      if(work.hint){work.hint=false;Send(Kind::ObservePerformance,Phase::EndObservation,now);}
      else work.phase=Phase::AwaitBudget;
      break;
    case Phase::EndObservation:{
      if(!performance||!std::isfinite(*performance)){FailClosed("Native virtual-time actual performance observation failed");return;}
      // Budget events carry no nonce. Compare reconstructed actual native
      // microsecond ticks exactly: a one-tick-early stale event cannot settle.
      const auto measured=NativeTicks(*performance);
      if(!measured){FailClosed("Native virtual-time performance receipt exceeds precise native tick range");return;}
      if(*measured>=work.target)Fence(now);
      else if(work.hint){work.hint=false;Send(Kind::ObservePerformance,Phase::EndObservation,now);}
      else work.phase=Phase::AwaitBudget;
      break;}
    case Phase::Fence:
      if(work.request.action==Action::RunFor){
        if(receipt_sequence<exception_sequence_){FailClosed("Native runFor terminal exception receipt fence moved backwards");return;}
        exception_sequence_=receipt_sequence;
      }
      paused_=true;Retire(now,true);break;
    default:FailClosed("Native virtual-time acknowledgement phase mismatch");break;
  }
}
bool NativeVirtualTime::Exception(const ExceptionReceipt& receipt){
  if(receipt.sequence==0||receipt.sequence<=exception_sequence_)return false;
  exception_sequence_=receipt.sequence;
  if(closing_||!active_||!active_->logical)return false;
  auto& work=*active_;
  if(work.request.action!=Action::RunFor||receipt.sequence<=work.receipt_begin||
     receipt.context!=work.request.context||!receipt.synchronous||!work.exception.empty())return false;
  if(work.phase!=Phase::Advance&&work.phase!=Phase::AwaitBudget&&
     work.phase!=Phase::EndObservation&&work.phase!=Phase::Fence)return false;
  // This policy concerns an observed current-document exception. Inspector
  // supplies no timer-operation token; do not infer timer-exclusive ownership.
  work.exception="Native runFor observed a synchronous uncaught current-document exception";
  if(!receipt.error.empty()){
    if(receipt.error.size()<=65536)work.exception+=": "+receipt.error;
    else work.exception+=" (actual exception text exceeds the 65536-byte receipt limit)";
  }
  return true;
}
void NativeVirtualTime::BudgetExpired(double now){
  if(!active_||closing_||!active_->logical)return;
  auto& work=*active_;
  if(work.phase==Phase::Advance||work.phase==Phase::EndObservation){work.hint=true;return;}
  if(work.phase==Phase::AwaitBudget)Send(Kind::ObservePerformance,Phase::EndObservation,now);
}
void NativeVirtualTime::Cancel(int id,double now){
  queue_.erase(std::remove_if(queue_.begin(),queue_.end(),[&](const Request& item){return item.id==id;}),queue_.end());
  if(!active_||active_->request.id!=id||!active_->logical)return;
  active_->logical=false;active_->cleanup_deadline=now+1000;
  if(!active_->physical)Fence(now);
}
void NativeVirtualTime::Tick(double now){
  std::vector<Request> expired;
  for(auto it=queue_.begin();it!=queue_.end();){
    if(now<it->deadline){++it;continue;}
    expired.push_back(*it);it=queue_.erase(it);
  }
  for(const auto& request:expired)
    hooks_.complete(request.id,request.generation,false,"Native virtual-time operation timed out (Timeout exceeded)");
  if(active_&&active_->logical&&now>=active_->request.deadline){
    const auto request=active_->request;Cancel(request.id,now);
    hooks_.complete(request.id,request.generation,false,"Native virtual-time operation timed out (Timeout exceeded)");
  }
  if(active_&&!active_->logical&&active_->cleanup_deadline>0&&now>=active_->cleanup_deadline)
    FailClosed("Native virtual-time physical cleanup acknowledgement deadline expired");
}
void NativeVirtualTime::Closed(){active_.reset();queue_.clear();installed_=false;paused_=false;closing_=true;}
bool NativeVirtualTime::OwnsTarget()const{return installed_||active_.has_value()||!queue_.empty()||closing_;}
NativeVirtualTime::Snapshot NativeVirtualTime::State()const{return {installed_,paused_,queue_.size(),active_&&active_->physical,closing_,active_.has_value()};}
}
