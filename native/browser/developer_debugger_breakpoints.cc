// SPDX-License-Identifier: MIT
#include "developer_debugger_breakpoints.h"
#include <atomic>
#include <cmath>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include "devtools_utf16.h"
namespace suitecut {
namespace {
std::atomic<size_t> process_breakpoints{0};
constexpr size_t kPerSession=16,kProcess=128;
constexpr double kCleanup=30000;
std::string Quote(const std::string&text){return DevToolsQuoteUnits(CefString(text).ToString16());}
bool Location(DeveloperDebuggerBreakpoints::Dict value,std::string&script,int&line,int&column){
  if(!value||value->GetType("scriptId")!=VTYPE_STRING||value->GetString("scriptId").empty()||value->GetString("scriptId").length()>128||
     value->GetType("lineNumber")!=VTYPE_INT||value->GetInt("lineNumber")<0||
     (value->HasKey("columnNumber")&&(value->GetType("columnNumber")!=VTYPE_INT||value->GetInt("columnNumber")<0)))return false;
  script=value->GetString("scriptId").ToString();line=value->GetInt("lineNumber");column=value->HasKey("columnNumber")?value->GetInt("columnNumber"):0;return true;
}
}
class DeveloperDebuggerBreakpoints::Impl:public std::enable_shared_from_this<Impl>{
 public:
  struct Record{
    uint64_t serial=0;int owner=0;double deadline=0,cleanup_deadline=0;
    std::string token,script,native_id;Reply reply;
    bool set_pending=true,remove_pending=false,canceled=false,failed=false,published=false;
    Record()=default;Record(const Record&)=delete;Record&operator=(const Record&)=delete;
    ~Record(){--process_breakpoints;}
  };
  explicit Impl(Hooks hooks):hooks_(std::move(hooks)){}
  Hooks hooks_;std::map<std::string,std::shared_ptr<Record>>records_;uint64_t serial_=0;
  std::shared_ptr<Impl>physical_self_;
  bool closing_=false,closed_=false,close_requested_=false;
  bool Expired(double deadline)const{return deadline>0&&hooks_.now()>=deadline;}
  bool Current(const std::shared_ptr<Record>&record)const{
    const auto found=records_.find(record->token);return !closed_&&found!=records_.end()&&found->second==record;
  }
  void Changed(){if(hooks_.changed)hooks_.changed();}
  bool Finish(const std::shared_ptr<Record>&record,bool ok,const std::string&json,const std::string&error){
    auto reply=std::move(record->reply);record->owner=0;
    const bool expired=Expired(record->deadline);
    return reply&&reply(ok&&!expired,expired?"":json,
      expired?"Native debugger breakpoint original deadline expired":error)&&ok&&!expired;
  }
  void CleanupClock(const std::shared_ptr<Record>&record){
    if(!record->cleanup_deadline)record->cleanup_deadline=((record->owner||!record->published)&&Expired(record->deadline)?record->deadline:hooks_.now())+kCleanup;
  }
  void RequestClose(const std::string&error){
    if(close_requested_||closed_)return;
    close_requested_=true;
    if(hooks_.close_channel)hooks_.close_channel(error);
  }
  void Fatal(const std::shared_ptr<Record>&record,const std::string&error){
    record->failed=true;Finish(record,false,"",error);
    RequestClose(error);
    Changed();
  }
  void Drop(const std::shared_ptr<Record>&record,bool cleanup){
    if(!Current(record)||record->set_pending||record->remove_pending||record->failed||record->native_id.empty())return;
    if(cleanup)CleanupClock(record);
    record->remove_pending=true;
    auto params=CefDictionaryValue::Create();params->SetString("breakpointId",record->native_id);
    auto self=shared_from_this();
    const auto actual=[self,record](bool ok,Dict,const std::string&error){
      if(!self->Current(record))return;
      record->remove_pending=false;
      if(!ok){self->Fatal(record,"Native debugger breakpoint removal failed: "+error);return;}
      // Remove the exact record before a reentrant user reply can admit a new
      // breakpoint; its actual removal ACK has already been observed.
      self->records_.erase(record->token);if(self->records_.empty())self->physical_self_.reset();
      self->Finish(record,true,"{}","");self->Changed();
    };
    if(!hooks_.send("Debugger.removeBreakpoint",params,cleanup?record->cleanup_deadline:record->deadline,actual))
      actual(false,nullptr,"Actual remove dispatch was not admitted");
  }
  void SetFailed(const std::shared_ptr<Record>&record,bool no_install,const std::string&error){
    if(!Current(record))return;
    if(!no_install){Fatal(record,"Actual native debugger set result could not prove whether a breakpoint was installed: "+error);return;}
    records_.erase(record->token);if(records_.empty())physical_self_.reset();
    Finish(record,false,"",error);Changed();
  }
  void Set(int owner,Dict location,double deadline,Reply reply){
    std::string script;int line=0,column=0;
    if(!reply)return;
    if(closed_||closing_||close_requested_||owner<1||!std::isfinite(deadline)||deadline<0||Expired(deadline)||!hooks_.current()||
       !Location(location,script,line,column)||!hooks_.owns_script(script)||records_.size()>=kPerSession||serial_==9007199254740991ULL){
      reply(false,"","Native debugger breakpoint requires bounded actual current author script ownership");return;
    }
    size_t count=process_breakpoints.load();while(count<kProcess&&!process_breakpoints.compare_exchange_weak(count,count+1)){}
    if(count>=kProcess){reply(false,"","Native debugger process breakpoint capacity exceeded");return;}
    auto record=std::make_shared<Record>();record->serial=++serial_;record->owner=owner;record->deadline=deadline;
    record->script=script;record->reply=std::move(reply);record->token=hooks_.session+":breakpoint:"+std::to_string(record->serial);
    records_.emplace(record->token,record);physical_self_=shared_from_this();
    // Build a fresh native-owned graph; never retain/reparent the caller's CEF
    // location wrapper across an asynchronous Inspector callback.
    auto selected=CefDictionaryValue::Create();selected->SetString("scriptId",script);selected->SetInt("lineNumber",line);selected->SetInt("columnNumber",column);
    auto params=CefDictionaryValue::Create();params->SetDictionary("location",selected);selected=nullptr;
    struct Acceptance{
      struct Failure{bool agent_error;std::string error;};
      bool returned=false,admitted=false;std::optional<Failure>failure;
    };
    auto accepted=std::make_shared<Acceptance>();auto self=shared_from_this();
    const auto actual=[self,record,accepted](bool ok,Dict result,const std::string&error){
      if(!self->Current(record))return;
      record->set_pending=false;
      if(!ok){
        // Native transport/budget failures can arrive after a successful engine
        // mutation. Only an actual structured Inspector error or a proven
        // dispatch refusal permits retirement without a returned native ID.
        const bool agent_error=result&&result->GetType("code")==VTYPE_INT&&result->GetType("message")==VTYPE_STRING;
        if(!accepted->returned){accepted->failure=Acceptance::Failure{agent_error,error};return;}
        self->SetFailed(record,!accepted->admitted||agent_error,error);return;
      }
      // A successful malformed reply may still have installed a breakpoint.
      // Without its genuine ID, only an actual channel close proves cleanup.
      std::string resolved;int line=0,column=0;
      if(!result||result->GetType("breakpointId")!=VTYPE_STRING||result->GetString("breakpointId").empty()||
         result->GetString("breakpointId").length()>512||result->GetType("actualLocation")!=VTYPE_DICTIONARY||
         !Location(result->GetDictionary("actualLocation"),resolved,line,column)||resolved!=record->script){
        self->Fatal(record,"Actual native debugger breakpoint reply has no bounded matching identity");return;
      }
      record->native_id=result->GetString("breakpointId").ToString();
      for(const auto&[token,prior]:self->records_)if(prior!=record&&prior->native_id==record->native_id){
        self->Fatal(record,"Actual native debugger breakpoint identity collision");return;
      }
      if(self->closing_||record->canceled||!self->hooks_.current()||self->Expired(record->deadline)){
        self->CleanupClock(record);
        self->Finish(record,false,"","Native debugger breakpoint owner canceled or document changed");
        self->Drop(record,true);return;
      }
      const bool published=self->Finish(record,true,"{\"breakpoint\":"+Quote(record->token)+",\"location\":{\"scriptId\":"+Quote(resolved)+
        ",\"lineNumber\":"+std::to_string(line)+",\"columnNumber\":"+std::to_string(column)+"}}","");
      record->published=published;
      if(!published&&self->Current(record)){
        self->CleanupClock(record);record->canceled=true;self->Drop(record,true);
      }
      self->Changed();
    };
    accepted->admitted=hooks_.send("Debugger.setBreakpoint",params,deadline,actual);accepted->returned=true;
    if(accepted->failure){
      const auto failure=std::move(*accepted->failure);accepted->failure.reset();
      SetFailed(record,!accepted->admitted||failure.agent_error,failure.error);
    }else if(!accepted->admitted)SetFailed(record,true,"Actual set dispatch was not admitted");
  }
  void Remove(int owner,const std::string&token,double deadline,Reply reply){
    if(!reply)return;
    const auto found=records_.find(token);
    if(closed_||closing_||close_requested_||owner<1||!std::isfinite(deadline)||deadline<0||Expired(deadline)||!hooks_.current()||
       found==records_.end()||found->second->set_pending||found->second->remove_pending||found->second->canceled||found->second->failed){
      reply(false,"","Native debugger breakpoint is stale, closing or belongs to another owner");return;
    }
    auto record=found->second;record->owner=owner;record->deadline=deadline;record->reply=std::move(reply);Drop(record,false);
  }
  void Cancel(int owner){
    for(auto&[token,record]:records_)if(record->owner==owner){CleanupClock(record);record->owner=0;record->reply={};record->canceled=true;}
  }
  void Close(){
    if(closed_||closing_)return;
    const auto self=shared_from_this();closing_=true;
    std::vector<std::shared_ptr<Record>>records;for(const auto&[token,record]:records_)records.push_back(record);
    for(const auto&record:records){CleanupClock(record);record->canceled=true;record->owner=0;record->reply={};if(Current(record))Drop(record,true);}
    Changed();
  }
  void Tick(double now){
    if(closed_||close_requested_)return;
    const auto self=shared_from_this();
    for(const auto&[token,record]:records_)if(record->cleanup_deadline>0&&now>=record->cleanup_deadline){
      RequestClose("Native debugger breakpoint physical cleanup grace expired; actual channel closure required");return;
    }
  }
  void Closed(){const auto self=shared_from_this();closed_=true;closing_=true;records_.clear();physical_self_.reset();}
};
DeveloperDebuggerBreakpoints::DeveloperDebuggerBreakpoints(Hooks hooks):impl_(std::make_shared<Impl>(std::move(hooks))){}
DeveloperDebuggerBreakpoints::~DeveloperDebuggerBreakpoints(){impl_->Close();}
void DeveloperDebuggerBreakpoints::Set(int owner,Dict location,double deadline,Reply reply){impl_->Set(owner,location,deadline,std::move(reply));}
void DeveloperDebuggerBreakpoints::Remove(int owner,const std::string&token,double deadline,Reply reply){impl_->Remove(owner,token,deadline,std::move(reply));}
void DeveloperDebuggerBreakpoints::Cancel(int owner){impl_->Cancel(owner);}
void DeveloperDebuggerBreakpoints::Close(){impl_->Close();}
void DeveloperDebuggerBreakpoints::Tick(double now){impl_->Tick(now);}
bool DeveloperDebuggerBreakpoints::NeedsTick()const{
  if(impl_->closed_||impl_->close_requested_)return false;
  for(const auto&[token,record]:impl_->records_)if(record->cleanup_deadline>0)return true;
  return false;
}
void DeveloperDebuggerBreakpoints::Closed(){impl_->Closed();}
size_t DeveloperDebuggerBreakpoints::Retained()const{return impl_->records_.size();}
size_t DeveloperDebuggerBreakpoints::ProcessRetained(){return process_breakpoints.load();}
}
