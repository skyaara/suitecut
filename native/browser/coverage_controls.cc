// SPDX-License-Identifier: MIT
#include "coverage_controls.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <iomanip>
#include <sstream>
#include <set>
#include <openssl/evp.h>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
#include "coverage_ranges.h"
#include "coverage_retention.h"
#include "renderer_script_sources.h"
#include "devtools_utf16.h"
namespace suitecut {
class CoverageControls::Impl : public std::enable_shared_from_this<Impl> {
 public:
  explicit Impl(Hooks hooks):hooks_(std::move(hooks)){
    for(const auto& script:kNativeRendererScriptSources){const auto hash=SourceHash(script.source);if(!hash.empty())native_scripts_[script.name]=hash;}
  }
  enum class Kind{JS,CSS};enum class Phase{Starting,Active,Stopping,Cleaning};
  struct Source {
    std::string id,url,text;size_t units=0;uint64_t epoch=0;double deadline=0;bool fetched=false;int context=0;bool native_owned=false;
  };
  struct Record {
    Kind kind;Phase phase=Phase::Starting;size_t reserved=0;std::function<void(size_t)> release;~Record(){if(reserved&&release)release(reserved);}int owner=0,step=0;double deadline=0,cleanup_deadline=0;Reply reply;
    bool reset=true,anonymous=false,blocks=true,dom=false,debugger=false,profiler=false,precise=false,css=false,tracking=false,frozen=false;
    uint64_t epoch=1;size_t reading=0,physical=0,settling=0,text_bytes=0;
    std::map<std::string,Source> sources;std::deque<std::pair<std::string,uint64_t>> queue;
    bool terminal=false;std::string agent,capture;std::set<int> author_contexts;std::map<std::pair<int,std::string>,bool> known_scripts;CefRefPtr<CefListValue> measured;std::string error,output;
  };
  struct Call{std::shared_ptr<Record> record;double deadline=0,retirement=0;};
  static constexpr size_t kScripts=2048,kTextBytes=16*1048576,kPhysical=32,kReaders=4;
  std::map<std::string,std::string> native_scripts_;Hooks hooks_;std::map<Kind,std::shared_ptr<Record>> records_;std::map<uint64_t,Call> calls_;uint64_t next_=0;bool closed_=false,closing_channel_=false;
  static bool Boolean(Dict params,const char* name,bool& value){if(!params->HasKey(name))return true;if(params->GetType(name)!=VTYPE_BOOL)return false;value=params->GetBool(name);return true;}
  static bool Text(CefRefPtr<CefValue> value,std::string& text,size_t* count=nullptr){std::u16string units;if(!value||(value->GetType()!=VTYPE_STRING&&!DevToolsReadTag(value,units)))return false;text=DevToolsString(value);if(count)*count=value->GetType()==VTYPE_STRING?value->GetString().ToString16().size():units.size();return true;}
  bool Current(const std::shared_ptr<Record>& record)const{const auto found=records_.find(record->kind);return !closed_&&found!=records_.end()&&found->second==record;}
  bool Reserve(const std::shared_ptr<Record>& record,size_t bytes){if(hooks_.reserve_payload&&!hooks_.reserve_payload(bytes))return false;if(hooks_.reserve_payload)record->reserved+=bytes;return true;}
  void Error(const std::shared_ptr<Record>& record,const std::string& error){if(record->error.empty())record->error=error.substr(0,8192);}
  void Wake(){if(hooks_.wake)hooks_.wake();}
  void CloseChannel(const std::string& error){if(!closing_channel_){closing_channel_=true;if(hooks_.close_channel)hooks_.close_channel(error);}}
  void Owner(const std::shared_ptr<Record>& record,bool ok,const std::string& error,const std::string& json=""){
    const bool expired=ok&&record->deadline>0&&hooks_.now()>=record->deadline;
    if(expired){Error(record,"Native coverage operation timed out");record->phase=Phase::Cleaning;record->queue.clear();}
    auto reply=std::move(record->reply);record->owner=0;record->deadline=0;if(reply)reply(ok&&!expired,expired?record->error:error,expired?std::string():json);
  }
  void Fail(const std::shared_ptr<Record>& record,const std::string& error){
    if(!Current(record)){return;}Error(record,error);record->phase=Phase::Cleaning;record->queue.clear();Owner(record,false,record->error);Cleanup(record);
  }
  void Send(const std::shared_ptr<Record>& record,const std::string& method,Dict params,double deadline,std::function<void(bool,Dict,const std::string&)> done){
    if(!Current(record)){return;}
    if(calls_.size()>=kPhysical||next_>=9007199254740991ULL){done(false,nullptr,"Native coverage physical command capacity/identity exceeded");return;}
    const auto token=++next_;const auto observed=hooks_.now();const auto retirement=(deadline>0?deadline:observed+30000)+30000;
    calls_.emplace(token,Call{record,deadline,retirement});++record->physical;const auto weak=weak_from_this();
    ProtocolReply reply=[weak,record,token,done=std::move(done)](bool ok,Dict result,const std::string& error)mutable{
      const auto self=weak.lock();if(!self)return;const auto found=self->calls_.find(token);if(found==self->calls_.end())return;const double deadline=found->second.deadline;self->calls_.erase(found);--record->physical;
      if(self->Current(record)){
        ++record->settling;
        const auto now=self->hooks_.now();const bool owner_expired=record->owner&&record->deadline>0&&now>=record->deadline;
        const bool call_expired=deadline>0&&now>=deadline;
        if(owner_expired){self->Error(record,"Native coverage operation timed out");record->phase=Phase::Cleaning;record->queue.clear();self->Owner(record,false,record->error);}
        // A late startup ACK still describes a genuinely enabled domain. Its
        // callback records those physical flags while Cleaning, then disables
        // them. Expired source reads can never admit late source bytes.
        const bool source_expired=call_expired&&record->phase!=Phase::Cleaning;
        if(call_expired&&record->phase==Phase::Cleaning&&!owner_expired&&record->owner){self->Error(record,"Native coverage cleanup deadline expired");self->Owner(record,false,record->error);}
        done(source_expired?false:ok,source_expired?nullptr:result,source_expired?"Native coverage source read timed out":error);
        --record->settling;self->Pump(record);self->Cleanup(record);
      }self->Wake();
    };
    bool admitted=false;
    if(method=="Debugger.enable"&&hooks_.debugger_acquire)admitted=hooks_.debugger_acquire(deadline,reply);
    else if(method=="Debugger.disable"&&hooks_.debugger_release)admitted=hooks_.debugger_release(deadline,reply);
    else admitted=hooks_.send(method,params,deadline,reply);
    if(!admitted){reply(false,nullptr,"Native coverage Inspector dispatch failed");}Wake();
  }
  void Handle(int owner,Dict params,double deadline,Reply reply){
    if(!reply){return;}if(closed_||owner<1||!params||!std::isfinite(deadline)||deadline<0||(deadline>0&&hooks_.now()>=deadline)){reply(false,"Invalid, closed or expired native coverage command","");return;}
    const auto action=params->GetString("action").ToString();const bool starting=action=="startJS"||action=="startCSS",stopping=action=="stopJS"||action=="stopCSS";
    if(!starting&&!stopping){reply(false,"Unsupported native coverage command","");return;}const auto kind=(action=="startJS"||action=="stopJS")?Kind::JS:Kind::CSS;
    const auto found=records_.find(kind);
    if(starting){
      if(bool(hooks_.debugger_acquire)!=bool(hooks_.debugger_release)){reply(false,"Native coverage shared Debugger lease channel is incomplete","");return;}
      if(bool(hooks_.reserve_payload)!=bool(hooks_.release_payload)){reply(false,"Native coverage shared payload reservation channel is incomplete","");return;}
      if(native_scripts_.size()!=kNativeRendererScriptSources.size()){reply(false,"Actual native renderer script ownership hashes are unavailable","");return;}
      if(found!=records_.end()){reply(false,"Native coverage is already enabled or its physical cleanup is pending","");return;}
      if(!hooks_.root_target_only()){reply(false,"Native coverage of out-of-process frame sessions is not implemented","");return;}
      auto record=std::make_shared<Record>();record->release=hooks_.release_payload;record->kind=kind;record->owner=owner;record->deadline=deadline;record->reply=std::move(reply);
      if(!Boolean(params,"resetOnNavigation",record->reset)||!Boolean(params,"reportAnonymousScripts",record->anonymous)||!Boolean(params,"useBlockCoverage",record->blocks)){Owner(record,false,"Invalid native coverage option");return;}
      if(kind==Kind::CSS&&(params->HasKey("reportAnonymousScripts")||params->HasKey("useBlockCoverage"))){Owner(record,false,"Native CSS coverage does not accept JavaScript capture options");return;}
      records_.emplace(kind,record);Start(record);return;
    }
    if(found==records_.end()){reply(false,"Native coverage is not enabled","");return;}const auto record=found->second;if(record->phase!=Phase::Active){reply(false,"Native coverage is starting, stopping or physically cleaning up","");return;}
    record->owner=owner;record->deadline=deadline;record->reply=std::move(reply);record->phase=Phase::Stopping;
    const auto collect=record->reset?(kind==Kind::JS?"Profiler.takePreciseCoverage":"CSS.stopRuleUsageTracking"):(kind==Kind::JS?"Profiler.stopPreciseCoverageWithNavigationRetention":"CSS.stopRuleUsageTrackingWithNavigationRetention");
    Send(record,collect,nullptr,deadline,[this,record](bool ok,Dict result,const std::string& error){
      if(!Current(record)){return;}if(record->terminal)return;if(ok&&record->kind==Kind::CSS)record->tracking=false;if(ok&&!record->reset&&record->kind==Kind::JS)record->precise=false;if(record->phase!=Phase::Stopping){return;}if(!ok){Fail(record,error.empty()?"Native coverage collection failed":error);return;}
      record->frozen=true;if(record->kind==Kind::CSS)record->tracking=false;
      if(!record->reset){
        if(!record->error.empty()){Fail(record,record->error);return;}
        std::string decode_error;if(!Retained(record,result,decode_error)){Fail(record,decode_error);return;}record->phase=Phase::Cleaning;Cleanup(record);return;
      }
      auto measured=result?result->GetList(record->kind==Kind::JS?"result":"ruleUsage"):nullptr;if(measured){auto boxed=CefValue::Create();boxed->SetList(measured->Copy());const auto retained=CefWriteJSON(boxed,JSON_WRITER_DEFAULT).ToString().size();if(!Reserve(record,retained)){Fail(record,"Native coverage shared measurement payload capacity exceeded");return;}record->measured=measured->Copy();}
      if(!record->measured){Fail(record,"Actual native coverage result is unavailable");return;}MaybeBuild(record);
    });
  }
  static bool Identity(Dict params,const char* field,std::string& output){
    if(!params||params->GetType(field)!=VTYPE_STRING)return false;
    output=params->GetString(field).ToString();return !output.empty()&&output.size()<=256&&output.find('\0')==std::string::npos;
  }
  bool Retained(const std::shared_ptr<Record>& record,Dict result,std::string& error){
    if(!result||result->GetString("agentId")!=record->agent||result->GetString("captureId")!=record->capture||result->GetType("final")!=VTYPE_BOOL||!result->GetBool("final")){error="Native retained coverage final capture/agent receipt is missing or stale";return false;}
    auto raw=CefValue::Create();raw->SetDictionary(result->Copy(false));std::string payload;
    if(!DevToolsLosslessJSON(raw,payload,8*1048576)||!Reserve(record,payload.size()*3)){error="Retained native coverage measurement exceeds its shared payload capacity";return false;}
    Dict normalized;bool unknown=false;
    const auto select=[record,&unknown](int context,const std::string& id){const auto found=record->known_scripts.find({context,id});if(found==record->known_scripts.end()){unknown=true;return false;}return found->second;};
    const bool decoded=record->kind==Kind::JS?CoverageRetention::JavaScript(result,select,normalized,error):CoverageRetention::CSS(result,normalized,error);
    if(!decoded||unknown){if(unknown)error="Retained JavaScript script has no attested author/native identity";return false;}
    auto entries=normalized->GetList("result");for(size_t n=0;n<entries->GetSize();++n){auto entry=entries->GetDictionary(n);entry->SetString("agentId",record->agent);entry->SetString("captureId",record->capture);entry->SetString("sessionId",hooks_.target_session);if(!hooks_.target_id.empty())entry->SetString("targetId",hooks_.target_id);}
    auto boxed=CefValue::Create();boxed->SetList(entries->Copy());if(!DevToolsLosslessJSON(boxed,record->output,2*1048576)||!Reserve(record,record->output.size())){error="Retained native coverage output exceeds its shared payload capacity";return false;}return true;
  }
  bool FinalReceipt(const std::string& action,std::string& output,std::string& error)const{
    const auto found=records_.find(action=="startJS"||action=="stopJS"?Kind::JS:Kind::CSS);
    if(found==records_.end()){return false;}error=found->second->error;if(!found->second->terminal)return false;output=found->second->output;return true;
  }
  void Start(const std::shared_ptr<Record>& record){
    if(!Current(record)||record->phase!=Phase::Starting)return;
    const std::vector<std::string> js=hooks_.debugger_acquire?std::vector<std::string>{"Debugger.enable","Profiler.enable","Profiler.startPreciseCoverage"}:std::vector<std::string>{"Debugger.enable","Debugger.setSkipAllPauses","Profiler.enable","Profiler.startPreciseCoverage"},css={"DOM.enable","CSS.enable","CSS.startRuleUsageTracking"};auto retained=record->kind==Kind::JS?js:css;
    if(!record->reset){retained.back()=record->kind==Kind::JS?"Profiler.startPreciseCoverageWithNavigationRetention":"CSS.startRuleUsageTrackingWithNavigationRetention";retained.insert(retained.begin(),record->kind==Kind::JS?"Profiler.getNavigationCoverageCapabilities":"CSS.getNavigationCoverageCapabilities");}
    const auto& methods=retained;
    if(size_t(record->step)==methods.size()){record->phase=Phase::Active;Owner(record,true,"");return;}
    const auto method=methods[record->step++];Dict params;if(method=="Debugger.setSkipAllPauses"){params=CefDictionaryValue::Create();params->SetBool("skip",true);}if(method=="Profiler.startPreciseCoverage"||method=="Profiler.startPreciseCoverageWithNavigationRetention"){params=CefDictionaryValue::Create();if(record->reset)params->SetBool("callCount",true);params->SetBool("detailed",record->blocks);}
    Send(record,method,params,record->deadline,[this,record,method](bool ok,Dict result,const std::string& error){
      if(!Current(record)){return;}if(ok){if(method=="DOM.enable")record->dom=true;if(method=="Debugger.enable")record->debugger=true;if(method=="Profiler.enable")record->profiler=true;if(method=="Profiler.startPreciseCoverage"||method=="Profiler.startPreciseCoverageWithNavigationRetention")record->precise=true;if(method=="CSS.enable")record->css=true;if(method=="CSS.startRuleUsageTracking"||method=="CSS.startRuleUsageTrackingWithNavigationRetention")record->tracking=true;}
      if(record->phase!=Phase::Starting){return;}if(!ok){Fail(record,method.ends_with(".getNavigationCoverageCapabilities")?"CEF coverage retention across navigation is unavailable: "+error:error.empty()?"Native coverage domain startup failed":error);return;}
      if(method=="Profiler.startPreciseCoverageWithNavigationRetention"||method=="CSS.startRuleUsageTrackingWithNavigationRetention"){
        if(!result||result->GetType("revision")!=VTYPE_INT||result->GetInt("revision")!=(record->kind==Kind::JS?2:3)||result->GetString("agentId")!=record->agent||!Identity(result,"captureId",record->capture)){Fail(record,"Native retained coverage startup identity/revision receipt is incompatible");return;}
      }
      if(method.ends_with(".getNavigationCoverageCapabilities")){
        const int revision=record->kind==Kind::JS?2:3;
        if(!result||result->GetType("revision")!=VTYPE_INT||result->GetInt("revision")!=revision||result->GetString("lifecycleContract")!="suitecut.coverage.final-lifecycle.v1"||!Identity(result,"agentId",record->agent)){Fail(record,"CEF coverage retention across navigation lacks the required final context/agent lifecycle contract (JS revision2/CSS revision3)");return;}
      }
      if(method=="Debugger.enable"&&hooks_.script_inventory){
        std::vector<Dict> scripts;std::string error;
        if(!hooks_.script_inventory(record->deadline,scripts,error)||scripts.size()>kScripts){Fail(record,error.empty()?"Native coverage script inventory exceeds capture capacity":error);return;}
        for(const auto& script:scripts){if(!Current(record)||record->phase!=Phase::Starting)return;if(record->deadline>0&&hooks_.now()>=record->deadline){Fail(record,"Native coverage original deadline expired during inventory seed");return;}Event("Debugger.scriptParsed",script);}
      }
      Start(record);
    });
  }
  void Event(const std::string& method,Dict params){
    if(closed_||!params)return;
    if(method=="Profiler.navigationCoverageFinalized"||method=="CSS.navigationCoverageFinalized"){
      const auto found=records_.find(method.starts_with("Profiler.")?Kind::JS:Kind::CSS);if(found==records_.end())return;const auto record=found->second;
      if(record->reset||record->phase==Phase::Cleaning)return;
      if(record->terminal||record->phase==Phase::Starting||params->GetType("terminal")!=VTYPE_BOOL||!params->GetBool("terminal")){Fail(record,"Native retained coverage terminal receipt is duplicate, premature or invalid");return;}
      std::string error;if(!Retained(record,params,error)){Fail(record,error);return;}record->terminal=true;record->frozen=true;Wake();return;
    }
    if(method=="Debugger.paused"){
      if(hooks_.debugger_acquire)return; // Shared broker exclusively routes owned/unowned pause fences.
      const auto found=records_.find(Kind::JS);if(found!=records_.end()){const auto record=found->second;Send(record,"Debugger.resume",nullptr,hooks_.now()+30000,[this,record](bool ok,Dict,const std::string& error){if(!ok){Error(record,error.empty()?"Native coverage debugger resume failed":error);CloseChannel(record->error);}});}return;
    }
    const auto kind=method=="Debugger.scriptParsed"?Kind::JS:Kind::CSS;if(method!="Debugger.scriptParsed"&&method!="CSS.styleSheetAdded")return;const auto found=records_.find(kind);if(found==records_.end())return;const auto record=found->second;
    if(record->phase==Phase::Cleaning||record->frozen||record->terminal){return;}std::string id,url;
    if(kind==Kind::JS){
      if(!record->reset){
        const auto key=std::make_pair(params->GetInt("executionContextId"),params->GetString("scriptId").ToString());
        if(key.first<=0||key.second.empty()||key.second.size()>256||(!record->known_scripts.contains(key)&&record->known_scripts.size()>=kScripts)){Error(record,"Retained native coverage identity inventory capacity exceeded");return;}
        if(!record->known_scripts.contains(key)&&!Reserve(record,key.second.size()+96)){Error(record,"Retained native coverage inventory exceeds its shared payload capacity");return;}
        record->known_scripts.try_emplace(key,false);
      }
      const int context=params->GetInt("executionContextId");bool author=hooks_.author_context(context);
      if(!record->reset){
        if(author&&!record->author_contexts.contains(context)){if(record->author_contexts.size()>=kScripts){Error(record,"Retained coverage author-context capacity exceeded");return;}record->author_contexts.insert(context);}
        author=author||record->author_contexts.contains(context);
      }
      if(!author)return;
      if(params->GetType("suitecutNativeOwned")==VTYPE_BOOL&&params->GetBool("suitecutNativeOwned"))return;
      // Native CEF Eval carries an actual embedder resource name, no authored
      // sourceURL directive, and the exact hash of the shared native source.
      // An author choosing a similar //# sourceURL remains author coverage.
      const auto native=native_scripts_.find(params->GetString("embedderName").ToString());
      if(native!=native_scripts_.end()&&params->GetType("hasSourceURL")==VTYPE_BOOL&&!params->GetBool("hasSourceURL")&&params->GetString("url")==native->first&&params->GetString("hash")==native->second) return;
      if(params->GetType("scriptLanguage")==VTYPE_STRING&&params->GetString("scriptLanguage")!="JavaScript")return;
      id=params->GetString("scriptId");if(!Text(params->GetValue("url"),url)){Error(record,"Native coverage script URL is unavailable");return;}if(hooks_.script_url&&!hooks_.script_url(params,url))return;if(url.empty()&&!record->anonymous)return;
    }else{
      auto header=params->GetDictionary("header");if(!header)return;const auto origin=header->GetString("origin").ToString();if(origin!="regular")return;id=header->GetString("styleSheetId");if(!Text(header->GetValue("sourceURL"),url)){Error(record,"Native coverage stylesheet URL is unavailable");return;}if(url.empty())return;
    }
    if(id.empty()||id.size()>256){Error(record,"Invalid native coverage source identity");return;}if(record->sources.contains(id))return;
    if(record->sources.size()>=kScripts){Error(record,"Native coverage source capacity exceeded");return;}
    if(!Reserve(record,id.size()+url.size())){Error(record,"Native coverage shared source metadata capacity exceeded");return;}
    record->sources.emplace(id,Source{id,url,"",0,record->epoch,hooks_.now()+30000,false,kind==Kind::JS?params->GetInt("executionContextId"):0,false});
    if(!record->reset){if(kind==Kind::JS)record->known_scripts[{params->GetInt("executionContextId"),id}]=true;return;}
    record->queue.emplace_back(id,record->epoch);Pump(record);Wake();
  }
  void NativeScript(int context,const std::string& id){
    const auto record=records_.find(Kind::JS);if(record==records_.end())return;
    const auto source=record->second->sources.find(id);if(source==record->second->sources.end()||source->second.context!=context)return;
    // Ownership is permanent for this recorded source epoch, even if its V8
    // context dies before stop. Actual read callbacks remain physically held.
    source->second.native_owned=true;const auto known=record->second->known_scripts.find({context,id});if(known!=record->second->known_scripts.end())known->second=false;
  }
  void Pump(const std::shared_ptr<Record>& record){
    if(!Current(record)||record->phase==Phase::Cleaning)return;
    while(record->reading<kReaders&&!record->queue.empty()&&calls_.size()<kPhysical){
      const auto [id,epoch]=record->queue.front();record->queue.pop_front();const auto source=record->sources.find(id);if(source==record->sources.end()||source->second.epoch!=epoch||source->second.native_owned)continue;
      if(hooks_.now()>=source->second.deadline){Error(record,"Native coverage source read timed out before dispatch");continue;}
      auto params=CefDictionaryValue::Create();params->SetString(record->kind==Kind::JS?"scriptId":"styleSheetId",id);const auto deadline=source->second.deadline;++record->reading;
      Send(record,record->kind==Kind::JS?"Debugger.getScriptSource":"CSS.getStyleSheetText",params,deadline,[this,record,id,epoch](bool ok,Dict result,const std::string& error){
        --record->reading;const auto source=record->sources.find(id);if(!Current(record)||record->phase==Phase::Cleaning||source==record->sources.end()||source->second.epoch!=epoch||source->second.native_owned)return;
        std::string text;size_t units=0;if(!ok||!result||!Text(result->GetValue(record->kind==Kind::JS?"scriptSource":"text"),text,&units)){Error(record,error.empty()?"Actual native coverage source text is unavailable":error);return;}
        if(text.size()>kTextBytes-record->text_bytes){Error(record,"Native coverage source byte capacity exceeded");return;}if(!Reserve(record,text.size())){Error(record,"Native coverage shared source payload capacity exceeded");return;}record->text_bytes+=text.size();source->second.text=std::move(text);source->second.units=units;source->second.fetched=true;
      });
    }
    MaybeBuild(record);
  }
  void MaybeBuild(const std::shared_ptr<Record>& record);
  void Cleanup(const std::shared_ptr<Record>& record);
  void Navigated(){for(const auto& [kind,record]:records_)if(record->reset&&record->phase!=Phase::Cleaning&&!record->frozen){++record->epoch;record->sources.clear();record->author_contexts.clear();record->queue.clear();record->text_bytes=0;if(record->reserved&&record->release){record->release(record->reserved);record->reserved=0;}}}
  void OutOfProcessFrameAttached(){for(const auto& [kind,record]:records_)Error(record,"Native coverage of out-of-process frame sessions is not implemented");}
  void Abort(const std::string& action,const std::string& error){const bool js=action=="startJS"||action=="stopJS",css=action=="startCSS"||action=="stopCSS";if(!js&&!css)return;const auto found=records_.find(js?Kind::JS:Kind::CSS);if(found!=records_.end())Fail(found->second,error);}
  bool Ready(const std::string& action)const{const bool js=action=="startJS"||action=="stopJS",css=action=="startCSS"||action=="stopCSS";return (js||css)&&!records_.contains(js?Kind::JS:Kind::CSS);}
  void Cancel(int owner){for(const auto& [kind,record]:records_)if(record->owner==owner){Fail(record,"Native coverage operation canceled");return;}}
  void Tick(double now){
    std::vector<std::shared_ptr<Record>> expired;for(const auto& [kind,record]:records_)if(record->owner&&record->deadline>0&&now>=record->deadline)expired.push_back(record);for(const auto& record:expired)Fail(record,"Native coverage operation timed out");
    for(const auto& [id,call]:calls_)if(now>=call.retirement){CloseChannel("Native coverage physical command did not retire within its original deadline and bounded cleanup grace");break;}
    std::vector<std::shared_ptr<Record>> active;for(const auto& [kind,record]:records_)active.push_back(record);for(const auto& record:active){Pump(record);Cleanup(record);}
  }
  void Closed(){if(closed_)return;closed_=true;auto records=std::move(records_);calls_.clear();for(const auto& [kind,record]:records){record->physical=record->reading=0;record->queue.clear();Owner(record,false,"Native coverage page channel closed");}}
};
namespace {
bool CoverageOffset(CefRefPtr<CefDictionaryValue> value,const char* key,int& output){if(!value||(value->GetType(key)!=VTYPE_INT&&value->GetType(key)!=VTYPE_DOUBLE))return false;const auto number=value->GetType(key)==VTYPE_INT?double(value->GetInt(key)):value->GetDouble(key);if(!std::isfinite(number)||number<0||number>2147483647||number!=std::floor(number))return false;output=int(number);return true;}
bool CoverageCount(CefRefPtr<CefDictionaryValue> value,const char* key,double& output){if(!value||(value->GetType(key)!=VTYPE_INT&&value->GetType(key)!=VTYPE_DOUBLE))return false;output=value->GetType(key)==VTYPE_INT?double(value->GetInt(key)):value->GetDouble(key);return std::isfinite(output)&&output>=0&&output<=9007199254740991.0&&output==std::floor(output);}
}
void CoverageControls::Impl::MaybeBuild(const std::shared_ptr<Record>& record){
  if(!Current(record)||!record->reset||record->phase!=Phase::Stopping||!record->frozen||record->reading||!record->queue.empty())return;
  if(!record->error.empty()){Fail(record,record->error);return;}
  auto entries=CefListValue::Create();size_t range_count=0;
  const auto fail=[this,record](const std::string& error){Fail(record,error);};
  if(record->kind==Kind::JS){
    if(record->measured->GetSize()>kScripts){fail("Native JavaScript coverage entry capacity exceeded");return;}
    for(size_t i=0;i<record->measured->GetSize();++i){
      auto measured=record->measured->GetDictionary(i);if(!measured||measured->GetType("scriptId")!=VTYPE_STRING){fail("Invalid actual JavaScript coverage identity");return;}const auto id=measured->GetString("scriptId").ToString();const auto found=record->sources.find(id);if(found==record->sources.end())continue;const auto& source=found->second;if(source.native_owned)continue;if(!source.fetched){fail("Actual JavaScript coverage source was not captured");return;}
      auto functions=measured->GetList("functions");if(!functions||functions->GetSize()>16384){fail("Invalid actual JavaScript coverage functions");return;}auto projected=CefListValue::Create();
      for(size_t f=0;f<functions->GetSize();++f){
        auto function=functions->GetDictionary(f);std::string name;if(!function||!Text(function->GetValue("functionName"),name)||function->GetType("isBlockCoverage")!=VTYPE_BOOL){fail("Invalid actual JavaScript coverage function metadata");return;}auto ranges=function->GetList("ranges");if(!ranges||ranges->GetSize()>16384-range_count){fail("Native JavaScript coverage range capacity exceeded");return;}range_count+=ranges->GetSize();auto selected_ranges=CefListValue::Create();
        for(size_t n=0;n<ranges->GetSize();++n){auto range=ranges->GetDictionary(n);int start=0,end=0;double count=0;if(!CoverageOffset(range,"startOffset",start)||!CoverageOffset(range,"endOffset",end)||end<start||size_t(end)>source.units||!CoverageCount(range,"count",count)){fail("Invalid actual JavaScript coverage range");return;}auto selected=CefDictionaryValue::Create();selected->SetInt("startOffset",start);selected->SetInt("endOffset",end);selected->SetDouble("count",count);selected_ranges->SetDictionary(n,selected);}
        auto selected=CefDictionaryValue::Create();DevToolsSetString(selected,"functionName",name);selected->SetBool("isBlockCoverage",function->GetBool("isBlockCoverage"));selected->SetList("ranges",selected_ranges);projected->SetDictionary(f,selected);
      }
      auto entry=CefDictionaryValue::Create();entry->SetString("scriptId",source.id);DevToolsSetString(entry,"url",source.url);DevToolsSetString(entry,"source",source.text);entry->SetList("functions",projected);entries->SetDictionary(entries->GetSize(),entry);
    }
  }else{
    if(record->measured->GetSize()>16384){fail("Native CSS coverage rule capacity exceeded");return;}std::map<std::string,std::vector<CSSCoverageRule>> rules;
    for(size_t n=0;n<record->measured->GetSize();++n){auto rule=record->measured->GetDictionary(n);if(!rule||rule->GetType("styleSheetId")!=VTYPE_STRING){fail("Invalid actual CSS coverage source identity");return;}const auto id=rule->GetString("styleSheetId").ToString();const auto source=record->sources.find(id);if(source==record->sources.end())continue;int start=0,end=0;if(!CoverageOffset(rule,"startOffset",start)||!CoverageOffset(rule,"endOffset",end)||end<start||size_t(end)>source->second.units||rule->GetType("used")!=VTYPE_BOOL){fail("Invalid actual CSS coverage rule");return;}rules[id].push_back({start,end,rule->GetBool("used")});}
    for(const auto& [id,source]:record->sources){
      if(!source.fetched){fail("Actual CSS coverage source was not captured");return;}std::vector<CSSCoverageRange> ranges;std::string error;if(!CSSCoverageRanges(rules[id],ranges,error)){fail(error);return;}auto selected=CefListValue::Create();for(const auto& range:ranges){auto item=CefDictionaryValue::Create();item->SetInt("start",range.start);item->SetInt("end",range.end);selected->SetDictionary(selected->GetSize(),item);}auto entry=CefDictionaryValue::Create();DevToolsSetString(entry,"url",source.url);DevToolsSetString(entry,"text",source.text);entry->SetList("ranges",selected);entries->SetDictionary(entries->GetSize(),entry);
    }
  }
  auto value=CefValue::Create();value->SetList(entries);if(!DevToolsLosslessJSON(value,record->output,2*1048576)){fail("Native coverage response exceeds 2 MiB");return;}if(!Reserve(record,record->output.size())){fail("Native coverage shared output payload capacity exceeded");return;}record->phase=Phase::Cleaning;Cleanup(record);
}
void CoverageControls::Impl::Cleanup(const std::shared_ptr<Record>& record){
  if(!Current(record)||record->phase!=Phase::Cleaning||record->physical||record->settling||closing_channel_)return;
  if(!record->cleanup_deadline)record->cleanup_deadline=hooks_.now()+30000;
  // Retained stop may return a sticky data error after already stopping.
  // Domain disable is the actual idempotent retirement fence in either state.
  if(!record->reset){record->precise=false;record->tracking=false;}
  std::string method;
  if(record->kind==Kind::JS){if(record->precise){record->precise=false;method="Profiler.stopPreciseCoverage";}else if(record->profiler){record->profiler=false;method="Profiler.disable";}else if(record->debugger){record->debugger=false;method="Debugger.disable";}}
  else if(record->tracking){record->tracking=false;method="CSS.stopRuleUsageTracking";}else if(record->css){record->css=false;method="CSS.disable";}else if(record->dom){record->dom=false;method="DOM.disable";}
  if(!method.empty()){
    Send(record,method,nullptr,record->cleanup_deadline,[this,record](bool ok,Dict,const std::string& error){if(!Current(record)){return;}if(!ok){Error(record,error.empty()?"Actual native coverage domain teardown failed":error);Owner(record,false,record->error);CloseChannel("Actual native coverage domain teardown failed: "+record->error);}});return;
  }
  records_.erase(record->kind);Owner(record,record->error.empty(),record->error,record->output);
}
std::string CoverageControls::SourceHash(const std::string& source){unsigned char digest[EVP_MAX_MD_SIZE];unsigned size=0;if(!EVP_Digest(source.data(),source.size(),digest,&size,EVP_sha256(),nullptr))return "";std::ostringstream hash;hash<<std::hex<<std::setfill('0');for(unsigned n=0;n<size;++n)hash<<std::setw(2)<<unsigned(digest[n]);return hash.str();}
CoverageControls::CoverageControls(Hooks hooks):impl_(std::make_shared<Impl>(std::move(hooks))){}
CoverageControls::~CoverageControls(){impl_->Closed();}
void CoverageControls::Handle(int owner,Dict params,double deadline,Reply reply){impl_->Handle(owner,params,deadline,std::move(reply));}
void CoverageControls::Event(const std::string& method,Dict params){impl_->Event(method,params);}
void CoverageControls::NativeScript(int context,const std::string& script){impl_->NativeScript(context,script);}
void CoverageControls::Navigated(){impl_->Navigated();}
bool CoverageControls::FinalReceipt(const std::string& action,std::string& output,std::string& error)const{return impl_->FinalReceipt(action,output,error);}
void CoverageControls::OutOfProcessFrameAttached(){impl_->OutOfProcessFrameAttached();}
void CoverageControls::Cancel(int owner){impl_->Cancel(owner);}
void CoverageControls::Abort(const std::string& action,const std::string& error){impl_->Abort(action,error);}
bool CoverageControls::Ready(const std::string& action)const{return impl_->Ready(action);}
void CoverageControls::Tick(double now){impl_->Tick(now);}
void CoverageControls::Closed(){impl_->Closed();}
size_t CoverageControls::PhysicalCount()const{return impl_->calls_.size();}
bool CoverageControls::NeedsTick()const{return !impl_->calls_.empty()||std::any_of(impl_->records_.begin(),impl_->records_.end(),[](const auto& item){return item.second->owner||!item.second->queue.empty();});}
void CoverageControls::Diagnostics(Dict output)const{size_t bytes=0,queued=0;for(const auto& [kind,record]:impl_->records_){bytes+=record->text_bytes;queued+=record->queue.size();}output->SetInt("nativeCoverageRecordings",int(impl_->records_.size()));output->SetInt("nativeCoveragePhysical",int(impl_->calls_.size()));output->SetInt("nativeCoverageQueuedSources",int(queued));output->SetDouble("nativeCoverageSourceBytes",double(bytes));}
} // namespace suitecut
