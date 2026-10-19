// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "debugger_domains.h"
#include "coverage_controls.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <map>
#include <utility>
#include <vector>
using Domain=suitecut::DebuggerDomains;
using Dict=Domain::Dict;
struct Harness {
  struct Command {std::string session,method;Dict params;double deadline;Domain::Reply reply;};
  double now=1,pause_deadline=100;size_t wakes=0,expired=0;std::deque<Command> commands;std::vector<std::string> closes,expired_markers;
  std::shared_ptr<Domain> domains;std::function<void()> on_expired;
  Harness(){Domain::Hooks hooks;hooks.now=[this](){return now;};hooks.wake=[this](){++wakes;};hooks.send=[this](const std::string& session,const std::string& method,Dict params,double deadline,Domain::Reply reply){commands.push_back({session,method,params?params->Copy(false):nullptr,deadline,std::move(reply)});return true;};hooks.pause_deadline=[this](const std::string&,Dict){return pause_deadline;};hooks.pause_expired=[this](uint64_t,Dict original,const std::string&){assert(original);expired_markers.push_back(original->GetString("actualFrameMarker").ToString());++expired;if(on_expired)on_expired();};hooks.close_channel=[this](const std::string& session,const std::string&){closes.push_back(session);};domains=std::make_shared<Domain>(std::move(hooks));}
  Command Take(const std::string& method){assert(!commands.empty());auto c=std::move(commands.front());commands.pop_front();if(c.method!=method)std::cerr<<"Expected "<<method<<" but got "<<c.method<<"\n";assert(c.method==method);return c;}
  void Ack(const std::string& method,bool ok=true,Dict data=nullptr){auto c=Take(method);c.reply(ok,data,ok?"":"actual test protocol error");}
  void Cache(bool bounded){assert(!commands.empty()&&commands.front().method=="Debugger.enable"&&commands.front().params);auto p=commands.front().params;assert(bounded?(p->GetSize()==1&&p->GetType("maxScriptsCacheSize")==VTYPE_INT&&p->GetInt("maxScriptsCacheSize")==Domain::kFactoryScriptCacheBytes):p->GetSize()==0);Ack("Debugger.enable");}
  void Skip(bool skip){assert(!commands.empty()&&commands.front().params->GetBool("skip")==skip);Ack("Debugger.setSkipAllPauses");}
  void Acquire(uint64_t id,Domain::Kind kind,size_t& successes,const std::string& session="",double deadline=100){domains->Acquire(session,id,kind,deadline,[&successes](bool ok,const std::string&){if(ok)++successes;});}
  static Dict Breakpoint(){auto p=CefDictionaryValue::Create();p->SetString("url","suitecut:init-snapshot");p->SetInt("lineNumber",0);p->SetInt("columnNumber",0);return p;}
  static Dict BreakpointReply(){auto p=CefDictionaryValue::Create();p->SetString("breakpointId","br:1:0:0:suitecut:init-snapshot");return p;}
  static Dict Pause(bool owned=true){auto p=CefDictionaryValue::Create();auto hits=CefListValue::Create();if(owned)hits->SetString(0,"br:1:0:0:suitecut:init-snapshot");p->SetList("hitBreakpoints",hits);p->SetString("reason",owned?"other":"debugCommand");return p;}
};
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  size_t groups=0;
  { // Pool real domain/skip ACKs and retain capture->release receipt chain.
    Harness h;size_t ready=0,arms=0,release=0,captures=0,resumed=0;Domain::Resume resume;
    h.Acquire(1,Domain::Kind::Coverage,ready);assert(ready==0);h.Ack("Debugger.enable");h.Skip(true);assert(ready==1&&!h.domains->NeedsTick());
    h.Acquire(2,Domain::Kind::Factory,ready);h.Skip(false);assert(ready==2);
    h.domains->Arm(2,Harness::Breakpoint(),[&](Dict actual,double deadline,Domain::Resume once){assert(actual->GetString("reason")=="other"&&deadline==100);++captures;resume=std::move(once);},[&](bool ok,const std::string&){assert(ok);++arms;});
    h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());assert(arms==1&&!h.domains->NeedsTick());
    assert(h.domains->Conflicts("","Debugger.disable")&&!h.domains->Conflicts("","Runtime.evaluate"));
    assert(h.domains->Event("Debugger.paused",Harness::Pause(),""));assert(captures==1&&h.commands.empty());
    const auto first=h.domains->Hold(2);assert(first);
    h.domains->Release(1,200,[&](bool ok,const std::string&){assert(ok);++release;});h.Cache(true);
    h.domains->Release(2,200,[&](bool ok,const std::string&){assert(ok);++release;});
    resume([&](bool ok,const std::string&){assert(ok);++resumed;});assert(resumed==0);h.Ack("Debugger.resume");assert(resumed==1&&h.commands.empty());
    const auto group_release=h.domains->Hold(2);assert(group_release);h.domains->Retire(first);assert(h.commands.empty());
    h.domains->Retire(group_release);assert(!h.commands.empty());h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");
    assert(release==2&&h.domains->PhysicalCount()==0&&!h.domains->Conflicts("","Debugger.enable"));++groups;
  }
  { // A canceled accepted enable is retained, then actually disabled.
    Harness h;size_t failed=0,released=0;
    h.domains->Acquire("",1,Domain::Kind::Factory,100,[&](bool ok,const std::string&){assert(!ok);++failed;});
    h.domains->Release(1,200,[&](bool ok,const std::string&){assert(ok);++released;});
    assert(failed==1&&released==0&&h.domains->PhysicalCount()==1);
    h.Ack("Debugger.enable");assert(released==0);h.Ack("Debugger.disable");assert(released==1&&h.domains->PhysicalCount()==0);++groups;
  }
  { // Late ACK before Tick cannot report startup success or drop enabled state.
    Harness h;size_t failures=0;
    h.domains->Acquire("",1,Domain::Kind::Coverage,10,[&](bool ok,const std::string&){assert(!ok);++failures;});
    h.now=11;h.Ack("Debugger.enable");assert(failures==1);h.Ack("Debugger.disable");assert(h.domains->PhysicalCount()==0&&h.commands.empty());++groups;
  }
  { // Coverage release cannot disable an armed persistent factory.
    Harness h;size_t ready=0,released=0;
    h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume){assert(false);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());
    h.now=1000;h.domains->Tick(h.now);assert(!h.domains->NeedsTick()&&h.closes.empty());
    h.Acquire(2,Domain::Kind::Coverage,ready,"",2000);assert(ready==1);h.Cache(false);assert(ready==2&&h.commands.empty());
    h.domains->Release(2,3000,[&](bool ok,const std::string&){assert(ok);++released;});assert(!released);h.Cache(true);assert(released==1&&h.commands.empty());
    h.domains->Release(1,3000,[&](bool ok,const std::string&){assert(ok);++released;});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");assert(released==2);++groups;
  }
  { // Owned capture expiry resumes once; timeout does not free accepted receipt.
    Harness h;size_t ready=0,logical=0,captures=0;Domain::Resume resume;
    h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[&](Dict,double,Domain::Resume once){++captures;resume=std::move(once);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());
    h.pause_deadline=20;h.domains->Event("Debugger.paused",Harness::Pause(),"");const auto held=h.domains->Hold(1);assert(held);
    resume([&](bool ok,const std::string&){assert(!ok);++logical;});assert(h.domains->PhysicalCount()==3);
    h.now=21;h.domains->Tick(h.now);assert(logical==1&&h.expired==1&&h.domains->PhysicalCount()==3);
    h.Ack("Debugger.resume");assert(logical==1&&h.domains->PhysicalCount()==1);
    h.domains->Retire(held);assert(h.domains->PhysicalCount()==0);
    h.pause_deadline=60;h.domains->Event("Debugger.paused",Harness::Pause(),"");assert(captures==2);resume([](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");
    h.domains->Release(1,100,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");++groups;
  }
  { // Author pauses are resumed; genuine target/session closure retires receipts.
    Harness h;size_t ready=0;h.Acquire(1,Domain::Kind::Coverage,ready);h.Ack("Debugger.enable");h.Skip(true);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(h.domains->PhysicalCount()==2);auto late=h.Take("Debugger.resume");h.domains->Detached("");assert(h.domains->PhysicalCount()==0&&!h.domains->Conflicts("","Debugger.enable"));late.reply(true,nullptr,"");assert(h.commands.empty());++groups;
  }
  { // Physical command cap queues admission; timeout never clears pending ACKs.
    Harness h;size_t failures=0;
    for(uint64_t i=1;i<=65;++i)h.domains->Acquire(std::to_string(i),i,Domain::Kind::Coverage,10,[&](bool ok,const std::string&){assert(!ok);++failures;});
    assert(h.domains->PhysicalCount()==64&&h.commands.size()==64);
    h.now=11;h.domains->Tick(h.now);assert(failures==65&&h.domains->PhysicalCount()==64);
    h.now=30011;h.domains->Tick(h.now);assert(h.closes.size()==64&&h.domains->PhysicalCount()==64&&!h.domains->NeedsTick());
    h.domains->Closed();assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // Bounded capture receipts include logical-release cleanup chaining.
    Harness h;size_t ready=0;h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    std::vector<uint64_t> holds;for(size_t i=0;i<128;++i){auto receipt=h.domains->Hold(1);assert(receipt);holds.push_back(receipt);}assert(!h.domains->Hold(1));
    h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.domains->Retire(holds.back());holds.pop_back();auto followup=h.domains->Hold(1);assert(followup);for(auto receipt:holds)h.domains->Retire(receipt);assert(h.commands.empty());h.domains->Retire(followup);h.Ack("Debugger.disable");assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // New lease during accepted disable waits ACK and genuine re-enable.
    Harness h;size_t ready=0,released=0;h.Acquire(1,Domain::Kind::Coverage,ready);h.Ack("Debugger.enable");h.Skip(true);
    h.domains->Release(1,200,[&](bool ok,const std::string&){assert(ok);++released;});auto disable=h.Take("Debugger.disable");h.Acquire(2,Domain::Kind::Coverage,ready);assert(ready==1&&h.commands.empty());disable.reply(true,nullptr,"");h.Ack("Debugger.enable");h.Skip(true);assert(ready==2&&released==1);h.domains->Closed();++groups;
  }
  { // Exact session boundaries never retire another session's accepted command.
    Harness h;size_t ready=0,failed=0;
    h.domains->Acquire("a",1,Domain::Kind::Coverage,100,[&](bool ok,const std::string&){assert(!ok);++failed;});
    h.Acquire(2,Domain::Kind::Coverage,ready,"b");auto a=h.Take("Debugger.enable");auto b=h.Take("Debugger.enable");h.domains->Detached("a");assert(failed==1&&h.domains->PhysicalCount()==1);a.reply(true,nullptr,"");b.reply(true,nullptr,"");h.Skip(true);assert(ready==1);h.domains->Closed();++groups;
  }
  { // A real pause may precede its breakpoint-install reply; do not autoresume it.
    Harness h;size_t ready=0,captured=0,armed=0;Domain::Resume resume;
    h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[&](Dict,double deadline,Domain::Resume once){assert(deadline==50);++captured;resume=std::move(once);},[&](bool ok,const std::string&){assert(ok);++armed;});
    h.pause_deadline=50;h.domains->Event("Debugger.paused",Harness::Pause(),"");assert(captured==0&&h.domains->PhysicalCount()==2);
    h.pause_deadline=500;h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());assert(armed==1&&captured==1&&h.commands.empty());
    resume([](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // Actual coverage controller leases the domain without disabling factory.
    Harness h;size_t ready=0;h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume){assert(false);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());
    suitecut::CoverageControls::Hooks hooks;hooks.now=[&](){return h.now;};hooks.root_target_only=[](){return true;};hooks.author_context=[](int){return true;};hooks.send=[&](const std::string& method,Dict params,double deadline,Domain::Reply reply){h.commands.push_back({"",method,params,deadline,std::move(reply)});return true;};
    hooks.debugger_acquire=[&](double deadline,Domain::Reply reply){h.domains->Acquire("",2,Domain::Kind::Coverage,deadline,[reply=std::move(reply)](bool ok,const std::string& error){reply(ok,nullptr,error);});return true;};
    hooks.debugger_release=[&](double deadline,Domain::Reply reply){h.domains->Release(2,deadline,[reply=std::move(reply)](bool ok,const std::string& error){reply(ok,nullptr,error);});return true;};
    suitecut::CoverageControls coverage(std::move(hooks));auto params=CefDictionaryValue::Create();params->SetString("action","startJS");size_t started=0,stopped=0;
    coverage.Handle(1,params,100,[&](bool ok,const std::string&,const std::string&){assert(ok);++started;});h.Cache(false);h.Ack("Profiler.enable");h.Ack("Profiler.startPreciseCoverage");assert(started==1&&h.commands.empty());
    coverage.Event("Debugger.paused",Harness::Pause());assert(h.commands.empty());
    params->SetString("action","stopJS");coverage.Handle(2,params,100,[&](bool ok,const std::string&,const std::string& json){assert(ok&&json=="[]");++stopped;});auto actual=CefDictionaryValue::Create();actual->SetList("result",CefListValue::Create());h.Ack("Profiler.takePreciseCoverage",true,actual);h.Ack("Profiler.stopPreciseCoverage");h.Ack("Profiler.disable");h.Cache(true);
    assert(stopped==1&&coverage.PhysicalCount()==0&&h.commands.empty()&&h.domains->Conflicts("","Debugger.disable"));
    h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // Coverage cancellation during pooled startup retains its acquire receipt.
    Harness h;size_t ready=0;h.Acquire(1,Domain::Kind::Factory,ready);
    suitecut::CoverageControls::Hooks hooks;hooks.now=[&](){return h.now;};hooks.root_target_only=[](){return true;};hooks.author_context=[](int){return true;};hooks.send=[](const std::string&,Dict,double,Domain::Reply){assert(false);return false;};
    hooks.debugger_acquire=[&](double deadline,Domain::Reply reply){h.domains->Acquire("",2,Domain::Kind::Coverage,deadline,[reply=std::move(reply)](bool ok,const std::string& error){reply(ok,nullptr,error);});return true;};
    hooks.debugger_release=[&](double deadline,Domain::Reply reply){h.domains->Release(2,deadline,[reply=std::move(reply)](bool ok,const std::string& error){reply(ok,nullptr,error);});return true;};
    suitecut::CoverageControls coverage(std::move(hooks));auto params=CefDictionaryValue::Create();params->SetString("action","startJS");size_t failed=0;coverage.Handle(1,params,100,[&](bool ok,const std::string&,const std::string&){assert(!ok);++failed;});coverage.Cancel(1);assert(failed==1&&coverage.PhysicalCount()==1&&h.domains->PhysicalCount()==1);
    h.Ack("Debugger.enable");h.Cache(false);h.Skip(false);h.Cache(true);assert(ready==1&&coverage.Ready("startJS")&&coverage.PhysicalCount()==0&&h.commands.empty());
    h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.disable");assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // Expiry before real arm ACK cannot admit a late factory capture.
    Harness h;size_t ready=0,captured=0;h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[&](Dict,double,Domain::Resume){++captured;},[](bool ok,const std::string&){assert(ok);});
    h.pause_deadline=10;h.domains->Event("Debugger.paused",Harness::Pause(),"");h.now=11;h.domains->Tick(h.now);assert(h.expired==1&&h.domains->PhysicalCount()==2);
    h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());assert(captured==0&&h.expired==1&&h.domains->PhysicalCount()==2);h.Ack("Debugger.resume");
    h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // Reentrant actual callbacks safely settle state and once-only resume.
    double now=1;size_t sends=0,ready=0;Domain::Hooks hooks;hooks.now=[&](){return now;};hooks.pause_deadline=[](const std::string&,Dict){return 100;};hooks.send=[&](const std::string&,const std::string& method,Dict,double,Domain::Reply reply){++sends;reply(true,method=="Debugger.setBreakpointByUrl"?Harness::BreakpointReply():nullptr,"");return true;};Domain domains(std::move(hooks));
    domains.Acquire("",1,Domain::Kind::Factory,100,[&](bool ok,const std::string&){assert(ok);++ready;});assert(ready==1&&sends==2);
    domains.Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume resume){resume([](bool ok,const std::string&){assert(ok);});resume([](bool ok,const std::string&){assert(!ok);});},[](bool ok,const std::string&){assert(ok);});
    domains.Event("Debugger.paused",Harness::Pause(),"");assert(domains.PhysicalCount()==0);domains.Release(1,200,[](bool ok,const std::string&){assert(ok);});assert(domains.PhysicalCount()==0&&!domains.NeedsTick()&&sends==6);++groups;
  }
  { // Logical release resumes a paused lease once all real capture receipts retire.
    Harness h;size_t ready=0,released=0;h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume){},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());h.domains->Event("Debugger.paused",Harness::Pause(),"");auto hold=h.domains->Hold(1);assert(hold);
    h.domains->Release(1,200,[&](bool ok,const std::string&){assert(ok);++released;});assert(h.commands.empty());h.domains->Retire(hold);assert(released==0);h.Ack("Debugger.resume");h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");assert(released==1&&h.domains->PhysicalCount()==0);++groups;
  }
  { // Raw ownership has real receipt lifetime and never autoresumes its pauses.
    Harness h;size_t failed=0;auto raw=h.domains->BeginRaw("raw","Debugger.enable",100);assert(raw&&h.domains->PhysicalCount()==1);
    h.domains->Acquire("raw",1,Domain::Kind::Coverage,100,[&](bool ok,const std::string& error){assert(!ok&&error.find("raw Inspector ownership")!=std::string::npos);++failed;});assert(failed==1);
    assert(!h.domains->Event("Debugger.paused",Harness::Pause(false),"raw")&&h.commands.empty());h.domains->EndRaw(raw,true);assert(h.domains->PhysicalCount()==0&&!h.domains->NeedsTick());
    h.domains->Acquire("raw",2,Domain::Kind::Factory,100,[&](bool ok,const std::string&){assert(!ok);++failed;});assert(failed==2);
    auto disable=h.domains->BeginRaw("raw","Debugger.disable",100);assert(disable);h.domains->EndRaw(disable,true);size_t ready=0;h.Acquire(3,Domain::Kind::Coverage,ready,"raw");h.Ack("Debugger.enable");h.Skip(true);assert(ready==1&&!h.domains->BeginRaw("raw","Debugger.disable",100));h.domains->Closed();++groups;
  }
  { // A failed raw dispatch does not fabricate a persistent enabled domain.
    Harness h;auto raw=h.domains->BeginRaw("","Debugger.enable",100);assert(raw);h.domains->EndRaw(raw,false,false);size_t ready=0;h.Acquire(1,Domain::Kind::Coverage,ready);h.Ack("Debugger.enable");h.Skip(true);assert(ready==1);h.domains->Closed();++groups;
  }
  { // Out-of-order pending raw ACKs require another uncontended disable ACK.
    Harness h;auto raw=h.domains->BeginRaw("","Debugger.enable",100),disable=h.domains->BeginRaw("","Debugger.disable",100);assert(raw&&disable);h.domains->EndRaw(disable,true);h.domains->EndRaw(raw,true);size_t failed=0;h.domains->Acquire("",1,Domain::Kind::Factory,100,[&](bool ok,const std::string&){assert(!ok);++failed;});assert(failed==1);
    disable=h.domains->BeginRaw("","Debugger.disable",100);assert(disable);h.domains->EndRaw(disable,true);size_t ready=0;h.Acquire(2,Domain::Kind::Coverage,ready);h.Ack("Debugger.enable");h.Skip(true);assert(ready==1);h.domains->Closed();++groups;
  }
  { // Raw logical timeout leaves physical receipt until actual channel closure.
    Harness h;auto raw=h.domains->BeginRaw("raw","Debugger.enable",10);assert(raw);h.now=11;h.domains->Tick(h.now);assert(h.domains->PhysicalCount()==1&&h.closes.empty());h.now=30011;h.domains->Tick(h.now);assert(h.domains->PhysicalCount()==1&&h.closes.size()==1&&!h.domains->NeedsTick());h.domains->Detached("raw");assert(h.domains->PhysicalCount()==0);h.domains->EndRaw(raw,true);assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // Raw and internal native commands share the same64 physical command cap.
    Harness h;std::vector<uint64_t> receipts;for(size_t i=0;i<64;++i){auto raw=h.domains->BeginRaw("raw"+std::to_string(i),"Debugger.enable",100);assert(raw);receipts.push_back(raw);}size_t ready=0;h.Acquire(1,Domain::Kind::Coverage,ready,"native");assert(h.commands.empty()&&h.domains->PhysicalCount()==64);assert(!h.domains->BeginRaw("overflow","Debugger.enable",100));h.domains->EndRaw(receipts.back(),false,false);receipts.pop_back();assert(h.commands.size()==1&&h.domains->PhysicalCount()==64);h.domains->Closed();assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // Real resumed notification may precede resume ACK and the next realm pause.
    Harness h;size_t ready=0,captured=0,resumed=0;Domain::Resume resume;
    h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);h.domains->Arm(1,Harness::Breakpoint(),[&](Dict,double deadline,Domain::Resume once){assert(deadline==(captured?60:20));++captured;resume=std::move(once);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());
    h.pause_deadline=20;h.domains->Event("Debugger.paused",Harness::Pause(),"");resume([&](bool ok,const std::string&){assert(ok);++resumed;});h.domains->Event("Debugger.resumed",CefDictionaryValue::Create(),"");h.pause_deadline=60;h.domains->Event("Debugger.paused",Harness::Pause(),"");assert(captured==1&&resumed==0&&h.domains->PhysicalCount()==2);h.Ack("Debugger.resume");assert(captured==2&&resumed==1&&h.commands.empty());resume([&](bool ok,const std::string&){assert(ok);++resumed;});h.Ack("Debugger.resume");assert(resumed==2);h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");++groups;
  }
  { // Unexpected genuine resume invalidates the capture ticket, not its accepted eval receipt.
    Harness h;size_t ready=0;Domain::Resume stale;h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);h.domains->Arm(1,Harness::Breakpoint(),[&](Dict,double,Domain::Resume once){stale=std::move(once);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());h.domains->Event("Debugger.paused",Harness::Pause(),"");auto hold=h.domains->Hold(1);assert(hold);h.domains->Event("Debugger.resumed",CefDictionaryValue::Create(),"");assert(h.expired==1&&h.domains->PhysicalCount()==1);stale([](bool ok,const std::string&){assert(!ok);});h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});assert(h.commands.empty());h.domains->Retire(hold);h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");++groups;
  }
  { // Oversized actual paused metadata is rejected before owned deep copy.
    Harness h;size_t ready=0,captured=0;h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);h.domains->Arm(1,Harness::Breakpoint(),[&](Dict,double,Domain::Resume){++captured;},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());
    auto actual=Harness::Pause();actual->SetString("largeDescription",std::string(131073,'x'));h.domains->Event("Debugger.paused",actual,"");assert(captured==0&&h.expired==1);auto stats=CefDictionaryValue::Create();h.domains->Diagnostics(stats);assert(stats->GetInt("nativeDebuggerPausePackets")==0);h.Ack("Debugger.resume");
    actual=Harness::Pause();auto frames=CefListValue::Create();for(size_t i=0;i<4097;++i)frames->SetNull(i);actual->SetList("callFrames",frames);h.domains->Event("Debugger.paused",actual,"");assert(captured==0&&h.expired==2);h.Ack("Debugger.resume");h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");++groups;
  }
  { // Deferred and current pause expiry retain their exact original packet.
    Harness h;size_t ready=0;h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume){},[](bool ok,const std::string&){assert(ok);});
    auto actual=Harness::Pause();actual->SetString("actualFrameMarker","first-admitted-document");h.pause_deadline=10;h.domains->Event("Debugger.paused",actual,"");auto stats=CefDictionaryValue::Create();h.domains->Diagnostics(stats);assert(stats->GetInt("nativeDebuggerPausePackets")==1);h.now=11;h.domains->Tick(h.now);assert(h.expired==1&&h.expired_markers==std::vector<std::string>{"first-admitted-document"});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());h.Ack("Debugger.resume");h.domains->Diagnostics(stats);assert(stats->GetInt("nativeDebuggerPausePackets")==0);h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");++groups;
  }
  { // A real session close from expiry can retire its own deferred packet reentrantly.
    Harness h;size_t ready=0;h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume){assert(false);},[](bool ok,const std::string&){assert(!ok);});
    h.pause_deadline=10;h.domains->Event("Debugger.paused",Harness::Pause(),"");auto accepted=h.Take("Debugger.setBreakpointByUrl");h.on_expired=[&](){h.domains->Detached("");};h.now=11;h.domains->Tick(h.now);assert(h.expired==1&&h.domains->PhysicalCount()==0);accepted.reply(true,Harness::BreakpointReply(),"");assert(h.commands.empty());auto stats=CefDictionaryValue::Create();h.domains->Diagnostics(stats);assert(stats->GetInt("nativeDebuggerPausePackets")==0);++groups;
  }
  { // Owned pause packets share a global cap; excess real pauses resume without copying.
    Harness h;size_t ready=0,captured=0;std::vector<Domain::Resume> resumes;
    for(uint64_t i=1;i<=65;++i){const auto session=std::to_string(i);h.Acquire(i,Domain::Kind::Factory,ready,session);h.Ack("Debugger.enable");h.Skip(false);h.domains->Arm(i,Harness::Breakpoint(),[&](Dict,double,Domain::Resume once){++captured;resumes.push_back(std::move(once));},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());h.domains->Event("Debugger.paused",Harness::Pause(),session);}
    auto stats=CefDictionaryValue::Create();h.domains->Diagnostics(stats);assert(captured==64&&h.expired==1&&stats->GetInt("nativeDebuggerPausePackets")==64);h.Ack("Debugger.resume");h.domains->Closed();h.domains->Diagnostics(stats);assert(h.domains->PhysicalCount()==0&&stats->GetInt("nativeDebuggerPausePackets")==0);for(auto& resume:resumes)resume([](bool ok,const std::string&){assert(!ok);});++groups;
  }
  { // An actual protocol error cannot prove raw domain state is unchanged.
    Harness h;auto raw=h.domains->BeginRaw("actual","Debugger.enable",100);assert(raw);h.domains->EndRaw(raw,false);size_t failures=0;h.domains->Acquire("actual",1,Domain::Kind::Coverage,100,[&](bool ok,const std::string&){assert(!ok);++failures;});assert(failures==1);auto disable=h.domains->BeginRaw("actual","Debugger.disable",100);assert(disable);h.domains->EndRaw(disable,true);size_t ready=0;h.Acquire(2,Domain::Kind::Coverage,ready,"actual");h.Ack("Debugger.enable");h.Skip(true);assert(ready==1);h.domains->Closed();++groups;
  }
  { // Without any factory, overlapping unowned pauses need no document-capture authorization.
    Harness h;size_t ready=0;h.Acquire(1,Domain::Kind::Coverage,ready);h.Ack("Debugger.enable");h.Skip(true);h.pause_deadline=-1;
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");h.domains->Event("Debugger.resumed",nullptr,"");h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(h.closes.empty());h.Ack("Debugger.resume");h.Ack("Debugger.resume");assert(h.expired==0&&h.domains->PhysicalCount()==0);auto stats=CefDictionaryValue::Create();h.domains->Diagnostics(stats);assert(stats->GetInt("nativeDebuggerUnownedPauses")==2&&stats->GetInt("nativeDebuggerDeferredUnownedPauses")==1);h.domains->Closed();++groups;
  }
  { // Explicit developer author pauses retain actual step/resume receipts.
    Harness h;size_t ready=0,pauses=0,expired=0,continued=0,released=0;Domain::Continue next;double budget=150;
    h.domains->AcquireDeveloper("",1,100,[&](Dict){return budget;},[&](uint64_t id,Dict packet,double deadline,Domain::Continue resume){assert(id&&packet&&deadline==budget);++pauses;next=std::move(resume);},[&](uint64_t,Dict,const std::string&){++expired;},[&](bool ok,const std::string&){assert(ok);++ready;});
    h.Ack("Debugger.enable");h.Skip(false);assert(ready==1);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(pauses==1&&h.commands.empty());
    next(Domain::Continuation::StepOver,90,[&](bool ok,const std::string&){assert(ok);++continued;});
    h.domains->Event("Debugger.resumed",nullptr,"");h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(pauses==1&&h.domains->PhysicalCount()==2);
    h.Ack("Debugger.stepOver");assert(continued==1&&pauses==2);
    next(Domain::Continuation::Resume,90,[&](bool ok,const std::string&){assert(ok);++continued;});h.Ack("Debugger.resume");
    h.domains->Release(1,200,[&](bool ok,const std::string&){assert(ok);++released;});h.Ack("Debugger.disable");assert(released==1&&expired==0&&h.domains->PhysicalCount()==0);
    auto stats=CefDictionaryValue::Create();h.domains->Diagnostics(stats);assert(stats->GetInt("nativeDebuggerUnownedPauses")==0);++groups;
  }
  { // Factory hits have exact priority while developer and coverage coexist.
    Harness h;size_t coverage=0,factory=0,developer=0,author=0,native=0;Domain::Continue next;Domain::Resume native_resume;
    h.Acquire(1,Domain::Kind::Coverage,coverage);h.Ack("Debugger.enable");h.Skip(true);
    h.Acquire(2,Domain::Kind::Factory,factory);h.Skip(false);
    h.domains->Arm(2,Harness::Breakpoint(),[&](Dict,double,Domain::Resume resume){++native;native_resume=std::move(resume);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());
    h.domains->AcquireDeveloper("",3,100,[](Dict){return 100;},[&](uint64_t,Dict,double,Domain::Continue resume){++author;next=std::move(resume);},[](uint64_t,Dict,const std::string&){assert(false);},[&](bool ok,const std::string&){assert(ok);++developer;});assert(developer==1);
    h.domains->Event("Debugger.paused",Harness::Pause(true),"");assert(native==1&&author==0);native_resume([](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(author==1);next(Domain::Continuation::Resume,100,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");
    h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.domains->Release(3,200,[](bool ok,const std::string&){assert(ok);});h.Cache(true);assert(h.commands.empty());
    h.domains->Release(2,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.removeBreakpoint");h.Ack("Debugger.disable");assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // A factory's immediate refusal budget cannot expire a developer author pause.
    Harness h;size_t factory=0,ready=0,pauses=0,expired=0;double developer_budget=150;Domain::Continue next;
    h.Acquire(1,Domain::Kind::Factory,factory);h.Ack("Debugger.enable");h.Skip(false);h.pause_deadline=1;
    h.domains->Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume){assert(false);},[](bool ok,const std::string&){assert(ok);});
    h.domains->AcquireDeveloper("",2,100,[&](Dict){return developer_budget;},[&](uint64_t,Dict,double deadline,Domain::Continue resume){assert(deadline==150);++pauses;next=std::move(resume);},[&](uint64_t,Dict,const std::string&){++expired;},[&](bool ok,const std::string&){assert(ok);++ready;});
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");developer_budget=250;h.now=2;h.domains->Tick(h.now);h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());h.Cache(false);assert(ready==1&&pauses==1&&expired==0);
    next(Domain::Continuation::Resume,100,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");h.domains->Closed();++groups;
  }
  { // Deferred developer budget is frozen at the original event, never renewed.
    Harness h;size_t factory=0,expired=0,pauses=0;double budget=50;
    h.Acquire(1,Domain::Kind::Factory,factory);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume){assert(false);},[](bool ok,const std::string&){assert(ok);});
    h.domains->AcquireDeveloper("",2,100,[&](Dict){return budget;},[&](uint64_t,Dict,double,Domain::Continue){++pauses;},[&](uint64_t,Dict,const std::string&){++expired;},[](bool ok,const std::string&){assert(ok);});
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");budget=500;h.now=51;h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());h.Cache(false);assert(pauses==0&&expired==1);assert(h.domains->PhysicalCount()==2);h.Ack("Debugger.resume");h.domains->Closed();++groups;
  }
  { // A real pause before initial enable ACK is held through actual policy readiness.
    Harness h;size_t ready=0,pauses=0;Domain::Continue next;
    h.domains->AcquireDeveloper("",1,100,[](Dict){return 150;},[&](uint64_t,Dict,double,Domain::Continue resume){++pauses;next=std::move(resume);},[](uint64_t,Dict,const std::string&){assert(false);},[&](bool ok,const std::string&){assert(ok);++ready;});
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(pauses==1&&ready==0);h.Ack("Debugger.enable");assert(ready==0);h.Skip(false);assert(ready==1);
    next(Domain::Continuation::Resume,100,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");h.domains->Closed();++groups;
  }
  { // The original continuation deadline is checked even when ACK precedes Tick.
    Harness h;Domain::Continue next;size_t rejected=0;
    h.domains->AcquireDeveloper("",1,100,[](Dict){return 200;},[&](uint64_t,Dict,double,Domain::Continue resume){next=std::move(resume);},[](uint64_t,Dict,const std::string&){},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");next(Domain::Continuation::StepInto,10,[&](bool ok,const std::string&){assert(!ok);++rejected;});h.now=11;h.Ack("Debugger.stepInto");assert(rejected==1&&h.domains->PhysicalCount()==0);h.domains->Closed();++groups;
  }
  { // Accepted developer source receipts survive Release, then actual resume ACK.
    Harness h;size_t released=0;
    h.domains->AcquireDeveloper("",1,100,[](Dict){return 0;},[](uint64_t,Dict,double,Domain::Continue){},[](uint64_t,Dict,const std::string&){assert(false);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");const auto receipt=h.domains->Hold(1);assert(receipt);
    h.domains->Release(1,200,[&](bool ok,const std::string&){assert(ok);++released;});assert(h.commands.empty()&&released==0);h.domains->Retire(receipt);assert(released==0);h.domains->Event("Debugger.resumed",nullptr,"");assert(h.domains->PhysicalCount()==2);h.Ack("Debugger.resume");assert(released==0);h.Ack("Debugger.disable");assert(released==1);++groups;
  }
  { // Actual lease/pause predicates and receipt diagnostics preserve owner kind.
    Harness h;uint64_t pause=0;Domain::Continue next;
    h.domains->AcquireDeveloper("",1,100,[](Dict){return 0;},[&](uint64_t id,Dict,double,Domain::Continue resume){pause=id;next=std::move(resume);},[](uint64_t,Dict,const std::string&){assert(false);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.enable");h.Skip(false);
    assert(h.domains->HasLease(1)&&!h.domains->IsPaused(1,1));h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(h.domains->IsPaused(1,pause));const auto receipt=h.domains->Hold(1);auto stats=CefDictionaryValue::Create();h.domains->Diagnostics(stats);assert(stats->GetInt("nativeDebuggerDeveloperReceipts")==1&&stats->GetInt("nativeDebuggerFactoryReceipts")==0);
    next(Domain::Continuation::Resume,100,[](bool ok,const std::string&){assert(ok);});assert(!h.domains->IsPaused(1,pause));h.Ack("Debugger.resume");h.domains->Retire(receipt);h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});assert(h.domains->HasLease(1));h.Ack("Debugger.disable");assert(!h.domains->HasLease(1));++groups;
  }
  { // Stopping coverage does not require resuming an independently owned pause.
    Harness h;size_t ready=0,released=0;uint64_t pause=0;Domain::Continue next;
    h.Acquire(1,Domain::Kind::Coverage,ready);h.Ack("Debugger.enable");h.Skip(true);
    h.domains->AcquireDeveloper("",2,100,[](Dict){return 0;},[&](uint64_t id,Dict,double,Domain::Continue resume){pause=id;next=std::move(resume);},[](uint64_t,Dict,const std::string&){assert(false);},[](bool ok,const std::string&){assert(ok);});h.Skip(false);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");
    h.domains->Release(1,200,[&](bool ok,const std::string&){assert(ok);++released;});
    assert(released==1&&!h.domains->HasLease(1)&&h.domains->IsPaused(2,pause)&&h.commands.empty());
    next(Domain::Continuation::Resume,100,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");
    h.domains->Release(2,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.disable");assert(h.domains->PhysicalCount()==0);++groups;
  }
  { // Starting coverage also shares actual policy while the developer stays paused.
    Harness h;uint64_t pause=0;size_t ready=0;Domain::Continue next;
    h.domains->AcquireDeveloper("",1,100,[](Dict){return 0;},[&](uint64_t id,Dict,double,Domain::Continue resume){pause=id;next=std::move(resume);},[](uint64_t,Dict,const std::string&){assert(false);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");
    h.Acquire(2,Domain::Kind::Coverage,ready);assert(ready==1&&h.domains->IsPaused(1,pause)&&h.commands.empty());
    h.domains->Release(2,200,[](bool ok,const std::string&){assert(ok);});assert(h.domains->IsPaused(1,pause)&&h.commands.empty());
    next(Domain::Continuation::Resume,100,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.disable");++groups;
  }
  { // Foreign pauses receive a real unowned resume, never a developer token.
    Harness h;size_t captured=0,rejected=0;Domain::Continue next;uint64_t owned=0;
    auto disposition=Domain::DeveloperPauseDisposition::Foreign;
    h.domains->AcquireScopedDeveloper("",1,100,[](Dict){return 100.;},[&](uint64_t id,Dict,double,Domain::Continue resume){++captured;owned=id;next=std::move(resume);},[&](uint64_t,Dict,const std::string&){++rejected;},[&](Dict){return disposition;},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(captured==0&&rejected==0&&h.domains->PhysicalCount()==2);auto resume=h.Take("Debugger.resume");assert(!h.domains->IsPaused(1,1));resume.reply(true,nullptr,"");assert(h.domains->PhysicalCount()==0);
    disposition=Domain::DeveloperPauseDisposition::Owned;h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(captured==1&&h.domains->IsPaused(1,owned));next(Domain::Continuation::Resume,100,[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");h.domains->Closed();++groups;
  }
  { // Unsupported attribution fails after the actual pause is retained for resume.
    Harness h;size_t rejected=0;h.domains->AcquireScopedDeveloper("",1,100,[](Dict){return 100.;},[](uint64_t,Dict,double,Domain::Continue){assert(false);},[&](uint64_t lease,Dict,const std::string&error){assert(lease==1&&error.find("frame ownership")!=std::string::npos);++rejected;h.domains->Release(1,200,[](bool ok,const std::string&){assert(ok);});},[](Dict){return Domain::DeveloperPauseDisposition::Unsupported;},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(rejected==1&&h.domains->HasLease(1));h.Ack("Debugger.resume");assert(h.domains->HasLease(1));h.Ack("Debugger.disable");assert(!h.domains->HasLease(1));++groups;
  }
  { // An actual factory breakpoint hit bypasses scoped eligibility entirely.
    Harness h;size_t ready=0,captured=0;Domain::Resume resume;
    h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[&](Dict,double,Domain::Resume next){++captured;resume=std::move(next);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());
    h.domains->AcquireScopedDeveloper("",2,100,[](Dict){assert(false);return 0.;},[](uint64_t,Dict,double,Domain::Continue){assert(false);},[](uint64_t,Dict,const std::string&){assert(false);},[](Dict){assert(false);return Domain::DeveloperPauseDisposition::Unsupported;},[](bool ok,const std::string&){assert(ok);});h.Cache(false);
    h.domains->Event("Debugger.paused",Harness::Pause(),"");assert(captured==1);resume([](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");h.domains->Closed();++groups;
  }
  { // A foreign deferred packet cannot upgrade into ownership after arm ACK.
    Harness h;size_t ready=0,captured=0;auto disposition=Domain::DeveloperPauseDisposition::Foreign;
    h.Acquire(1,Domain::Kind::Factory,ready);h.Ack("Debugger.enable");h.Skip(false);
    h.domains->AcquireScopedDeveloper("",2,100,[](Dict){return 100.;},[&](uint64_t,Dict,double,Domain::Continue){++captured;},[](uint64_t,Dict,const std::string&){assert(false);},[&](Dict){return disposition;},[](bool ok,const std::string&){assert(ok);});h.Cache(false);
    h.domains->Arm(1,Harness::Breakpoint(),[](Dict,double,Domain::Resume){assert(false);},[](bool ok,const std::string&){assert(ok);});h.domains->Event("Debugger.paused",Harness::Pause(false),"");disposition=Domain::DeveloperPauseDisposition::Owned;h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());assert(captured==0);h.Ack("Debugger.resume");h.domains->Closed();++groups;
  }
  { // Reentrant logical release during classification cannot disable before resume.
    Harness h;size_t released=0;h.domains->AcquireScopedDeveloper("",1,100,[](Dict){return 100.;},[](uint64_t,Dict,double,Domain::Continue){assert(false);},[](uint64_t,Dict,const std::string&){assert(false);},[&](Dict){h.domains->Release(1,200,[&](bool ok,const std::string&){assert(ok);++released;});assert(h.commands.empty());return Domain::DeveloperPauseDisposition::Owned;},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.enable");h.Skip(false);
    h.domains->Event("Debugger.paused",Harness::Pause(false),"");assert(released==0);h.Ack("Debugger.resume");h.Ack("Debugger.disable");assert(released==1);++groups;
  }
  { // Reentrant actual channel death cannot publish a stale developer pause.
    Harness h;h.domains->AcquireScopedDeveloper("child",1,100,[](Dict){return 100.;},[](uint64_t,Dict,double,Domain::Continue){assert(false);},[](uint64_t,Dict,const std::string&){assert(false);},[&](Dict){h.domains->Detached("child");return Domain::DeveloperPauseDisposition::Owned;},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.enable");h.Skip(false);h.domains->Event("Debugger.paused",Harness::Pause(false),"child");assert(h.commands.empty()&&!h.domains->HasLease(1)&&h.domains->PhysicalCount()==0);++groups;
  }
  { // Cache upgrades/downgrades are real ACK barriers even while factory-paused.
    Harness h;size_t factory=0,coverage=0,released=0;Domain::Resume resume;
    h.Acquire(1,Domain::Kind::Factory,factory);h.Cache(true);h.Skip(false);
    h.domains->Arm(1,Harness::Breakpoint(),[&](Dict,double,Domain::Resume once){resume=std::move(once);},[](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.setBreakpointByUrl",true,Harness::BreakpointReply());
    h.domains->Event("Debugger.paused",Harness::Pause(),"");const auto receipt=h.domains->Hold(1);assert(receipt&&resume);
    h.Acquire(2,Domain::Kind::Coverage,coverage);assert(coverage==0&&h.domains->PhysicalCount()==3);h.Cache(false);assert(coverage==1&&h.commands.empty());
    h.domains->Release(2,200,[&](bool ok,const std::string&){assert(ok);++released;});assert(!released);h.Cache(true);assert(released==1&&h.commands.empty()&&h.domains->PhysicalCount()==2);
    resume([](bool ok,const std::string&){assert(ok);});h.Ack("Debugger.resume");h.domains->Retire(receipt);h.domains->Closed();++groups;
  }
  { // Canceling an accepted cache upgrade retains both actual policy ACKs.
    Harness h;size_t factory=0,failed=0,released=0;h.Acquire(1,Domain::Kind::Factory,factory);h.Cache(true);h.Skip(false);
    h.domains->Acquire("",2,Domain::Kind::Coverage,100,[&](bool ok,const std::string&){assert(!ok);++failed;});
    h.domains->Release(2,200,[&](bool ok,const std::string&){assert(ok);++released;});assert(failed==1&&!released&&h.domains->PhysicalCount()==1);
    h.Cache(false);assert(!released&&h.domains->PhysicalCount()==1);h.Cache(true);assert(released==1&&h.commands.empty());h.domains->Closed();++groups;
  }
  { // A late real upgrade ACK cannot renew an expired observer acquisition.
    Harness h;size_t factory=0,failed=0;h.Acquire(1,Domain::Kind::Factory,factory);h.Cache(true);h.Skip(false);
    h.domains->Acquire("",2,Domain::Kind::Coverage,10,[&](bool ok,const std::string&){assert(!ok);++failed;});h.now=11;h.Cache(false);assert(failed==1&&h.domains->HasLease(2)&&h.domains->PhysicalCount()==1);h.Cache(true);assert(!h.domains->HasLease(2)&&h.domains->HasLease(1)&&h.commands.empty());h.domains->Closed();++groups;
  }
  { // Actual cache failure closes the channel; no success or synthetic retirement.
    Harness h;size_t factory=0,failed=0;h.Acquire(1,Domain::Kind::Factory,factory);h.Cache(true);h.Skip(false);
    h.domains->Acquire("",2,Domain::Kind::Coverage,100,[&](bool ok,const std::string&){assert(!ok);++failed;});h.Ack("Debugger.enable",false);
    assert(failed==1&&h.closes.size()==1&&h.commands.empty()&&h.domains->HasLease(1)&&h.domains->HasLease(2));h.domains->Detached("");assert(h.domains->PhysicalCount()==0&&!h.domains->HasLease(1));++groups;
  }
  { // Observer source/group receipts keep historical cache ownership through release.
    Harness h;size_t factory=0,developer=0,released=0;h.Acquire(1,Domain::Kind::Factory,factory);h.Cache(true);h.Skip(false);
    h.domains->AcquireDeveloper("",2,100,[](Dict){return 100.;},[](uint64_t,Dict,double,Domain::Continue){assert(false);},[](uint64_t,Dict,const std::string&){assert(false);},[&](bool ok,const std::string&){assert(ok);++developer;});assert(!developer);h.Cache(false);assert(developer==1);
    const auto receipt=h.domains->Hold(2);assert(receipt);h.domains->Release(2,200,[&](bool ok,const std::string&){assert(ok);++released;});assert(!released&&h.commands.empty());h.domains->Retire(receipt);assert(!released);h.Cache(true);assert(released==1&&h.commands.empty());h.domains->Closed();++groups;
  }
  { // Repeated explicit coverage sessions never leave factory cache unbounded.
    Harness h;size_t factory=0,coverage=0,released=0;h.Acquire(1,Domain::Kind::Factory,factory);h.Cache(true);h.Skip(false);
    for(uint64_t id=2;id<1002;++id){h.Acquire(id,Domain::Kind::Coverage,coverage);assert(coverage==id-2);h.Cache(false);assert(coverage==id-1);h.domains->Release(id,200,[&](bool ok,const std::string&){assert(ok);++released;});h.Cache(true);assert(released==id-1&&h.commands.empty());}
    h.domains->Closed();++groups;
  }
  std::cout<<"debugger domain component groups "<<groups<<" passed\n";
}
