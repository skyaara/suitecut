// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "developer_debugger_controls.h"
#include "debugger_pause_projection.h"
#include "developer_debugger_frame_pause.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <utility>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
using Controls = suitecut::DeveloperDebuggerControls;
using Domains = suitecut::DebuggerDomains;
using Dict = Controls::Dict;
static Dict JSON(const std::string& text) {
  const auto value = CefParseJSON(text, JSON_PARSER_RFC); assert(value);
  const auto result = value->GetDictionary(); assert(result); return result;
}
static int completed_groups=0;
struct Fixture {
  struct Call { std::string method; Dict params; Domains::Reply reply; double deadline; };
  struct Result { bool ok; std::string json, error; };
  double now = 1;
  uint64_t lease = 0;
  Controls::Document document{1, 7, "actual-root-unique"};
  std::vector<Call> calls;
  struct Results:std::map<int,Result>{
    Result& at(int owner){const auto found=find(owner);if(found==end()){std::cerr<<"Missing native debugger result owner="<<owner<<" completedGroups="<<completed_groups<<std::endl;std::abort();}return found->second;}
  } results;
  std::vector<std::string> closes;
  std::shared_ptr<Domains> domains;
  std::unique_ptr<Controls> controls;
  bool refuse = false;
  uint64_t script_revision=1;
  bool pin_ready=true,scope_current=true,frame_target=false;
  std::string inspector_session;
  suitecut::FrameDebuggerScopes::Host scope_host{"actual-source-owner",1};
  std::shared_ptr<suitecut::FrameDebuggerScopes>scopes;
  suitecut::FrameDebuggerScopes::Pin ScopePin()const{return {{scope_host,inspector_session},1,41,frame_target?"actual-child":"actual-root","actual-loader",document.unique_context,document.context,!frame_target};}
  explicit Fixture(bool scoped=false,std::string session="",bool frame=false):frame_target(frame),inspector_session(std::move(session)) {
    Domains::Hooks domain;
    domain.now = [&] { return now; };
    domain.send = [&](const std::string& session, const std::string& method, Dict params, double deadline, Domains::Reply reply) {
      assert(session==inspector_session); calls.push_back({method, params ? params->Copy(false) : nullptr, std::move(reply), deadline}); return true;
    };
    domain.close_channel = [&](const std::string&, const std::string& error) { closes.push_back(error); };
    domains = std::make_shared<Domains>(std::move(domain));
    Controls::Hooks hooks;
    hooks.now = [&] { return now; }; hooks.domains = domains;
    hooks.owner_namespace = "actual-source-owner";hooks.inspector_session=inspector_session;hooks.agent_controls=!frame_target;
    hooks.lease = [&] { return ++lease; };
    hooks.current = [&] { return document; };
    if(scoped){
      suitecut::FrameDebuggerScopes::Hooks scope;
      scope.now=[&]{return now;};scope.current=[&](const auto&pin){return pin_ready&&scope_current&&pin==ScopePin();};
      scope.lease_present=[&](uint64_t id){return domains->HasLease(id);};
      scope.cleanup=[&](auto,const auto&){if(controls)controls->Changed();};
      scope.close_channel=[&](const auto&,const auto&error){closes.push_back(error);};scope.wake=[]{};
      scopes=suitecut::FrameDebuggerScopes::Create(scope_host,std::move(scope),std::make_shared<suitecut::FrameDebuggerScopes::Budget>());
      hooks.scopes=scopes;hooks.scope_pin=[&](const auto&owner,auto&pin){if(!pin_ready||!scope_current||owner!=document)return false;pin=ScopePin();return true;};
    }
    if(frame_target)hooks.pause_eligibility=[&](auto token,Dict packet){
      return suitecut::FrameDebuggerPause::Classify(scopes,token,packet,[&](const std::string&session,const std::string&script)->std::optional<suitecut::FrameDebuggerPause::Script>{
        assert(session==inspector_session);
        if(script=="actual-script")return suitecut::FrameDebuggerPause::Script{document.context,false};
        if(script=="foreign-script")return suitecut::FrameDebuggerPause::Script{document.context+1,false};
        if(script=="attested-native")return suitecut::FrameDebuggerPause::Script{document.context,true};
        return std::nullopt;
      });
    };
    hooks.owns_pause = [&](Dict packet, const Controls::Document& owner) {
      return owner == document && packet->GetList("callFrames")->GetDictionary(0)->GetDictionary("location")->GetString("scriptId") == "actual-script";
    };
    hooks.owns_script = [&](const std::string& script, const Controls::Document& owner) { return script == "actual-script" && owner == document; };
    hooks.owns_breakpoint_script = hooks.owns_script;
    hooks.script_page=[&](const Controls::Document&owner,const std::string&after,const uint64_t*revision,double,suitecut::DeveloperDebuggerScripts::Page&page,std::string&error){
      assert(owner==document);if(revision&&*revision!=script_revision){error="inventory changed; restart enumeration";return false;}
      page.revision=script_revision;page.json=R"([{"scriptId":"actual-script","url":"http://actual/author.js","attestedNative":false}])";
      page.count=1;page.last=after.empty()?"actual-script":"z-final";page.more=after.empty();return true;
    };
    hooks.send = [&](const std::string& method, Dict params, double deadline, Domains::Reply reply) {
      if (refuse) return false;
      calls.push_back({method, params->Copy(false), std::move(reply), deadline}); return true;
    };
    hooks.close_channel = [&](const std::string& error) { closes.push_back(error); };
    controls = std::make_unique<Controls>(std::move(hooks));
  }
  ~Fixture() { domains->Closed(); controls->Closed();if(scopes)scopes->HostClosed(scope_host);controls.reset(); assert(calls.empty() || !domains->HasLease(lease)); }
  bool Has(const std::string& method) const {
    return std::any_of(calls.begin(), calls.end(), [&](const auto& call) { return call.method == method; });
  }
  void Ack(const std::string& method, bool ok = true, Dict data = nullptr) {
    auto it = std::find_if(calls.begin(), calls.end(), [&](const auto& call) { return call.method == method; }); assert(it != calls.end());
    auto reply = std::move(it->reply); calls.erase(it); reply(ok, data ? data : CefDictionaryValue::Create(), ok ? "" : "actual command error");
  }
  Controls::Reply Reply(int id) {
    return [&, id](bool ok, const std::string& json, const std::string& error) {
      assert(!results.contains(id)); results.emplace(id, Result{ok, json, error});
    };
  }
  void Open() {
    controls->Handle(1, JSON("{\"action\":\"open\"}"), 100, Reply(1));
    assert(!results.contains(1)); Ack("Debugger.enable"); Ack("Debugger.setSkipAllPauses");
    assert(results.at(1).ok);
  }
  std::string Session() { return JSON(results.at(1).json)->GetString("session").ToString(); }
  void Run(int owner, const std::string& action, double deadline = 100, const std::string& extra = "") {
    controls->Handle(owner, JSON("{\"action\":\"" + action + "\",\"session\":\"" + Session() + "\"" + extra + "}"), deadline, Reply(owner));
  }
  Dict Packet(const std::string& script = "actual-script") {
    return JSON("{\"reason\":\"other\",\"callFrames\":[{\"callFrameId\":\"opaque-frame\",\"functionName\":\"authorFn\",\"url\":\"http://actual/author.js\",\"location\":{\"scriptId\":\"" + script + "\",\"lineNumber\":3,\"columnNumber\":2},\"scopeChain\":[{\"type\":\"local\",\"name\":\"authorFn\",\"object\":{\"type\":\"object\",\"objectId\":\"opaque-scope\",\"value\":{\"__suitecutNativeUTF16Scalar\":\"d800\"}}}]}],\"hitBreakpoints\":[]}");
  }
  void Pause() { assert(domains->Event("Debugger.paused", Packet(), inspector_session)); }
  std::string PauseToken(int owner) { return JSON(results.at(owner).json)->GetString("pause").ToString(); }
  void Resume(int owner, const std::string& token, const std::string& action = "resume", double deadline = 100) {
    Run(owner, action, deadline, ",\"pause\":\"" + token + "\"");
  }
  void Close(int owner = 30) {
    Run(owner, "close", 1000);
    if (Has("Debugger.resume")) Ack("Debugger.resume");
    if (Has("Debugger.disable")) Ack("Debugger.disable");
    assert(results.at(owner).ok);
  }
  void Eval(int owner,const std::string&pause,const std::string&frame="opaque-frame",double deadline=100){
    auto params=CefDictionaryValue::Create();params->SetString("action","evaluateOnCallFrame");params->SetString("session",Session());
    params->SetString("pause",pause);params->SetString("callFrameId",frame);params->SetString("expressionJSON",R"("actualLocal+1")");
    controls->Handle(owner,params,deadline,Reply(owner));
  }
  void Tick(double value) { now = value; controls->Tick(now); domains->Tick(now); }
};

int main() {
  assert(cef_api_hash(CEF_API_VERSION, 0));
  int&groups=completed_groups;
  {
    Fixture f; f.Open(); f.Run(2, "waitForPause"); f.Pause();
    const auto output = JSON(f.results.at(2).json);
    const auto frame = output->GetList("callFrames")->GetDictionary(0);
    assert(frame->GetString("callFrameId") == "opaque-frame");
    assert(frame->GetDictionary("location")->GetInt("lineNumber") == 3);
    assert(!frame->GetList("scopeChain")->GetDictionary(0)->GetDictionary("object")->HasKey("value"));
    f.Resume(3, f.PauseToken(2)); assert(!f.results.contains(3));
    f.domains->Event("Debugger.resumed", CefDictionaryValue::Create(), "");
    assert(!f.results.contains(3)); f.Ack("Debugger.resume"); assert(f.results.at(3).ok);
    f.Close(); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "pause");
    assert(f.controls->PhysicalCount() == 2);
    f.Ack("Debugger.pause"); assert(!f.results.contains(2) && f.controls->PhysicalCount() == 1);
    f.Pause(); assert(f.results.at(2).ok && f.controls->PhysicalCount() == 0);
    f.Close(); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "pause"); f.Pause();
    assert(!f.results.contains(2) && f.controls->PhysicalCount() == 1);
    f.Ack("Debugger.pause"); assert(f.results.at(2).ok);
    f.Close(); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "waitForPause"); f.Pause();
    const auto old = f.PauseToken(2); f.Resume(3, old, "stepOver", 90);
    f.domains->Event("Debugger.resumed", CefDictionaryValue::Create(), "");
    f.Pause(); assert(!f.results.contains(3)); f.Ack("Debugger.stepOver");
    assert(f.results.at(3).ok && f.PauseToken(3) != old);
    f.Resume(4, old); assert(!f.results.at(4).ok && !f.Has("Debugger.resume"));
    f.Close(); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "pause"); f.Ack("Debugger.pause");
    f.controls->Cancel(2); assert(!f.results.contains(2));
    assert(f.controls->PhysicalCount() == 1 && !f.Has("Debugger.disable") && !f.Has("Debugger.resume"));
    f.Run(3, "close", 0); f.Pause();
    assert(f.Has("Debugger.resume") && !f.results.contains(3));
    f.Ack("Debugger.resume"); f.Ack("Debugger.disable");
    assert(f.results.at(3).ok && f.controls->PhysicalCount() == 0); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "pause", 5); f.Ack("Debugger.pause"); f.Tick(5);
    assert(!f.results.at(2).ok && f.controls->PhysicalCount() == 1);
    f.Tick(30006); assert(f.closes.size() == 1 && f.controls->PhysicalCount() == 1);
    assert(!f.controls->NeedsTick());
    f.domains->Closed(); f.controls->Closed(); assert(f.controls->PhysicalCount() == 0); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "scriptSource", 50, ",\"scriptId\":\"actual-script\"");
    assert(f.controls->PhysicalCount() == 1);
    f.Run(3, "close", 0); assert(!f.results.at(2).ok && !f.Has("Debugger.disable"));
    f.Ack("Debugger.getScriptSource", true, JSON("{\"scriptSource\":\"actual source\"}"));
    assert(f.Has("Debugger.disable") && !f.results.contains(3));
    f.Ack("Debugger.disable"); assert(f.results.at(3).ok); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "scriptSource", 5, ",\"scriptId\":\"actual-script\"");
    f.now = 6; f.Ack("Debugger.getScriptSource", true, JSON("{\"scriptSource\":\"actual source\"}"));
    assert(!f.results.at(2).ok && f.results.at(2).error.find("deadline") != std::string::npos);
    f.Close(); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "scriptSource", 100, ",\"scriptId\":\"unobserved\"");
    assert(!f.results.at(2).ok && !f.Has("Debugger.getScriptSource"));
    f.Run(3, "waitForPause"); f.Pause();
    ++f.document.epoch; f.controls->Changed();
    assert(f.Has("Debugger.resume"));
    f.Ack("Debugger.resume"); f.Ack("Debugger.disable");
    f.Run(4, "resume", 100, ",\"pause\":\"old\""); assert(!f.results.at(4).ok); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "pause");
    f.Run(3, "pause"); assert(!f.results.at(3).ok);
    f.Run(4, "waitForPause"); f.controls->Cancel(4);
    f.Pause(); f.Ack("Debugger.pause"); assert(f.results.at(2).ok && !f.results.contains(4));
    f.Close(); ++groups;
  }
  {
    Fixture f; f.Open(); f.refuse = true; f.Run(2, "pause");
    assert(!f.results.at(2).ok && f.controls->PhysicalCount() == 0);
    f.Ack("Debugger.disable"); assert(!f.domains->HasLease(f.lease)); ++groups;
  }
  {
    Fixture f; f.Open();
    for (int i = 2; i < 34; ++i) f.Run(i, "waitForPause", 0);
    f.Run(34, "waitForPause", 0); assert(!f.results.at(34).ok);
    assert(!f.controls->NeedsTick());
    f.Pause(); for (int i = 2; i < 34; ++i) assert(f.results.at(i).ok);
    f.Close(35); ++groups;
  }
  {
    Fixture f; f.Open(); f.Run(2, "waitForPause", 100);
    f.domains->Event("Debugger.paused", f.Packet("actual-child-script"), "");
    assert(!f.results.at(2).ok && f.Has("Debugger.resume"));
    f.Ack("Debugger.resume"); f.Ack("Debugger.disable"); ++groups;
  }
  {
    Fixture f; auto packet = f.Packet();
    const auto frame = packet->GetList("callFrames")->GetDictionary(0);
    suitecut::DevToolsSetString(frame, "functionName", suitecut::DevToolsUnitsWTF8(std::u16string{0xd800}));
    std::string out; assert(suitecut::DebuggerPauseProjection::Build(packet, "session", "pause", out));
    assert(out.find("\\ud800") != std::string::npos && out.find("__suitecutNativeUTF16Scalar") == std::string::npos);
    frame->GetDictionary("location")->SetInt("lineNumber", -1);
    assert(!suitecut::DebuggerPauseProjection::Build(packet, "session", "pause", out)); ++groups;
  }
  { // Cancellation while the domain enable ACK is still physically retained.
    Fixture f;
    f.controls->Handle(1, JSON("{\"action\":\"open\"}"), 100, f.Reply(1));
    f.controls->Cancel(1); assert(!f.results.contains(1));
    f.Ack("Debugger.enable"); assert(f.Has("Debugger.disable"));
    assert(f.domains->HasLease(f.lease)); f.Ack("Debugger.disable");
    assert(!f.domains->HasLease(f.lease)); ++groups;
  }
  { // Late pause packet before Tick cannot renew the expired public budget.
    Fixture f; f.Open(); f.Run(2, "pause", 5); f.Ack("Debugger.pause");
    f.now = 6; f.Pause(); assert(!f.results.at(2).ok);
    f.controls->Tick(f.now); assert(f.controls->PhysicalCount() == 0);
    assert(f.Has("Debugger.resume")); f.Ack("Debugger.resume"); f.Ack("Debugger.disable"); ++groups;
  }
  { // A failed real disable ACK is not a physical lease-retirement receipt.
    Fixture f; f.Open(); f.Run(2, "close", 0); f.Ack("Debugger.disable", false);
    assert(f.domains->HasLease(f.lease) && !f.results.contains(2) && !f.closes.empty());
    f.domains->Closed(); f.controls->Closed();
    assert(f.results.contains(2) && !f.domains->HasLease(f.lease)); ++groups;
  }
  { // Real source ACK is retained through cancel; callback cannot complete a reused owner.
    Fixture f; f.Open(); f.Run(2, "scriptSource", 0, ",\"scriptId\":\"actual-script\"");
    f.controls->Cancel(2); f.Run(2, "waitForPause", 0);
    f.Ack("Debugger.getScriptSource", true, JSON("{\"scriptSource\":\"actual canceled source\"}"));
    assert(!f.results.contains(2) && f.controls->PhysicalCount() == 0);
    f.Pause(); assert(f.results.at(2).ok); f.Close(); ++groups;
  }
  { // Cleanup admission remains available with all32 logical observer slots occupied.
    Fixture f; f.Open();
    for (int i = 2; i < 34; ++i) f.Run(i, "waitForPause", 0);
    f.Run(35,"waitForPause",0);assert(!f.results.at(35).ok&&f.results.at(35).error.find("deadline")==std::string::npos);
    f.Run(34, "close", 0); assert(f.Has("Debugger.disable"));
    for (int i = 2; i < 34; ++i) assert(!f.results.at(i).ok);
    f.Ack("Debugger.disable"); assert(f.results.at(34).ok); ++groups;
  }
  { // Process slots include genuine pending acquisitions, before any enable ACK.
    std::vector<std::unique_ptr<Fixture>> pool;
    for (int i = 0; i < 17; ++i) {
      auto f = std::make_unique<Fixture>();
      f->controls->Handle(1, JSON("{\"action\":\"open\"}"), 0, f->Reply(1));
      if (i < 16) assert(f->Has("Debugger.enable") && !f->results.contains(1));
      else assert(!f->Has("Debugger.enable") && !f->results.at(1).ok);
      pool.push_back(std::move(f));
    }
    pool.clear(); ++groups;
  }
  { // Expiring an old call cannot close a reentrantly opened replacement owner.
    Fixture f; f.Open(); f.Run(2, "waitForPause"); f.Pause();
    const auto token = f.PauseToken(2);
    f.controls->Handle(3, JSON("{\"action\":\"resume\",\"session\":\"" + f.Session() + "\",\"pause\":\"" + token + "\"}"), 5,
      [&](bool ok, const std::string&, const std::string&) {
        assert(!ok);
        f.Ack("Debugger.resume"); f.Ack("Debugger.disable");
        f.controls->Handle(8, JSON("{\"action\":\"open\"}"), 100, f.Reply(8));
        f.Ack("Debugger.enable"); f.Ack("Debugger.setSkipAllPauses");
        assert(f.results.at(8).ok);
      });
    f.Tick(5); assert(f.domains->HasLease(f.lease) && !f.Has("Debugger.disable"));
    const auto session = JSON(f.results.at(8).json)->GetString("session").ToString();
    f.controls->Handle(9, JSON("{\"action\":\"close\",\"session\":\"" + session + "\"}"), 100, f.Reply(9));
    f.Ack("Debugger.disable"); assert(f.results.at(9).ok); ++groups;
  }
  { // A close cannot release the real domain while installed IDs await removal.
    Fixture f;f.Open();f.Run(2,"setBreakpoint",100,",\"location\":{\"scriptId\":\"actual-script\",\"lineNumber\":3}");
    f.Ack("Debugger.setBreakpoint",true,JSON(R"({"breakpointId":"actual-id","actualLocation":{"scriptId":"actual-script","lineNumber":3,"columnNumber":0}})"));
    assert(f.results.at(2).ok);f.Run(3,"close",1000);
    assert(f.Has("Debugger.removeBreakpoint")&&!f.Has("Debugger.disable")&&!f.results.contains(3));
    f.Ack("Debugger.removeBreakpoint");assert(f.Has("Debugger.disable")&&!f.results.contains(3));
    f.Ack("Debugger.disable");assert(f.results.at(3).ok);++groups;
  }
  { // Late successful installation must really be removed before lease release.
    Fixture f;f.Open();f.Run(2,"setBreakpoint",100,",\"location\":{\"scriptId\":\"actual-script\",\"lineNumber\":3}");
    f.Run(3,"close",1000);assert(!f.results.at(2).ok&&!f.results.contains(3)&&!f.Has("Debugger.disable"));
    f.Ack("Debugger.setBreakpoint",true,JSON(R"({"breakpointId":"actual-late-id","actualLocation":{"scriptId":"actual-script","lineNumber":3,"columnNumber":0}})"));
    assert(f.Has("Debugger.removeBreakpoint")&&!f.Has("Debugger.disable"));f.Ack("Debugger.removeBreakpoint");f.Ack("Debugger.disable");assert(f.results.at(3).ok);++groups;
  }
  { // A malformed successful reply cannot release a possibly installed ID.
    Fixture f;f.Open();f.Run(2,"setBreakpoint",100,",\"location\":{\"scriptId\":\"actual-script\",\"lineNumber\":3}");
    f.Ack("Debugger.setBreakpoint",true,JSON("{}"));assert(!f.results.at(2).ok&&f.closes.size()==1&&!f.Has("Debugger.disable"));
    auto diagnostic=CefDictionaryValue::Create();f.controls->Diagnostics(diagnostic);assert(diagnostic->GetInt("nativeDeveloperDebuggerBreakpoints")==1);++groups;
  }
  { // Factory-preserved domain release can finish synchronously; old readers
    // must be failed before a close reply reentrantly opens a replacement.
    Fixture f;f.Open();const auto factory=++f.lease;bool factory_ready=false;
    f.domains->Acquire("",factory,Domains::Kind::Factory,100,[&](bool ok,const std::string&){assert(ok);factory_ready=true;});assert(factory_ready);
    f.Run(2,"waitForPause",0);
    f.controls->Handle(3,JSON("{\"action\":\"close\",\"session\":\""+f.Session()+"\"}"),100,[&](bool ok,const std::string&,const std::string&){
      assert(ok&&!f.results.at(2).ok);
      f.controls->Handle(8,JSON(R"({"action":"open"})"),100,
        [&](bool opened,const std::string& json,const std::string& error){
          f.Reply(8)(opened,json,error);assert(opened);
          const auto token=JSON(json)->GetString("session").ToString();
          f.controls->Handle(9,JSON("{\"action\":\"waitForPause\",\"session\":\""+token+"\"}"),0,f.Reply(9));
        });
    });
    assert(!f.results.contains(9));const auto token=JSON(f.results.at(8).json)->GetString("session").ToString();
    f.controls->Handle(10,JSON("{\"action\":\"close\",\"session\":\""+token+"\"}"),100,f.Reply(10));assert(f.results.at(10).ok&&!f.results.at(9).ok);
    f.domains->Release(factory,100,[](bool ok,const std::string&){assert(ok);});f.Ack("Debugger.disable");++groups;
  }
  { // Evaluate uses an actual retained pause/frame, including opaque membership.
    Fixture f;f.Open();f.Run(2,"waitForPause");f.Pause();const auto pause=f.PauseToken(2);
    f.Eval(3,pause,"forged-frame");assert(!f.results.at(3).ok&&!f.Has("Debugger.evaluateOnCallFrame"));
    f.Eval(4,pause);f.Ack("Debugger.evaluateOnCallFrame",true,JSON(R"({"result":{"type":"number","value":42}})"));
    assert(!f.results.contains(4)&&f.controls->PhysicalCount()==1&&f.Has("Runtime.releaseObjectGroup"));
    f.Ack("Runtime.releaseObjectGroup");assert(f.results.at(4).ok);f.Run(5,"waitForPause");assert(f.results.at(5).ok&&f.PauseToken(5)==pause);f.Close();++groups;
  }
  { // Cancel cannot release/resume the developer lease before real eval cleanup.
    Fixture f;f.Open();f.Run(2,"waitForPause");f.Pause();const auto pause=f.PauseToken(2);
    f.Eval(3,pause);f.controls->Cancel(3);assert(!f.Has("Debugger.resume")&&f.controls->PhysicalCount()==1);
    f.Run(4,"close",1000);f.Ack("Debugger.evaluateOnCallFrame",true,JSON(R"({"result":{"type":"number","value":1}})"));
    assert(!f.Has("Debugger.resume")&&!f.results.contains(4));
    f.Ack("Runtime.releaseObjectGroup");assert(f.Has("Debugger.resume"));f.Ack("Debugger.resume");f.Ack("Debugger.disable");
    assert(f.results.at(4).ok&&!f.results.contains(3));++groups;
  }
  { // Resuming concurrently cannot publish an old paused-frame result late.
    Fixture f;f.Open();f.Run(2,"waitForPause");f.Pause();const auto pause=f.PauseToken(2);
    f.Eval(3,pause);f.Resume(4,pause);f.Ack("Debugger.resume");
    f.Ack("Debugger.evaluateOnCallFrame",true,JSON(R"({"result":{"type":"number","value":1}})"));f.Ack("Runtime.releaseObjectGroup");
    assert(!f.results.at(3).ok&&f.results.at(4).ok);f.Close();++groups;
  }
  { // Exact actual renderer death is separate from a logical document change.
    Fixture f;f.Open();f.Run(2,"waitForPause");f.Pause();const auto pause=f.PauseToken(2);
    f.Eval(3,pause);f.controls->RealmDestroyed(999);assert(!f.results.contains(3)&&f.controls->PhysicalCount()==1);
    f.controls->RealmDestroyed(f.document.epoch);assert(!f.results.at(3).ok&&f.controls->PhysicalCount()==1&&!f.Has("Debugger.disable"));
    f.Ack("Debugger.evaluateOnCallFrame",false);assert(!f.Has("Runtime.releaseObjectGroup")&&f.controls->PhysicalCount()==0&&f.closes.empty());
    if(f.Has("Debugger.resume"))f.Ack("Debugger.resume");
    if(f.Has("Debugger.disable"))f.Ack("Debugger.disable");
    ++groups;
  }
  {
    Fixture f; f.Open();f.Run(2,"scripts");assert(f.results.at(2).ok&&f.calls.empty()&&f.controls->PhysicalCount()==0);
    const auto first=JSON(f.results.at(2).json);const auto cursor=first->GetString("next").ToString();
    f.Run(3,"scripts",100,",\"cursor\":\""+cursor+"\"");assert(f.results.at(3).ok&&!JSON(f.results.at(3).json)->HasKey("next"));
    ++f.script_revision;f.Run(4,"scripts",100,",\"cursor\":\""+cursor+"\"");assert(!f.results.at(4).ok&&f.results.at(4).error.find("restart")!=std::string::npos);
    auto diagnostics=CefDictionaryValue::Create();f.controls->Diagnostics(diagnostics);assert(diagnostics->GetInt("nativeDeveloperDebuggerScriptCursors")==0);
    f.Run(5,"scripts");assert(f.results.at(5).ok);f.Close();f.controls->Diagnostics(diagnostics);assert(diagnostics->GetInt("nativeDeveloperDebuggerScriptCursors")==0);++groups;
  }
  { // Real default metadata may follow public open admission. No fake lease is
    // allocated while the bounded original-budget startup waiter is pending.
    Fixture f(true);f.pin_ready=false;
    f.controls->Handle(1,JSON(R"({"action":"open"})"),100,f.Reply(1));
    assert(f.calls.empty()&&!f.lease&&f.scopes->Retained()==0&&f.controls->NeedsTick());
    f.controls->Handle(2,JSON(R"({"action":"open"})"),100,f.Reply(2));assert(!f.results.at(2).ok&&!f.lease);
    f.pin_ready=true;f.Tick(10);assert(f.lease==1&&f.scopes->Retained()==1&&!f.results.contains(1));
    f.Ack("Debugger.enable");f.Ack("Debugger.setSkipAllPauses");assert(f.results.at(1).ok);f.Close();assert(f.scopes->Retained()==0);++groups;
  }
  { // Metadata cannot renew the original startup deadline after it expires.
    Fixture f(true);f.pin_ready=false;f.controls->Handle(1,JSON(R"({"action":"open"})"),5,f.Reply(1));
    f.Tick(5);assert(!f.results.at(1).ok&&!f.lease&&f.calls.empty()&&!f.controls->NeedsTick());
    f.pin_ready=true;f.Tick(6);assert(!f.lease&&f.calls.empty());++groups;
  }
  { // Cancel removes only the startup observer; zero remains disabled, not a
    // renewed finite deadline or an invented native scope.
    Fixture f(true);f.pin_ready=false;f.controls->Handle(1,JSON(R"({"action":"open"})"),0,f.Reply(1));
    f.controls->Cancel(1);assert(!f.lease&&f.calls.empty()&&!f.controls->NeedsTick());
    f.pin_ready=true;f.Open();f.Close();++groups;
  }
  { // Late real enable/disable ACKs keep the channel fenced after canceled open.
    Fixture f(true);f.controls->Handle(1,JSON(R"({"action":"open"})"),100,f.Reply(1));
    f.controls->Cancel(1);std::string error;auto child=f.ScopePin();child.frame="child";child.root=false;
    assert(f.scopes->Retained()==1&&!f.scopes->Reserve(child,2,100,error));
    f.Ack("Debugger.enable");assert(f.Has("Debugger.disable")&&f.scopes->Retained()==1);
    f.Ack("Debugger.disable");assert(!f.scopes->Retained()&&!f.results.contains(1));++groups;
  }
  { // Actual domain loss is distinct from the still-registered Source callback.
    Fixture f(true);f.Open();f.Run(2,"scriptSource",100,",\"scriptId\":\"actual-script\"");
    assert(f.scopes->Physical()==1);f.Run(3,"close",0);f.domains->Detached("");
    assert(!f.domains->HasLease(f.lease)&&f.scopes->Physical()==1&&f.scopes->Retained()==1&&!f.results.contains(3));
    f.Ack("Debugger.getScriptSource",false);assert(f.results.at(3).ok&&!f.scopes->Physical()&&!f.scopes->Retained());++groups;
  }
  { // A genuine pause can precede actual enable/skip acknowledgement without
    // publishing a ready session or borrowing another document's frame.
    Fixture f(true);f.controls->Handle(1,JSON(R"({"action":"open"})"),100,f.Reply(1));
    f.Pause();assert(!f.results.contains(1));f.Ack("Debugger.enable");f.Ack("Debugger.setSkipAllPauses");
    assert(f.results.at(1).ok);f.Run(2,"waitForPause");assert(f.results.at(2).ok);f.Close();++groups;
  }
  { // Late breakpoint publication admits its cleanup before dropping the
    // preceding physical receipt, leaving no transient channel gap.
    Fixture f(true);f.Open();f.Run(2,"setBreakpoint",100,",\"location\":{\"scriptId\":\"actual-script\",\"lineNumber\":3}");
    assert(f.scopes->Physical()==1);f.Run(3,"close",0);
    f.Ack("Debugger.setBreakpoint",true,JSON(R"({"breakpointId":"actual-owned-breakpoint","actualLocation":{"scriptId":"actual-script","lineNumber":3,"columnNumber":0}})"));
    assert(f.scopes->Physical()==1&&f.scopes->Retained()==1&&f.Has("Debugger.removeBreakpoint")&&!f.results.contains(3));
    f.Ack("Debugger.removeBreakpoint");assert(f.scopes->Physical()==0&&f.scopes->Retained()==1&&f.Has("Debugger.disable"));
    f.Ack("Debugger.disable");assert(f.results.at(3).ok&&!f.scopes->Retained());++groups;
  }
  { // A different exact native scope already fencing the root channel refuses
    // legacy root admission before any Inspector command is emitted.
    Fixture f(true);std::string error;const auto existing=f.scopes->Reserve(f.ScopePin(),++f.lease,100,error);assert(existing);
    f.controls->Handle(1,JSON(R"({"action":"open"})"),100,f.Reply(1));assert(!f.results.at(1).ok&&f.calls.empty()&&f.scopes->Retained()==1);
    f.scopes->Close(existing);assert(!f.scopes->Retained());++groups;
  }
  { // Child sessions retain their actual channel; agent-wide mutators cannot
    // consume a current pause or emit a protocol command.
    Fixture f(true,"actual-oopif-session",true);f.Open();assert(f.controls->Retained()&&f.controls->OwnsSession(f.Session()));
    f.Run(2,"pause");assert(!f.results.at(2).ok&&f.calls.empty());
    f.Run(3,"waitForPause");f.Pause();const auto pause=f.PauseToken(3);
    for(const auto&action:{"stepOver","stepInto","stepOut"}){const int id=10+int(f.results.size());f.Resume(id,pause,action);assert(!f.results.at(id).ok&&f.calls.empty());}
    f.Run(4,"waitForPause");assert(f.results.at(4).ok&&f.PauseToken(4)==pause);
    f.Resume(5,pause);assert(!f.results.contains(5));f.Ack("Debugger.resume");assert(f.results.at(5).ok);
    f.Close();assert(!f.controls->Retained()&&f.controls->OwnsSession(f.Session()));f.Run(29,"close",0);assert(f.results.at(29).ok&&f.calls.empty());++groups;
  }
  { // A foreign same-process sibling pause is auto-resumed by the exact arbiter
    // without publishing its opaque frame ID or invalidating our live session.
    Fixture f(true,"",true);f.Open();f.Run(2,"waitForPause");
    assert(f.domains->Event("Debugger.paused",f.Packet("foreign-script"),""));
    assert(!f.results.contains(2)&&f.Has("Debugger.resume")&&f.controls->OwnsSession(f.Session()));
    f.Ack("Debugger.resume");f.Pause();assert(f.results.at(2).ok);f.Close();++groups;
  }
  { // An author pause can precede enable ACK on the genuine child session.
    Fixture f(true,"actual-oopif-session",true);f.controls->Handle(1,JSON(R"({"action":"open"})"),100,f.Reply(1));
    f.Pause();assert(!f.results.contains(1));f.Ack("Debugger.enable");f.Ack("Debugger.setSkipAllPauses");
    assert(f.results.at(1).ok);f.Run(2,"waitForPause");assert(f.results.at(2).ok);f.Close();++groups;
  }
  { // Mixed-context ancestor stacks remain an explicit unsupported boundary.
    Fixture f(true,"",true);f.Open();f.Run(2,"waitForPause");auto packet=f.Packet();
    auto foreign=f.Packet("foreign-script")->GetList("callFrames")->GetDictionary(0)->Copy(false);
    packet->GetList("callFrames")->SetDictionary(1,foreign);
    assert(f.domains->Event("Debugger.paused",packet,""));assert(!f.results.at(2).ok&&f.Has("Debugger.resume"));
    f.Ack("Debugger.resume");if(f.Has("Debugger.disable"))f.Ack("Debugger.disable");assert(!f.controls->Retained());++groups;
  }
  { // A canceled child source request retains its scope after domain loss until
    // the actual registered callback arrives; a logical close is no receipt.
    Fixture f(true,"actual-oopif-session",true);f.Open();const auto token=f.Session();
    f.Run(2,"scriptSource",100,",\"scriptId\":\"actual-script\"");f.controls->Cancel(2);f.Run(3,"close",0);
    f.domains->Detached(f.inspector_session);assert(f.controls->Retained()&&f.scopes->Physical()==1&&!f.results.contains(3));
    f.Ack("Debugger.getScriptSource",false);assert(f.results.at(3).ok&&!f.controls->Retained()&&f.controls->OwnsSession(token));f.Run(4,"close",0);assert(f.results.at(4).ok&&f.calls.empty());++groups;
  }
  { // A canceled close observer can join exact, proven native retirement;
    // forged tokens still fail and no fresh Inspector command is admitted.
    Fixture f(true);f.Open();const auto token=f.Session();f.Run(2,"close",0);f.controls->Cancel(2);
    assert(f.controls->Retained()&&!f.results.contains(2));f.Ack("Debugger.disable");assert(!f.controls->Retained());
    f.Run(3,"close",0);assert(f.results.at(3).ok&&f.calls.empty());
    f.controls->Handle(4,JSON(R"({"action":"close","session":"unowned-token"})"),0,f.Reply(4));
    assert(!f.results.at(4).ok&&f.calls.empty()&&f.controls->OwnsSession(token));++groups;
  }
  { // Later physical retirement cannot rewrite the original terminal deadline
    // error of an admitted close into a successful cached completion.
    Fixture f(true);f.Open();f.Run(2,"close",5);f.Tick(5);assert(!f.results.at(2).ok&&f.controls->Retained());
    f.Ack("Debugger.disable");assert(!f.controls->Retained());f.Run(3,"close",0);
    assert(!f.results.at(3).ok&&f.results.at(3).error==f.results.at(2).error&&f.calls.empty());++groups;
  }
  { // An unpublished canceled open cannot evict the exact completed receipt
    // of an earlier public session, even after its late native cleanup ACKs.
    Fixture f(true);f.Open();const auto old=f.Session();f.Close();
    f.controls->Handle(31,JSON(R"({"action":"open"})"),100,f.Reply(31));f.controls->Cancel(31);
    f.Ack("Debugger.enable");f.Ack("Debugger.disable");assert(!f.results.contains(31)&&f.controls->OwnsSession(old));
    f.Run(32,"close",0);assert(f.results.at(32).ok&&f.calls.empty());++groups;
  }
  std::cout << "developer debugger component groups " << groups << " passed\n";
}
