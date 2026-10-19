// SPDX-License-Identifier: MIT
#include "developer_debugger_controls.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#include "debugger_pause_projection.h"
#include "developer_debugger_breakpoints.h"
#include "developer_debugger_evaluations.h"
namespace suitecut {
namespace {
std::atomic<size_t> developer_sessions{0};
constexpr size_t kSessions = 16, kOperations = 32, kCalls = 32;
constexpr double kCleanup = 30000;
bool DocumentValid(const DeveloperDebuggerControls::Document& doc) {
  return doc.epoch && doc.context > 0 && !doc.unique_context.empty() && doc.unique_context.size() <= 256;
}
std::string Quote(const std::string& text) { return DevToolsQuoteUnits(CefString(text).ToString16()); }
}
class DeveloperDebuggerControls::Impl : public std::enable_shared_from_this<Impl> {
 public:
  struct Operation {
    int owner = 0;
    std::string action;
    double deadline = 0;
    Reply reply;
    bool ack = false, actual_close = false;
    uint64_t preceding = 0, close_lease = 0;
  };
  struct Pause {
    uint64_t id = 0;
    double deadline = 0;
    std::string token, json;
    DebuggerDomains::Continue continuation;
    std::vector<std::string> frames;
  };
  struct Intent {
    uint64_t receipt = 0;
    double deadline = 0, cleanup = 0;
    bool actual_pause = false;
  };
  struct Completion {std::string token,error;};
  struct State {
    uint64_t lease = 0;
    FrameDebuggerScopes::Token scope;
    Document document;
    std::string token, inspector_session;
    std::shared_ptr<Completion> completion=std::make_shared<Completion>();
    bool counted = false, acquired = false, published = false, closing = false, release_requested = false, close_requested = false;
    std::optional<Pause> pause;
    std::optional<Intent> intent;
    std::unique_ptr<DeveloperDebuggerScripts> scripts;
    std::shared_ptr<DeveloperDebuggerBreakpoints> breakpoints;
    std::shared_ptr<DeveloperDebuggerEvaluations> evaluations;
    ~State() { if(counted)--developer_sessions; }
  };
  struct Call {
    std::shared_ptr<State> state;
    uint64_t receipt = 0;
    FrameDebuggerScopes::Receipt scope_receipt;
    double settlement = 0;
    bool closing = false;
  };
  explicit Impl(Hooks hooks) : hooks_(std::move(hooks)) {}
  Hooks hooks_;
  std::shared_ptr<State> state_;
  // Retained until real lease retirement/channel closure, including when the
  // public wrapper is destroyed while canonical cleanup is still outstanding.
  std::shared_ptr<Impl> physical_self_;
  std::map<int, std::shared_ptr<Operation>> operations_;
  // One logical startup waiter; no native lease exists until actual pins do.
  std::shared_ptr<Operation> opening_;
  std::map<uint64_t, Call> calls_;
  uint64_t next_call_ = 0;
  bool closed_ = false;
  std::shared_ptr<Completion> retired_;

  bool Expired(double deadline) const { return deadline > 0 && hooks_.now() >= deadline; }
  bool Current(const std::shared_ptr<State>& state) const { return state && !closed_ && state_ == state; }
  bool OwnerCurrent(const std::shared_ptr<State>&state)const{return hooks_.current()==state->document&&(!state->scope||hooks_.scopes->ObservedCurrent(state->scope));}
  void Wake() { if (hooks_.wake) hooks_.wake(); }
  bool Pending(const std::shared_ptr<Operation>& op) const {
    const auto it = operations_.find(op->owner);
    return it != operations_.end() && it->second == op;
  }
  bool Finish(const std::shared_ptr<Operation>& op, bool ok, const std::string& json, const std::string& error) {
    const auto it = operations_.find(op->owner);
    if (it == operations_.end() || it->second != op) return false;
    operations_.erase(it);
    if(opening_==op)opening_.reset();
    auto reply = std::move(op->reply);
    const bool expired=Expired(op->deadline);
    if(op->actual_close&&(!ok||expired)&&state_&&state_->lease==op->close_lease&&state_->completion->error.empty())
      state_->completion->error=expired?"Native debugger original deadline expired":error;
    if (expired) reply(false, "", "Native debugger original deadline expired");
    else reply(ok, json, error);
    return ok&&!expired;
  }
  static bool Mutating(const std::string& action) {
    return action == "open" || action == "pause" || action == "resume" ||
           action == "stepOver" || action == "stepInto" || action == "stepOut" ||
           action == "setBreakpoint" || action == "removeBreakpoint" || action == "evaluateOnCallFrame";
  }
  static bool Execution(const std::string& action) {
    return action == "pause" || action == "resume" || action == "stepOver" || action == "stepInto" || action == "stepOut";
  }
  std::shared_ptr<Operation> Admit(int owner, const std::string& action, double deadline, Reply reply) {
    // One reserved cleanup observer remains available with32 pending readers;
    // beginning close removes those logical readers before invoking callbacks.
    if(std::isfinite(deadline)&&Expired(deadline)){reply(false,"","Native debugger original deadline expired before admission");return {};}
    if (closed_ || owner < 1 || operations_.contains(owner) || operations_.size() >= kOperations + (action == "close" ? 1 : 0) ||
        !std::isfinite(deadline) || deadline < 0) {
      reply(false, "", "Native debugger owner, capacity or invalid budget rejected");
      return {};
    }
    auto op = std::make_shared<Operation>();
    op->owner = owner; op->action = action; op->deadline = deadline; op->reply = std::move(reply);
    operations_.emplace(owner, op);
    return op;
  }
  void RequestClose(const std::shared_ptr<State>& state, const std::string& error) {
    if (!Current(state) || state->close_requested) return;
    state->close_requested = true;
    if (hooks_.close_channel) hooks_.close_channel(error);
  }
  void Retire(const std::shared_ptr<State>& state) {
    if (!Current(state) || !state->closing || state->intent || (state->breakpoints && state->breakpoints->Retained()) || (state->evaluations && state->evaluations->Retained()) || hooks_.domains->HasLease(state->lease) ||
        std::any_of(calls_.begin(), calls_.end(), [&](const auto& item) { return item.second.state == state; })) return;
    if(state->scope){
      hooks_.scopes->Poll(state->scope);
      if(hooks_.scopes->Retains(state->scope))return;
    }
    std::vector<std::shared_ptr<Operation>> closes;
    for (const auto& [id, op] : operations_) if (op->action == "close") closes.push_back(op);
    // Moving the already-owned token cannot allocate in the actual receipt
    // path. Only this complete physical retirement authorizes a later close
    // observer to join; an arbitrary unknown token is never a success.
    if(state->completion->error.empty()&&std::any_of(closes.begin(),closes.end(),[&](const auto&op){return Expired(op->deadline);}))
      state->completion->error="Native debugger original deadline expired";
    const auto completed=state->completion;completed->token=std::move(state->token);if(state->published)retired_=completed;
    state_.reset(); physical_self_.reset();
    for (const auto& op : closes) Finish(op, completed->error.empty(), "{}", completed->error);
  }
  void Release(const std::shared_ptr<State>& state) {
    if (!Current(state) || !state->closing || state->release_requested || state->intent || (state->breakpoints && state->breakpoints->Retained()) || (state->evaluations && state->evaluations->Retained())) return;
    if (!hooks_.domains->HasLease(state->lease)) { Retire(state); return; }
    state->release_requested = true;
    auto self = shared_from_this();
    hooks_.domains->Release(state->lease, hooks_.now() + kCleanup,
      [self, state](bool ok, const std::string& error) {
        if (!self->Current(state)) return;
        // Actual arbiter channel retirement can finish a release observer with
        // its channel-closed notification. Preserve the established physical
        // close contract there; an error while its lease still exists remains
        // a real failed cleanup and requires actual posted channel closure.
        if(!ok&&self->hooks_.domains->HasLease(state->lease)){
          if(state->completion->error.empty())state->completion->error=error;
          self->RequestClose(state,error);
        }
        self->Retire(state); self->Wake();
      });
  }
  void BeginClose(std::shared_ptr<State> state, const std::string& error) {
    // Scope/submodule cleanup may synchronously finish the last developer
    // lease and reset state_, including when Changed passed that member.
    // Keep both owners alive and stop after any callback replaced/retired it.
    const auto self = shared_from_this();
    if (!Current(state)) return;
    state->closing = true; state->pause.reset();
    if(state->scope)hooks_.scopes->Close(state->scope);
    if(!Current(state))return;
    if(state->scripts)state->scripts->Clear();
    if (state->intent && !state->intent->cleanup) state->intent->cleanup = hooks_.now() + kCleanup;
    std::vector<std::shared_ptr<Operation>> fail;
    for (const auto& [id, op] : operations_) if (op->action != "close") fail.push_back(op);
    for (const auto& op : fail) {
      Finish(op, false, "", error);
      if(!Current(state))return;
    }
    if(state->breakpoints)state->breakpoints->Close();
    if(!Current(state))return;
    if(state->evaluations)state->evaluations->Close();
    if(!Current(state))return;
    Release(state); Wake();
  }
  bool Send(const std::shared_ptr<State>& state, const std::string& method, Dict params,
            double deadline, DebuggerDomains::Reply done) {
    if (!Current(state) || !state->acquired || calls_.size() >= kCalls || Expired(deadline) ||
        next_call_ == 9007199254740991ULL) return false;
    const auto id = next_call_+1;
    auto self = shared_from_this();
    DebuggerDomains::Reply actual = [self, state, id, done = std::move(done)](bool ok, Dict data, const std::string& error) {
      const auto found = self->calls_.find(id);
      if (found == self->calls_.end()) return;
      const auto hold = found->second.receipt;
      const auto scope_hold=found->second.scope_receipt;
      self->calls_.erase(found);
      // The consumer processes late work/cleanup before the preceding Hold
      // retires. A logical cancellation never erases this actual callback.
      if (self->Current(state)) done(ok, data, error);
      self->hooks_.domains->Retire(hold);
      if(scope_hold)self->hooks_.scopes->Retire(scope_hold);
      self->Release(state); self->Retire(state); self->Wake();
    };
    // Stage the fallible outgoing callback/map node before either reservation.
    std::map<uint64_t,Call>staged;
    staged.emplace(id,Call{state,0,{},(deadline>0?deadline:hooks_.now()+kCleanup)+kCleanup,false});
    struct Unsent {
      Impl&self;uint64_t domain=0;FrameDebuggerScopes::Receipt scope;
      ~Unsent(){if(domain)self.hooks_.domains->Retire(domain);if(scope)self.hooks_.scopes->Retire(scope);}
    }unsent{*this};
    if(state->scope){unsent.scope=hooks_.scopes->Hold(state->scope);if(!unsent.scope)return false;}
    unsent.domain=hooks_.domains->Hold(state->lease);if(!unsent.domain)return false;
    staged.begin()->second.receipt=unsent.domain;staged.begin()->second.scope_receipt=unsent.scope;
    calls_.insert(staged.extract(staged.begin()));next_call_=id;
    unsent.domain=0;unsent.scope={};
    struct Dispatch{
      Impl&self;std::shared_ptr<State>state;uint64_t id;bool returned=false;
      ~Dispatch(){if(!returned&&self.calls_.contains(id)){self.BeginClose(state,"Native debugger sender threw with unsettled physical ownership");self.RequestClose(state,"Native debugger sender could not establish physical settlement");}}
    }dispatch{*this,state,id};
    const bool admitted = hooks_.send(method, params, deadline, actual);
    dispatch.returned=true;
    if (!admitted) actual(false, nullptr, "Native debugger command dispatch was not accepted");
    Wake(); return admitted;
  }
  double PauseDeadline(const std::shared_ptr<State>& state) const {
    if (!Current(state)) return hooks_.now();
    // A canceled armed pause still needs genuine cleanup capture/resume. This
    // separate cleanup watchdog never revives a public operation's old budget.
    if (state->closing) return state->intent && state->intent->cleanup > 0 ? state->intent->cleanup : hooks_.now();
    double deadline = 0; bool unlimited = false, found = false;
    for (const auto& [id, op] : operations_) {
      if (op->action != "pause" && op->action != "waitForPause" &&
          op->action != "stepOver" && op->action != "stepInto" && op->action != "stepOut") continue;
      found = true;
      if (!op->deadline) unlimited = true;
      else if (!deadline || op->deadline < deadline) deadline = op->deadline;
    }
    return deadline ? deadline : (found && unlimited ? 0 : hooks_.now() + 30000);
  }
  bool PauseCurrent(const std::shared_ptr<State>& state) const {
    return state->pause && hooks_.domains->IsPaused(state->lease, state->pause->id);
  }
  bool FrameCurrent(const std::shared_ptr<State>& state,const std::string&pause,const std::string&frame)const{
    if(!Current(state)||state->closing||!OwnerCurrent(state)||!PauseCurrent(state)||state->pause->token!=pause)return false;
    const auto&frames=state->pause->frames;
    return std::find(frames.begin(),frames.end(),frame)!=frames.end();
  }
  void Deliver(const std::shared_ptr<State>& state) {
    if (!Current(state) || !state->acquired || state->closing || !OwnerCurrent(state) || !PauseCurrent(state)) return;
    const auto pause = *state->pause;
    std::vector<std::shared_ptr<Operation>> ready;
    for (const auto& [id, op] : operations_) {
      if (op->action == "waitForPause" || (op->action == "pause" && op->ack) ||
          ((op->action == "stepOver" || op->action == "stepInto" || op->action == "stepOut") &&
           op->ack && op->preceding != pause.id)) ready.push_back(op);
    }
    for (const auto& op : ready) Finish(op, true, pause.json, "");
  }
  void Paused(const std::shared_ptr<State>& state, uint64_t id, Dict packet,
              double deadline, DebuggerDomains::Continue continuation) {
    if (!Current(state)) return;
    if (state->intent) {
      const auto receipt = state->intent->receipt;
      state->intent.reset();
      // This actual developer pause supersedes the armed next-statement intent;
      // the arbiter now retains its own actual capture/resume obligation.
      hooks_.domains->Retire(receipt);
    }
    if (state->closing) { Release(state); return; }
    const auto token = state->token + ":pause:" + std::to_string(id);
    std::string json;
    if (!OwnerCurrent(state) || !hooks_.owns_pause(packet, state->document) ||
        !DebuggerPauseProjection::Build(packet, state->token, token, json)) {
      BeginClose(state, "Native debugger supports only bounded actual current document pauses"); return;
    }
    std::vector<std::string>frames;
    const auto actual_frames=packet->GetList("callFrames");
    for(size_t i=0;i<actual_frames->GetSize();++i)frames.push_back(actual_frames->GetDictionary(i)->GetString("callFrameId").ToString());
    state->pause = Pause{id, deadline, token, std::move(json), std::move(continuation), std::move(frames)};
    Deliver(state); Wake();
  }
  void Open(const std::shared_ptr<Operation>& op) {
    if (state_||opening_) { Finish(op, false, "", "Native debugger session is already active, waiting for actual metadata or physically closing"); return; }
    const auto document = hooks_.current();
    if(bool(hooks_.scopes)!=bool(hooks_.scope_pin)||
       (hooks_.pause_eligibility&&!hooks_.scopes)||
       (!hooks_.agent_controls&&(!hooks_.scopes||!hooks_.pause_eligibility)&&!hooks_.target_pause_eligibility)||
       (hooks_.target_pause_eligibility&&(hooks_.scopes||hooks_.inspector_session.empty()))||
       hooks_.inspector_session.size()>128||hooks_.inspector_session.find('\0')!=std::string::npos){
      Finish(op,false,"","Native debugger exact channel/scope adapter is incomplete");return;
    }
    FrameDebuggerScopes::Pin pin;
    if(hooks_.scopes&&(!DocumentValid(document)||!hooks_.scope_pin(document,pin))){opening_=op;Wake();return;}
    if(hooks_.scopes&&pin.channel.session!=hooks_.inspector_session){Finish(op,false,"","Native debugger scope belongs to another actual Inspector channel");return;}
    if (!DocumentValid(document) || hooks_.owner_namespace.empty() || hooks_.owner_namespace.size() > 128) {
      Finish(op, false, "", "Actual current native root document is unavailable"); return;
    }
    auto state = std::make_shared<State>();
    size_t count = developer_sessions.load();
    while (count < kSessions && !developer_sessions.compare_exchange_weak(count, count + 1)) {}
    if (count >= kSessions) { Finish(op, false, "", "Native debugger process session capacity exceeded"); return; }
    state->counted=true;
    state->document = document; state->inspector_session=hooks_.inspector_session; state->lease = hooks_.lease();
    state->token = hooks_.owner_namespace + ":developer:" + std::to_string(state->lease);
    if (!state->lease) { Finish(op, false, "", "Native debugger lease identity exhausted"); return; }
    state_ = state; physical_self_ = shared_from_this();
    auto weak = weak_from_this();
    const std::weak_ptr<State> weak_state=state;
    DeveloperDebuggerScripts::Hooks scripts;
    scripts.session=state->token;scripts.now=hooks_.now;
    scripts.current=[weak,weak_state]{const auto self=weak.lock();const auto state=weak_state.lock();return self&&state&&self->Current(state)&&!state->closing&&self->OwnerCurrent(state);};
    scripts.page=[weak,weak_state](const std::string&after,const uint64_t*revision,double deadline,DeveloperDebuggerScripts::Page&page,std::string&error){
      const auto self=weak.lock();const auto state=weak_state.lock();
      if(!self||!state||!self->hooks_.script_page){error="Actual native debugger script metadata adapter unavailable";return false;}
      return self->hooks_.script_page(state->document,after,revision,deadline,page,error);
    };
    state->scripts=std::make_unique<DeveloperDebuggerScripts>(std::move(scripts));
    DeveloperDebuggerBreakpoints::Hooks breakpoint;
    breakpoint.session=state->token;breakpoint.now=hooks_.now;
    breakpoint.current=[weak,weak_state]{const auto self=weak.lock();const auto state=weak_state.lock();return self&&state&&self->Current(state)&&self->OwnerCurrent(state);};
    breakpoint.owns_script=[weak,weak_state](const std::string&script){const auto self=weak.lock();const auto state=weak_state.lock();return self&&state&&self->hooks_.owns_breakpoint_script&&self->hooks_.owns_breakpoint_script(script,state->document);};
    breakpoint.send=[weak,weak_state](const std::string&method,Dict params,double deadline,DebuggerDomains::Reply reply){const auto self=weak.lock();const auto state=weak_state.lock();return self&&state&&self->Send(state,method,params,deadline,std::move(reply));};
    breakpoint.changed=[weak,weak_state]{const auto self=weak.lock();const auto state=weak_state.lock();if(self&&state){self->Release(state);self->Retire(state);self->Wake();}};
    breakpoint.close_channel=[weak,weak_state](const std::string&error){const auto self=weak.lock();const auto state=weak_state.lock();if(self&&state){self->BeginClose(state,error);self->RequestClose(state,error);}};
    state->breakpoints=std::make_shared<DeveloperDebuggerBreakpoints>(std::move(breakpoint));
    DeveloperDebuggerEvaluations::Hooks evaluations;
    evaluations.session=state->token;evaluations.now=hooks_.now;
    evaluations.current=[weak,weak_state](const std::string&pause,const std::string&frame){const auto self=weak.lock();const auto state=weak_state.lock();return self&&state&&self->FrameCurrent(state,pause,frame);};
    evaluations.send=[weak,weak_state](const std::string&method,Dict params,double deadline,DebuggerDomains::Reply reply){const auto self=weak.lock();const auto state=weak_state.lock();return self&&state&&self->Send(state,method,params,deadline,std::move(reply));};
    evaluations.changed=[weak,weak_state]{const auto self=weak.lock();const auto state=weak_state.lock();if(self&&state){self->Release(state);self->Retire(state);self->Wake();}};
    evaluations.close_channel=[weak,weak_state](const std::string&error){const auto self=weak.lock();const auto state=weak_state.lock();if(self&&state){self->BeginClose(state,error);self->RequestClose(state,error);}};
    state->evaluations=std::make_shared<DeveloperDebuggerEvaluations>(std::move(evaluations));
    DebuggerDomains::DeveloperDeadline pause_deadline=[weak, state](Dict) { if (auto self = weak.lock()) return self->PauseDeadline(state); return 0.0; };
    DebuggerDomains::DeveloperPaused paused=[weak, state](uint64_t id, Dict packet, double deadline, DebuggerDomains::Continue continuation) {
        if (auto self = weak.lock()) self->Paused(state, id, packet, deadline, std::move(continuation));
      };
    DebuggerDomains::DeveloperExpired rejected=[weak, state](uint64_t, Dict, const std::string& error) {
        if (auto self = weak.lock()) {
          if (self->Current(state)) {
            state->pause.reset();
            // Expiry can be notified while Event is still constructing its
            // auto-resume capture. Retire intent only after that event returns.
            if (state->intent) state->intent->actual_pause = true;
          }
          self->BeginClose(state, error);
        }
      };
    DebuggerDomains::Ready ready=[weak, state, op](bool ok, const std::string& error) {
        if (auto self = weak.lock()) {
          if (!self->Current(state)) return;
          state->acquired = ok;
          const bool scope_ready=!state->scope||self->hooks_.scopes->Ready(state->scope,ok);
          if (!ok || !scope_ready || !self->Pending(op) || self->Expired(op->deadline) || self->hooks_.current() != state->document) {
            self->Finish(op, false, "", self->Expired(op->deadline) ? "Native debugger original deadline expired during open" : !ok ? error : "Native debugger original open owner or document changed");
            self->BeginClose(state, !ok ? error : "Native debugger open canceled or expired"); return;
          }
          state->published=true;
          self->Finish(op, true, "{\"session\":" + Quote(state->token) + "}", "");
          self->Deliver(state); self->Wake();
        }
      };
    if(hooks_.scopes){
      std::string error;state->scope=hooks_.scopes->Reserve(std::move(pin),state->lease,op->deadline,error);
      if(!state->scope){Finish(op,false,"",error);BeginClose(state,error);return;}
      if(!hooks_.scopes->BeginAcquire(state->scope)){Finish(op,false,"","Native debugger exact scope changed before acquisition");BeginClose(state,"Native debugger exact scope changed before acquisition");Retire(state);return;}
    }
    // The bracket stays alive through synchronous Ready failure/reentrancy.
    struct Acquisition{
      std::shared_ptr<Impl>self;std::shared_ptr<State>state;bool returned=false;
      ~Acquisition(){
        if(!state->scope)return;
        if(!returned){self->BeginClose(state,"Native debugger acquisition threw before returning");self->RequestClose(state,"Native debugger acquisition could not establish physical settlement");}
        self->hooks_.scopes->AcquireReturned(state->scope);self->Retire(state);
      }
    }acquisition{shared_from_this(),state};
    if(hooks_.pause_eligibility||hooks_.target_pause_eligibility){
      DebuggerDomains::DeveloperEligibility eligible=[weak,state](Dict packet){
        const auto self=weak.lock();
        if(!self||!self->Current(state)||state->closing||!self->OwnerCurrent(state))return DebuggerDomains::DeveloperPauseDisposition::Unsupported;
        return self->hooks_.target_pause_eligibility?self->hooks_.target_pause_eligibility(packet):self->hooks_.pause_eligibility(state->scope,packet);
      };
      hooks_.domains->AcquireScopedDeveloper(state->inspector_session,state->lease,op->deadline,std::move(pause_deadline),std::move(paused),std::move(rejected),std::move(eligible),std::move(ready));
    }else hooks_.domains->AcquireDeveloper(state->inspector_session,state->lease,op->deadline,std::move(pause_deadline),std::move(paused),std::move(rejected),std::move(ready));
    acquisition.returned=true;
  }
  void PauseRequest(const std::shared_ptr<State>& state, const std::shared_ptr<Operation>& op) {
    if (PauseCurrent(state)) { Finish(op, true, state->pause->json, ""); return; }
    if (state->intent) { Finish(op, false, "", "A native next-statement pause request already owns this session"); return; }
    const auto intent = hooks_.domains->Hold(state->lease);
    if (!intent) { Finish(op, false, "", "Native debugger pause-intent capacity exceeded"); return; }
    state->intent = Intent{intent, op->deadline, 0, false};
    auto self = shared_from_this();
    if (!Send(state, "Debugger.pause", CefDictionaryValue::Create(), op->deadline,
      [self, state, op](bool ok, Dict, const std::string& error) {
        if (!ok) {
          // A failed ACK does not prove a previously accepted pause request had
          // no execution effect. Keep the intent until actual pause/closure.
          self->Finish(op, false, "", error); self->BeginClose(state, error); return;
        }
        op->ack = true;
        if (self->Expired(op->deadline) || !self->Pending(op)) {
          self->Finish(op, false, "", "Native debugger original pause deadline expired");
          self->BeginClose(state, "Native debugger pause owner canceled or expired"); return;
        }
        self->Deliver(state);
      })) {
      // No command was physically admitted: the intent can genuinely retire.
      if (state->intent) { hooks_.domains->Retire(state->intent->receipt); state->intent.reset(); }
      Finish(op, false, "", "Native debugger pause dispatch was not admitted");
      Release(state);
    }
  }
  void Continue(const std::shared_ptr<State>& state, const std::shared_ptr<Operation>& op, Dict params) {
    if (!PauseCurrent(state) || params->GetType("pause") != VTYPE_STRING || params->GetString("pause").ToString() != state->pause->token) {
      Finish(op, false, "", "Native debugger pause token is stale, resumed or belongs to another owner"); return;
    }
    auto pause = std::move(*state->pause); state->pause.reset();
    op->preceding = pause.id;
    auto kind = DebuggerDomains::Continuation::Resume;
    if (op->action == "stepOver") kind = DebuggerDomains::Continuation::StepOver;
    else if (op->action == "stepInto") kind = DebuggerDomains::Continuation::StepInto;
    else if (op->action == "stepOut") kind = DebuggerDomains::Continuation::StepOut;
    auto self = shared_from_this();
    pause.continuation(kind, op->deadline, [self, state, op](bool ok, const std::string& error) {
      if (!self->Current(state)) return;
      if (!ok || self->Expired(op->deadline)) {
        self->Finish(op, false, "", self->Expired(op->deadline) ? "Native debugger original continuation deadline expired" : error);
        self->BeginClose(state, !ok ? error : "Native debugger continuation expired"); return;
      }
      op->ack = true;
      if (op->action == "resume") self->Finish(op, true, "{}", "");
      else self->Deliver(state);
    });
  }
  void Source(const std::shared_ptr<State>& state, const std::shared_ptr<Operation>& op, Dict params) {
    if (params->GetType("scriptId") != VTYPE_STRING || params->GetString("scriptId").empty() ||
        params->GetString("scriptId").length() > 128 ||
        !hooks_.owns_script(params->GetString("scriptId").ToString(), state->document)) {
      Finish(op, false, "", "Native debugger source requires an actual current root-document script identity"); return;
    }
    auto request = CefDictionaryValue::Create(); request->SetString("scriptId", params->GetString("scriptId"));
    auto self = shared_from_this();
    if (!Send(state, "Debugger.getScriptSource", request, op->deadline,
      [self, state, op](bool ok, Dict result, const std::string& error) {
        if (!self->Pending(op)) return;
        if (!ok || !result || !result->HasKey("scriptSource")) { self->Finish(op, false, "", !ok ? error : "Actual native debugger source is absent"); return; }
        if (state->closing || !self->OwnerCurrent(state)) { self->Finish(op, false, "", "Native debugger source document changed"); return; }
        std::u16string units;
        const auto value = result->GetValue("scriptSource");
        if (value->GetType() == VTYPE_STRING) units = value->GetString().ToString16();
        else if (!DevToolsReadTag(value, units)) { self->Finish(op, false, "", "Actual native debugger source is not text"); return; }
        if (units.size() > 262144) { self->Finish(op, false, "", "Native debugger source exceeds 262144 UTF-16 units"); return; }
        self->Finish(op, true, "{\"source\":" + DevToolsQuoteUnits(units) + "}", "");
      })) Finish(op, false, "", "Native debugger source dispatch was not admitted");
  }
  void Handle(int owner, Dict params, double deadline, Reply reply) {
    if (!reply) return;
    const auto action = params && params->GetType("action") == VTYPE_STRING ? params->GetString("action").ToString() : "";
    if (action != "open" && action != "pause" && action != "waitForPause" && action != "resume" &&
        action != "stepOver" && action != "stepInto" && action != "stepOut" && action != "scriptSource" && action != "scripts" && action != "evaluateOnCallFrame" && action != "setBreakpoint" && action != "removeBreakpoint" && action != "close") {
      reply(false, "", "Unsupported native debugger action"); return;
    }
    if(!hooks_.agent_controls&&(action=="pause"||action=="stepOver"||action=="stepInto"||action=="stepOut")){
      reply(false,"","Agent-wide pause and step controls are unsupported for native frame or worker debugger sessions");return;
    }
    const auto op = Admit(owner, action, deadline, std::move(reply));
    if (!op) return;
    if (action == "open") { Open(op); return; }
    const auto state = state_;
    if(action=="close"&&params->GetType("session")==VTYPE_STRING&&retired_&&params->GetString("session").ToString()==retired_->token){
      const auto completed=retired_;Finish(op,completed->error.empty(),"{}",completed->error);return;
    }
    if (!state || params->GetType("session") != VTYPE_STRING || params->GetString("session").ToString() != state->token) {
      Finish(op, false, "", "Native debugger session token is closed or belongs to another owner"); return;
    }
    if (action == "close") { op->actual_close=true;op->close_lease=state->lease;BeginClose(state, "Native debugger session is closing"); return; }
    if (!state->acquired || state->closing || !OwnerCurrent(state)) {
      Finish(op, false, "", "Native debugger document is changed, unavailable or closing"); return;
    }
    if (Execution(action) && std::any_of(operations_.begin(), operations_.end(), [&](const auto& item) {
      return item.second != op && Execution(item.second->action);
    })) {
      Finish(op, false, "", "A native debugger execution control already owns its original budget"); return;
    }
    if (action == "pause") PauseRequest(state, op);
    else if (action == "waitForPause") Deliver(state);
    else if (action == "scriptSource") Source(state, op, params);
    else if(action=="scripts"){
      if(params->HasKey("cursor")&&(params->GetType("cursor")!=VTYPE_STRING||params->GetString("cursor").empty()||params->GetString("cursor").length()>512)){
        Finish(op,false,"","Native debugger script cursor must be an opaque bounded token");return;
      }
      std::string json,error;
      const bool ok=state->scripts->Read(params->HasKey("cursor")?params->GetString("cursor").ToString():"",deadline,json,error);
      Finish(op,ok,json,error);
    }
    else if(action=="evaluateOnCallFrame"){
      if(params->GetType("pause")!=VTYPE_STRING||params->GetType("callFrameId")!=VTYPE_STRING||params->GetType("expressionJSON")!=VTYPE_STRING){Finish(op,false,"","Native paused evaluation requires exact pause/frame and source fields");return;}
      auto self=shared_from_this();state->evaluations->Evaluate(owner,params->GetString("pause").ToString(),params->GetString("callFrameId").ToString(),params->GetString("expressionJSON").ToString(),deadline,
        [self,op](bool ok,const std::string&json,const std::string&error){self->Finish(op,ok,json,error);});
    }
    else if (action == "setBreakpoint" || action == "removeBreakpoint") {
      auto self=shared_from_this();
      const auto reply=[self,op](bool ok,const std::string&json,const std::string&error){return self->Finish(op,ok,json,error);};
      if(action=="setBreakpoint")state->breakpoints->Set(owner,params->GetType("location")==VTYPE_DICTIONARY?params->GetDictionary("location"):nullptr,deadline,reply);
      else if(params->GetType("breakpoint")==VTYPE_STRING&&params->GetString("breakpoint").length()<=512)state->breakpoints->Remove(owner,params->GetString("breakpoint").ToString(),deadline,reply);
      else Finish(op,false,"","Native debugger breakpoint token is absent or oversized");
    }
    else Continue(state, op, params);
    Wake();
  }
  void Cancel(int owner) {
    const auto found = operations_.find(owner);
    if (found == operations_.end()) return;
    const auto op = found->second; operations_.erase(found); op->reply = {};
    if(opening_==op)opening_.reset();
    if(state_&&state_->breakpoints)state_->breakpoints->Cancel(owner);
    if(state_&&state_->evaluations)state_->evaluations->Cancel(owner);
    if (Mutating(op->action) && state_) BeginClose(state_, "Native debugger operation canceled");
  }
  void Changed() { if (state_) BeginClose(state_, "Native debugger root document changed; replacement recovery is unsupported"); }
  void RealmDestroyed(uint64_t actual_source_epoch){
    const auto state=state_;if(!state||state->document.epoch!=actual_source_epoch)return;
    if(state->evaluations)state->evaluations->RealmDestroyed();
    BeginClose(state,"Actual native debugger root V8 realm destroyed");
  }
  void Dispose() { if(opening_)Finish(opening_,false,"","Native debugger controller disposed before actual default metadata");if (state_) BeginClose(state_, "Native debugger controller disposed"); }
  void Tick(double now) {
    std::vector<std::shared_ptr<Operation>> expired;
    for (const auto& [id, op] : operations_) if (op->deadline > 0 && now >= op->deadline) expired.push_back(op);
    for (const auto& op : expired) {
      if (!Pending(op)) continue;
      const auto expired_state = state_;
      Finish(op, false, "", "Native debugger original deadline expired");
      if (Mutating(op->action) && expired_state) BeginClose(expired_state, "Native debugger original deadline expired");
    }
    if(opening_&&Pending(opening_)){const auto pending=std::move(opening_);Open(pending);}
    const auto state = state_;
    if (!state) return;
    if(!state->closing&&!OwnerCurrent(state))BeginClose(state,"Native debugger exact default-document scope changed");
    if(!Current(state))return;
    if(state->breakpoints)state->breakpoints->Tick(now);
    if(state->evaluations)state->evaluations->Tick(now);
    if (state->intent && state->intent->actual_pause) {
      const auto receipt = state->intent->receipt; state->intent.reset();
      hooks_.domains->Retire(receipt);
    }
    if (state->intent && state->intent->cleanup > 0 && now >= state->intent->cleanup)
      RequestClose(state, "Native debugger accepted pause intent did not settle within physical cleanup grace");
    for (auto& [id, call] : calls_) if (!call.closing && now >= call.settlement) {
      call.closing = true; RequestClose(call.state, "Native debugger accepted command did not settle within physical grace");
    }
    if (state->pause && !PauseCurrent(state)) state->pause.reset();
    Release(state); Retire(state);
  }
  void Closed() {
    if (closed_) return;
    closed_ = true;
    // This method is an actual channel boundary, never a logical page abort.
    auto state = std::move(state_); calls_.clear(); physical_self_.reset();
    opening_.reset();
    if (state) { if(state->scripts)state->scripts->Clear(); state->intent.reset(); state->pause.reset(); if(state->breakpoints)state->breakpoints->Closed(); if(state->evaluations)state->evaluations->Closed(); }
    std::vector<std::shared_ptr<Operation>> pending;
    for (const auto& [id, op] : operations_) pending.push_back(op);
    for (const auto& op : pending) Finish(op, false, "", "Actual native debugger channel closed");
  }
};

DeveloperDebuggerControls::DeveloperDebuggerControls(Hooks hooks) : impl_(std::make_shared<Impl>(std::move(hooks))) {}
DeveloperDebuggerControls::~DeveloperDebuggerControls() { impl_->Dispose(); }
void DeveloperDebuggerControls::Handle(int owner, Dict params, double deadline, Reply reply) { impl_->Handle(owner, params, deadline, std::move(reply)); }
void DeveloperDebuggerControls::Cancel(int owner) { impl_->Cancel(owner); }
void DeveloperDebuggerControls::Changed() { impl_->Changed(); }
void DeveloperDebuggerControls::RealmDestroyed(uint64_t actual_source_epoch){impl_->RealmDestroyed(actual_source_epoch);}
void DeveloperDebuggerControls::Tick(double now) { impl_->Tick(now); }
void DeveloperDebuggerControls::Closed() { impl_->Closed(); }
bool DeveloperDebuggerControls::Retained() const { return impl_->state_||impl_->opening_||!impl_->operations_.empty()||!impl_->calls_.empty(); }
bool DeveloperDebuggerControls::OwnsSession(const std::string& token) const { return !token.empty()&&((impl_->state_&&impl_->state_->token==token)||(impl_->retired_&&impl_->retired_->token==token)); }
bool DeveloperDebuggerControls::NeedsTick() const {
  if(impl_->opening_)return true;
  for (const auto& [id, op] : impl_->operations_) if (op->deadline > 0) return true;
  for (const auto& [id, call] : impl_->calls_) if (!call.closing) return true;
  const auto state = impl_->state_;
  return state && ((state->breakpoints && state->breakpoints->NeedsTick()) || (state->evaluations && state->evaluations->NeedsTick()) || (!state->close_requested && state->intent && state->intent->cleanup > 0));
}
size_t DeveloperDebuggerControls::PhysicalCount() const { return impl_->calls_.size() + (impl_->state_ && impl_->state_->intent ? 1 : 0); }
void DeveloperDebuggerControls::Diagnostics(Dict output) const {
  output->SetInt("nativeDeveloperDebuggerScriptCursors", impl_->state_ && impl_->state_->scripts ? int(impl_->state_->scripts->Retained()) : 0);
  output->SetInt("nativeDeveloperDebuggerProcessScriptCursors", int(DeveloperDebuggerScripts::ProcessRetained()));
  output->SetInt("nativeDeveloperDebuggerSessions", impl_->state_ ? 1 : 0);
  output->SetInt("nativeDeveloperDebuggerWaiters", int(impl_->operations_.size()));
  output->SetInt("nativeDeveloperDebuggerPhysical", int(PhysicalCount()));
  output->SetInt("nativeDeveloperDebuggerProcessSessions", int(developer_sessions.load()));
  output->SetInt("nativeDeveloperDebuggerBreakpoints", impl_->state_ && impl_->state_->breakpoints ? int(impl_->state_->breakpoints->Retained()) : 0);
  output->SetInt("nativeDeveloperDebuggerEvaluations", impl_->state_ && impl_->state_->evaluations ? int(impl_->state_->evaluations->Retained()) : 0);
  output->SetInt("nativeDeveloperDebuggerProcessEvaluations", int(DeveloperDebuggerEvaluations::ProcessRetained()));
  output->SetInt("nativeDeveloperDebuggerProcessBreakpoints", int(DeveloperDebuggerBreakpoints::ProcessRetained()));
}
}  // namespace suitecut
