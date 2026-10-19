// SPDX-License-Identifier: MIT
#pragma once
#include "dom_domain_leases.h"
#include "snapshot_reference_controls.h"
#include "include/cef_values.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace suitecut {
// One genuine Chromium Overlay node-tool per actual Inspector session. Multiple
// arbitrary locator nodes are deliberately unsupported; never show only the
// last of several nodes while claiming the entire locator was highlighted.
class LocatorHighlights:public std::enable_shared_from_this<LocatorHighlights>{
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;using Document=SnapshotReferences::Document;
  using Reply=std::function<void(bool,const std::string&,const std::string&)>;
  using Ready=DOMDomainLeases::Ready;
  struct Hooks{
    std::function<double()> now;
    std::function<bool(const Document&)> current;
    std::function<bool(const std::string&,const std::string&,Dict,double,Ready)> send;
    std::function<void(const std::string&)> fatal;
    std::function<void()> wake;
    std::shared_ptr<DOMDomainLeases> dom;
    std::string name_space;
  };
  explicit LocatorHighlights(Hooks hooks):hooks_(std::move(hooks)){}
  void Show(int owner,uint64_t generation,Document document,int backend,double deadline,Reply reply){
    if(closed_||failed_||hooks_.name_space.empty()||hooks_.name_space.size()>64||!std::all_of(hooks_.name_space.begin(),hooks_.name_space.end(),[](char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9');})||owner<1||!generation||backend<1||!std::isfinite(deadline)||deadline<0||(deadline&&hooks_.now()>=deadline)||!hooks_.current(document)||sequence_==UINT64_MAX){reply(false,"Invalid, changed or expired native highlight request","");return;}
    auto found=sessions_.find(document.session);
    if(found==sessions_.end()){
      if(sessions_.size()>=32||GlobalResidents()>=128){reply(false,"Native highlight session capacity exceeded","");return;}
      auto state=std::make_shared<Session>();state->document=document;state->lease="highlight";found=sessions_.emplace(state->document.session,state).first;++GlobalResidents();
    }
    const auto state=found->second;
    if(state->busy||state->cleaning){reply(false,"Native highlight session is physically busy","");return;}
    // A current document check precedes every actual side effect. A frame that
    // shares a renderer session replaces that session's previous node tool.
    state->document=std::move(document);state->backend=backend;state->owner=owner;state->generation=generation;state->deadline=deadline;state->reply=std::move(reply);state->token=++sequence_;state->canceled=false;state->delivered=false;state->delivery=0;
    if(state->dom){Enable(state);return;}
    state->busy=true;const auto self=shared_from_this();
    hooks_.dom->Acquire(state->document.session,state->lease,deadline,[self,state](bool ok,const std::string& error){
      if(state->dead)return;
      state->busy=false;
      if(!ok){self->Fail(state,error.empty()?"Native highlight DOM domain acquisition failed":error);return;}
      state->dom=true;
      if(!self->Valid(state)){self->Cleanup(state);return;}
      self->Enable(state);
    });hooks_.wake();
  }
  void Empty(const std::string&session,double deadline,Reply reply){
    if(sequence_==UINT64_MAX){reply(false,"Native highlight identity exhausted","");return;}
    const auto token=++sequence_;Hide(session,"",deadline,[this,token,reply=std::move(reply)](bool ok,const std::string&error,const std::string&){reply(ok,error,ok?"{\"token\":\""+Token(token)+"\",\"empty\":true}":"");});
  }
  void Hide(const std::string& session,const std::string& token,double deadline,Reply reply){
    if(closed_){reply(false,"Native highlight channel is closed","");return;}
    if(!std::isfinite(deadline)||deadline<0||(deadline&&hooks_.now()>=deadline)){reply(false,"Native operation timed out","");return;}
    const auto found=sessions_.find(session);
    if(found==sessions_.end()||(!token.empty()&&token!=Token(found->second->token))){reply(true,"","{}");return;}
    const auto state=found->second;
    if(state->reply||state->hide_reply){reply(false,"Native highlight request is still in progress","");return;}
    state->hide_reply=std::move(reply);state->hide_deadline=deadline;state->canceled=true;Cleanup(state);hooks_.wake();
  }
  void Claim(const std::string&token,Reply reply){
    std::shared_ptr<Session> state;for(const auto&[id,candidate]:sessions_){(void)id;if(token==Token(candidate->token)){state=candidate;break;}}
    if(!state||state->canceled||state->cleaning||state->busy||!state->shown){reply(false,"Native highlight delivery identity is unavailable","");return;}
    if(!state->delivered&&((state->deadline&&hooks_.now()>=state->deadline)||(state->delivery&&hooks_.now()>=state->delivery))){state->canceled=true;Cleanup(state);reply(false,"Native operation timed out","");return;}
    if(!hooks_.current(state->document)){state->canceled=true;Cleanup(state);reply(false,"Native highlight document changed","");return;}
    state->delivered=true;state->delivery=0;reply(true,"","{}");
  }
  void Cancel(int owner,uint64_t generation){
    std::vector<std::shared_ptr<Session>> states;for(const auto&[id,state]:sessions_){(void)id;states.push_back(state);}
    for(const auto&state:states)if(state->owner==owner&&state->generation==generation&&(state->reply||!state->delivered)){state->reply={};state->canceled=true;Cleanup(state);}
  }
  void InvalidateFrame(const std::string& frame){
    std::vector<std::shared_ptr<Session>> states;for(const auto&[id,state]:sessions_){(void)id;if(state->document.frame==frame)states.push_back(state);}
    for(const auto&state:states){Owner(state,false,"Native highlight document changed");state->canceled=true;Cleanup(state);}
  }
  void Tick(){
    const auto now=hooks_.now();std::vector<std::shared_ptr<Session>> states;for(const auto&[id,state]:sessions_){(void)id;states.push_back(state);}
    for(const auto& state:states){
      if(state->shown&&!state->delivered&&state->delivery&&now>=state->delivery){state->canceled=true;}
      if(state->reply&&state->deadline&&now>=state->deadline){Owner(state,false,"Native operation timed out");state->canceled=true;}
      if(state->hide_reply&&state->hide_deadline&&now>=state->hide_deadline){auto reply=std::move(state->hide_reply);reply(false,"Native operation timed out","");}
      if(!hooks_.current(state->document)){if(state->reply)Owner(state,false,"Native highlight document changed");state->canceled=true;}
      if(state->canceled)Cleanup(state);
    }
  }
  void Detached(const std::string& session){
    const auto found=sessions_.find(session);if(found==sessions_.end())return;
    const auto state=found->second;sessions_.erase(found);--GlobalResidents();state->dead=true;
    Owner(state,false,"Native highlight target channel destroyed");if(state->hide_reply){auto reply=std::move(state->hide_reply);reply(true,"","{}");}
  }
  void Closed(){if(closed_)return;closed_=true;while(!sessions_.empty())Detached(sessions_.begin()->first);}
  bool SessionOwned(const std::string&session)const{return sessions_.contains(session);}
  std::string SessionForToken(const std::string& token)const{for(const auto&[session,state]:sessions_)if(token==Token(state->token))return session;return {}; }
  size_t Active()const{return std::count_if(sessions_.begin(),sessions_.end(),[](const auto& entry){return entry.second->shown&&!entry.second->cleaning;});}
  size_t Resident()const{return sessions_.size();}
  bool NeedsTick()const{return std::any_of(sessions_.begin(),sessions_.end(),[](const auto& entry){return bool(entry.second->reply)||bool(entry.second->hide_reply)||entry.second->cleaning||(!entry.second->delivered&&entry.second->delivery);});}
 private:
  static size_t& GlobalResidents(){static size_t value=0;return value;}
  std::string Token(uint64_t token)const{return "h"+hooks_.name_space+"_"+std::to_string(token);}
  struct Session{Document document;std::string lease;int owner=0,backend=0;uint64_t generation=0,token=0;double deadline=0,hide_deadline=0,delivery=0;bool busy=false,dom=false,overlay=false,shown=false,cleaning=false,canceled=false,dead=false,delivered=false;Reply reply,hide_reply;};
  bool Valid(const std::shared_ptr<Session>& s){
    if(s->dead)return false;
    if(s->deadline&&hooks_.now()>=s->deadline){Owner(s,false,"Native operation timed out");s->canceled=true;}
    if(!hooks_.current(s->document)){Owner(s,false,"Native highlight document changed");s->canceled=true;}
    return !s->canceled;
  }
  void Owner(const std::shared_ptr<Session>&s,bool ok,const std::string&error){if(!s->reply)return;auto reply=std::move(s->reply);reply(ok,error,ok?"{\"token\":\""+Token(s->token)+"\"}":"");}
  void Fail(const std::shared_ptr<Session>&s,const std::string&error){Owner(s,false,error);s->canceled=true;Cleanup(s);}
  void Send(const std::shared_ptr<Session>&s,const std::string&method,Dict params,double deadline,Ready done){
    s->busy=true;const auto self=shared_from_this();auto settled=std::make_shared<bool>(false);
    auto callback=[self,s,settled,done=std::move(done)](bool ok,const std::string&error){if(*settled)return;*settled=true;if(s->dead)return;s->busy=false;done(ok,error);};
    if(!hooks_.send(s->document.session,method,params,deadline,callback))callback(false,"Native highlight command was not submitted");
    hooks_.wake();
  }
  void Enable(const std::shared_ptr<Session>&s){
    if(!Valid(s)){Cleanup(s);return;}if(s->overlay){Draw(s);return;}
    Send(s,"Overlay.enable",nullptr,s->deadline,[self=shared_from_this(),s](bool ok,const std::string&error){if(ok)s->overlay=true;if(!ok){self->Fail(s,error);return;}if(!self->Valid(s)){self->Cleanup(s);return;}self->Draw(s);});
  }
  void Draw(const std::shared_ptr<Session>&s){
    if(!Valid(s)){Cleanup(s);return;}
    auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",s->backend);auto config=CefDictionaryValue::Create();
    auto content=CefDictionaryValue::Create();content->SetInt("r",124);content->SetInt("g",58);content->SetInt("b",237);content->SetDouble("a",0.3);config->SetDictionary("contentColor",content);
    auto border=CefDictionaryValue::Create();border->SetInt("r",124);border->SetInt("g",58);border->SetInt("b",237);border->SetDouble("a",0.85);config->SetDictionary("borderColor",border);config->SetBool("showInfo",true);params->SetDictionary("highlightConfig",config);
    Send(s,"Overlay.highlightNode",params,s->deadline,[self=shared_from_this(),s](bool ok,const std::string&error){if(ok)s->shown=true;if(!ok){self->Fail(s,error);return;}if(!self->Valid(s)){self->Cleanup(s);return;}s->delivery=self->hooks_.now()+1000;if(s->deadline)s->delivery=std::min(s->delivery,s->deadline);self->Owner(s,true,"");});
  }
  void Cleanup(const std::shared_ptr<Session>&s){
    if(s->dead||s->busy)return;
    s->cleaning=true;
    // Cleanup has its own bounded physical settlement grace. It never extends
    // the caller's original publication deadline, which Tick already settles.
    const auto self=shared_from_this();const auto deadline=hooks_.now()+30000;
    if(s->shown){s->shown=false;Send(s,"Overlay.hideHighlight",nullptr,deadline,[self,s](bool ok,const std::string&error){if(!ok){self->Fatal(s,error);return;}self->Cleanup(s);});return;}
    if(s->overlay){s->overlay=false;Send(s,"Overlay.disable",nullptr,deadline,[self,s](bool ok,const std::string&error){if(!ok){self->Fatal(s,error);return;}self->Cleanup(s);});return;}
    if(s->dom){s->dom=false;s->busy=true;hooks_.dom->Release(s->document.session,s->lease,[self,s](bool ok,const std::string&error){if(s->dead)return;s->busy=false;if(!ok){self->Fatal(s,error);return;}self->Cleanup(s);});return;}
    sessions_.erase(s->document.session);--GlobalResidents();s->dead=true;if(s->hide_reply){auto reply=std::move(s->hide_reply);const bool expired=s->hide_deadline&&hooks_.now()>=s->hide_deadline;reply(!expired,expired?"Native operation timed out":"","{}");}
  }
  void Fatal(const std::shared_ptr<Session>&s,const std::string&error){failed_=true;Owner(s,false,error);if(s->hide_reply){auto reply=std::move(s->hide_reply);reply(false,error,"");}hooks_.fatal(error.empty()?"Native highlight physical cleanup failed":error);}
  Hooks hooks_;std::map<std::string,std::shared_ptr<Session>>sessions_;uint64_t sequence_=0;bool closed_=false,failed_=false;
};
} // namespace suitecut
