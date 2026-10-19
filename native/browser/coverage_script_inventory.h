// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "include/cef_parser.h"
#include "devtools_utf16.h"
#include "debugger_script_metadata.h"
namespace suitecut {
// Actual Inspector metadata only, independent of whether coverage currently owns
// a Profiler lease. No source bytes, execution counts, or protocol ACKs are made
// by this inventory. Limits cover retained JSON bytes; not an absolute heap cap.
class CoverageScriptInventory {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  static constexpr size_t kSessions=2049,kScripts=8192,kPerSession=2048,kBytes=8*1048576,kEntryBytes=65536;
  void Observe(const std::string& method,Dict params,const std::string& session){
    if(method=="Runtime.executionContextsCleared"){Detached(session);return;}
    if(method=="Runtime.executionContextDestroyed"){
      if(!params||params->GetType("executionContextId")!=VTYPE_INT)return;
      const auto found=sessions_.find(session);if(found==sessions_.end())return;
      found->second.revisions.erase(params->GetInt("executionContextId"));
      for(auto it=found->second.scripts.begin();it!=found->second.scripts.end();)if(it->second.context==params->GetInt("executionContextId")){bytes_-=it->second.json.size();--scripts_;it=found->second.scripts.erase(it);}else ++it;
      return;
    }
    if(method!="Debugger.scriptParsed"||!params)return;
    if(session.size()>128){failed_=true;return;}
    auto found=sessions_.find(session);
    if(found==sessions_.end()){if(sessions_.size()>=kSessions){failed_=true;return;}found=sessions_.emplace(session,Session{}).first;}
    auto& target=found->second;if(target.failed)return;
    if(params->GetType("executionContextId")!=VTYPE_INT||params->GetInt("executionContextId")<=0)return;
    if(params->GetType("scriptLanguage")==VTYPE_STRING&&params->GetString("scriptLanguage")!="JavaScript")return;
    if(params->GetType("scriptId")!=VTYPE_STRING||params->GetString("scriptId").length()>256){target.failed=true;return;}
    const auto id=params->GetString("scriptId").ToString();if(id.empty()){target.failed=true;return;}
    auto selected=CefDictionaryValue::Create();selected->SetString("scriptId",id);selected->SetInt("executionContextId",params->GetInt("executionContextId"));
    for(const char* key:{"url","embedderName","hash","scriptLanguage"})if(params->HasKey(key)){
      const auto value=params->GetValue(key);if(!BoundedString(value)){target.failed=true;return;}selected->SetValue(key,value->Copy());
    }
    if(!selected->HasKey("url")){target.failed=true;return;}
    if(params->HasKey("hasSourceURL")){if(params->GetType("hasSourceURL")!=VTYPE_BOOL){target.failed=true;return;}selected->SetBool("hasSourceURL",params->GetBool("hasSourceURL"));}
    auto boxed=CefValue::Create();boxed->SetDictionary(selected);const auto json=CefWriteJSON(boxed,JSON_WRITER_DEFAULT).ToString();
    const auto prior=target.scripts.find(id);const auto old=prior==target.scripts.end()?0:prior->second.json.size();
    if(json.empty()||json.size()>kEntryBytes||json.size()>kBytes-(bytes_-old)||(prior==target.scripts.end()&&(scripts_>=kScripts||target.scripts.size()>=kPerSession))){target.failed=true;return;}
    const int context=params->GetInt("executionContextId");
    const int previous_context=prior==target.scripts.end()?0:prior->second.context;
    const bool native=prior!=target.scripts.end()&&previous_context==context&&prior->second.native_owned;
    const bool changed=prior==target.scripts.end()||previous_context!=context||prior->second.json!=json;
    const bool moved=previous_context&&previous_context!=context;
    const uint64_t bumps=changed+(moved?1:0);
    if(bumps>UINT64_MAX-revision_serial_){failed_=true;return;}
    if(changed)Bump(target,context);
    if(moved)Bump(target,previous_context);
    bytes_=bytes_-old+json.size();if(prior==target.scripts.end())++scripts_;
    target.scripts[id]=Script{context,json,native};
    if(moved){
      if(std::none_of(target.scripts.begin(),target.scripts.end(),[&](const auto&entry){return entry.second.context==previous_context;}))target.revisions.erase(previous_context);
    }
  }
  // Retention needs native-owned identities too, after the live realm is gone.
  // Their ownership bit comes from MarkNative, never authored script metadata.
  bool Snapshot(const std::string& session,std::vector<Dict>& output,std::string& error,double deadline=0,double (*now)()=nullptr,bool include_native=false)const{
    output.clear();const auto found=sessions_.find(session);
    if(failed_||(found!=sessions_.end()&&found->second.failed)){error="Actual native coverage script inventory metadata capacity or identity failed";return false;}
    if(found==sessions_.end())return true;
    for(const auto& [id,script]:found->second.scripts){if(script.native_owned&&!include_native)continue;if(deadline>0&&now&&now()>=deadline){output.clear();error="Native coverage original deadline expired during inventory seed";return false;}auto value=CefParseJSON(script.json,JSON_PARSER_RFC);auto entry=value?value->GetDictionary():nullptr;if(!entry){output.clear();error="Retained native script inventory JSON is unavailable";return false;}auto retained=entry->Copy(false);if(script.native_owned)retained->SetBool("suitecutNativeOwned",true);output.push_back(retained);}
    return true;
  }
  // Only the caller's validated native getProperties FunctionLocation receipt
  // may invoke this. The inventory itself never guesses ownership from bytes.
  bool MarkNative(const std::string& session,int context,const std::string& id){
    if(failed_||context<=0||id.empty()||id.size()>256)return false;
    const auto target=sessions_.find(session);if(target==sessions_.end()||target->second.failed)return false;
    const auto script=target->second.scripts.find(id);if(script==target->second.scripts.end()||script->second.context!=context)return false;
    if(!script->second.native_owned&&!Bump(target->second,context))return false;
    script->second.native_owned=true;return true;
  }
  // Exact real scriptParsed identity for an internal native debugger source
  // read/pause. Native scripts remain queryable here; coverage Snapshot still
  // excludes attested native scripts. No URL/source-byte ownership inference.
  bool Contains(const std::string& session,int context,const std::string& id)const{
    if(failed_||context<=0||id.empty()||id.size()>256)return false;
    const auto target=sessions_.find(session);if(target==sessions_.end()||target->second.failed)return false;
    const auto script=target->second.scripts.find(id);
    return script!=target->second.scripts.end()&&script->second.context==context;
  }
  // Actual scriptParsed association for native scoped pause classification.
  // Attestation is a deny-only fact, never proof of author mutation authority.
  bool ScriptContext(const std::string&session,const std::string&id,int&context,bool&attested)const{
    context=0;attested=false;if(failed_||id.empty()||id.size()>256)return false;
    const auto target=sessions_.find(session);if(target==sessions_.end()||target->second.failed)return false;
    const auto script=target->second.scripts.find(id);if(script==target->second.scripts.end())return false;
    context=script->second.context;attested=script->second.native_owned;return true;
  }
  // A breakpoint may target only this actual current script record. Attested
  // native FunctionLocation identities are never exposed as author candidates.
  bool BreakpointCandidate(const std::string& session,int context,const std::string& id,Dict& output)const{
    output=nullptr;if(!Contains(session,context,id))return false;
    const auto&script=sessions_.at(session).scripts.at(id);if(script.native_owned)return false;
    auto value=CefParseJSON(script.json,JSON_PARSER_RFC);auto entry=value?value->GetDictionary():nullptr;
    if(!entry)return false;
    output=entry->Copy(false);return true;
  }
  struct Page {uint64_t revision=0;std::string json="[]",last;size_t count=0;bool more=false;};
  // A revision is only an actual selected-metadata change stamp. Cursor/page
  // authority still belongs to the owning debugger's exact document/session.
  bool ScriptPage(const std::string&session,int context,const std::string&after,
                  const uint64_t*expected,Page&output,std::string&error,
                  double deadline=0,double(*now)()=nullptr)const{
    const std::string position=after;
    const bool check_revision=expected!=nullptr;const uint64_t expected_revision=expected?*expected:0;
    output=Page{};
    if(context<=0||failed_){error="Actual native debugger script inventory is unavailable";return false;}
    const auto target=sessions_.find(session);
    if(target!=sessions_.end()&&target->second.failed){error="Actual native debugger script inventory capacity failed";return false;}
    const auto revision=target==sessions_.end()?0:ContextRevision(target->second,context);
    if(check_revision&&expected_revision!=revision){error="Native debugger script inventory changed; restart enumeration";return false;}
    output.revision=revision;output.json="[";
    if(target!=sessions_.end())for(auto it=target->second.scripts.upper_bound(position);it!=target->second.scripts.end();++it){
      if(deadline>0&&now&&now()>=deadline){output=Page{};error="Native debugger original deadline expired during script page";return false;}
      if(it->second.context!=context)continue;
      if(output.count==64){output.more=true;break;}
      auto value=CefParseJSON(it->second.json,JSON_PARSER_RFC);auto metadata=value?value->GetDictionary():nullptr;std::string entry;
      if(!metadata||!DebuggerScriptMetadata::Build(metadata,it->second.native_owned,entry)){output=Page{};error="Actual native debugger script metadata exceeds its projection bound";return false;}
      const size_t needed=entry.size()+(output.count?1:0)+1;
      if(needed>(512*1024-1024)-output.json.size()){
        if(!output.count){output=Page{};error="Actual native debugger script entry exceeds page byte bound";return false;}
        output.more=true;break;
      }
      if(output.count)output.json+=',';
      output.json+=entry;output.last=it->first;++output.count;
    }
    output.json+=']';
    if(deadline>0&&now&&now()>=deadline){output=Page{};error="Native debugger original deadline expired before script page publication";return false;}
    return true;
  }
  void Detached(const std::string& session){const auto found=sessions_.find(session);if(found==sessions_.end())return;for(const auto& [id,script]:found->second.scripts){bytes_-=script.json.size();--scripts_;}sessions_.erase(found);}
  // A real renderer's V8 contexts are dead, but this never retires Inspector calls.
  void Clear(){sessions_.clear();bytes_=scripts_=0;failed_=false;}
  size_t Bytes()const{return bytes_;}size_t Scripts()const{return scripts_;}
 private:
  static bool BoundedString(CefRefPtr<CefValue> value){
    if(!value){return false;}
    if(value->GetType()==VTYPE_STRING)return value->GetString().length()<=32768;
    if(value->GetType()!=VTYPE_DICTIONARY){return false;}
    const auto tag=value->GetDictionary();
    if(tag->GetSize()!=1||tag->GetType(kDevToolsUTF16Tag)!=VTYPE_STRING||tag->GetString(kDevToolsUTF16Tag).length()>4*32768)return false;
    std::u16string units;return DevToolsReadTag(value,units);
  }
  struct Script{int context;std::string json;bool native_owned=false;};struct Session{std::map<std::string,Script> scripts;std::map<int,uint64_t>revisions;bool failed=false;};
  static uint64_t ContextRevision(const Session&session,int context){const auto found=session.revisions.find(context);return found==session.revisions.end()?0:found->second;}
  bool Bump(Session&session,int context){if(revision_serial_==UINT64_MAX){failed_=true;return false;}session.revisions[context]=++revision_serial_;return true;}
  std::map<std::string,Session> sessions_;size_t bytes_=0,scripts_=0;uint64_t revision_serial_=0;bool failed_=false;
};
}
