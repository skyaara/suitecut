// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "include/cef_api_hash.h"
#include "include/cef_task.h"
#include "artifact_controls.h"
#include "operation_registry.h"

// Controlled native scheduling/CDP seams. Real CEF values are used; actual
// child-renderer pixels and lifecycle are verified separately in the live proof.
namespace {
std::deque<CefRefPtr<CefTask>> scheduled;
class Task : public CefTask {
 public:
  explicit Task(std::function<void()> callback) : callback_(std::move(callback)) {}
  void Execute() override { callback_(); }
 private:
  std::function<void()> callback_;
  IMPLEMENT_REFCOUNTING(Task);
};
bool TestPostDelayedTask(cef_thread_id_t thread, CefRefPtr<CefTask> task, int64_t delay) {
  assert(thread == TID_UI && delay == 10); scheduled.push_back(task); return true;
}
void DrainTasks() {
  for (int iteration = 0; iteration < 4096 && !scheduled.empty(); ++iteration) {
    auto task = scheduled.front(); scheduled.pop_front(); task->Execute();
  }
  assert(scheduled.empty());
}
}  // namespace
#define CefPostDelayedTask TestPostDelayedTask
class Source : public CefBaseRefCounted {
 public:
  using Dict = SuiteCutArtifactController::Dict;
  using Callback = SuiteCutArtifactController::Callback;
  struct FrameState { std::string id,parent,session,loader; int context = 1,utility_context = 2; std::string url = "about:blank"; std::set<std::string> milestones{"commit"}; };
  struct Call { int owner; Dict params; Callback callback; std::string session,frame; };
  struct Fire { Dict params; std::string session; };
  SuiteCutOperationRegistry operations_;
  std::map<int,uint64_t> artifact_attempts_;
  std::map<std::string,FrameState> frame_states_;
  std::map<std::string,std::string> sessions_;
  std::map<int,std::string> operation_frames_,operation_sessions_;
  std::string main_frame_id_ = "main";
  bool browser_ = true, closing_page_ = false;
  std::deque<Call> calls;
  std::vector<Fire> fires;
  int retries = 0;
  Source() {
    frame_states_["main"] = {"main","","","root-loader",1,2};
    frame_states_["alpha"] = {"alpha","main","","same-loader",3,4};
    frame_states_["beta"] = {"beta","main","oopif","cross-loader",3,4};
    sessions_["oopif"] = "beta";
  }
  void Admit(int owner) {
    assert(operations_.Begin(owner,100,50)); ++artifact_attempts_[owner];
    operation_frames_[owner] = "main"; operation_sessions_[owner] = "";
  }
  void EnsureUtilityWorld(const std::string&) {}
  bool Descendant(const std::string& child,const std::string& root) {
    auto current = child;
    for (size_t step = 0; step <= frame_states_.size(); ++step) {
      if (current == root) return true;
      const auto frame = frame_states_.find(current); if (frame == frame_states_.end() || frame->second.parent.empty()) return false;
      current = frame->second.parent;
    }
    return false;
  }
  void DevTools(int owner,const std::string& method,Dict params,Callback callback) {
    assert(operations_.Contains(owner) && method == "Runtime.evaluate");
    calls.push_back({owner,params->Copy(false),std::move(callback),operation_sessions_[owner],operation_frames_[owner]});
  }
  int SendDevTools(const std::string& method,Dict params,const std::string& session) {
    assert(method == "Runtime.evaluate"); fires.push_back({params->Copy(false),session}); return 1;
  }
  void RetryContext(int owner) { ++retries; ArtifactDocumentCleanup(owner,false); }
  void Ack() {
    assert(!calls.empty()); auto call = std::move(calls.front()); calls.pop_front();
    call.callback(true,"{\"result\":{\"type\":\"boolean\",\"value\":true}}");
  }
  #include "artifact_documents.inc"
 private:
  IMPLEMENT_REFCOUNTING(Source);
};
#undef CefPostDelayedTask

int main() {
  assert(cef_api_hash(CEF_API_VERSION,0));
  {
    auto source = CefRefPtr<Source>(new Source()); source->Admit(1); int done = 0;
    source->ArtifactDocuments(1,"(()=>true)()",[&done](bool ok,const std::string&) { assert(ok); ++done; });
    assert(source->ArtifactDocumentContextCount() == 1 && source->calls.size() == 1);
    assert(source->calls.front().session.empty() && source->calls.front().frame == "main");
    assert(source->calls.front().params->GetInt("contextId") == 4);
    source->Ack(); assert(source->ArtifactDocumentContextCount() == 2);
    assert(source->calls.front().session == "oopif" && source->calls.front().frame == "main");
    // Same context integers in different renderer sessions must remain separate.
    assert(source->calls.front().params->GetInt("contextId") == 4);
    source->Ack(); assert(done == 1 && source->operation_sessions_[1].empty());
    source->ArtifactDocumentCleanup(1,false); assert(source->fires.size() == 2);
    assert(source->fires[0].session.empty() && source->fires[1].session == "oopif");
    for (const auto& fire : source->fires) assert(fire.params->GetInt("contextId") == 4 && fire.params->GetBool("returnByValue"));
    assert(source->artifact_document_work_.empty() && source->ArtifactDocumentContextCount() == 0);
    assert(source->operations_.Finish(1)); DrainTasks();
  }
  {
    auto source = CefRefPtr<Source>(new Source()); source->Admit(1); int old_done = 0,new_done = 0;
    source->ArtifactDocuments(1,"(()=>true)()",[&old_done](bool,const std::string&) { ++old_done; });
    auto late = std::move(source->calls.front()); source->calls.pop_front();
    source->ArtifactDocumentCleanup(1,false); assert(source->operations_.Finish(1)); source->Admit(1);
    source->ArtifactDocuments(1,"(()=>true)()",[&new_done](bool ok,const std::string&) { assert(ok); ++new_done; });
    const auto count = source->calls.size(); late.callback(true,"{\"result\":{\"value\":true}}");
    assert(source->calls.size() == count && old_done == 0);
    source->Ack(); source->Ack(); assert(new_done == 1);
    source->ArtifactDocumentCleanup(1,false); assert(source->operations_.Finish(1)); DrainTasks();
  }
  {
    auto source = CefRefPtr<Source>(new Source()); source->Admit(1); const auto original = source->operations_.Get(1); int done = 0;
    source->ArtifactDocuments(1,"(()=>true)()",[&done](bool,const std::string&) { ++done; }); source->Ack();
    auto late = std::move(source->calls.front()); source->calls.pop_front();
    source->sessions_.erase("oopif"); source->frame_states_["beta"].context = 0;
    DrainTasks(); assert(source->retries == 1 && done == 0 && source->artifact_document_work_.empty());
    const auto current = source->operations_.Get(1);
    assert(current && current->generation == original->generation && current->deadline_ms == 150);
    assert(source->operations_.IsExpired(1,original->generation,150));
    // The removed renderer must not receive cleanup. Its document was destroyed;
    // the still-live same-process document is restored in its original context.
    assert(source->fires.size() == 1 && source->fires.front().session.empty());
    source->sessions_["replacement"] = "beta";
    source->frame_states_["beta"] = {"beta","main","replacement","new-loader",9,10};
    ++source->artifact_attempts_[1]; source->ArtifactDocuments(1,"(()=>true)()",[&done](bool ok,const std::string&) { assert(ok); ++done; });
    const auto count = source->calls.size(); late.callback(true,"{\"result\":{\"value\":true}}"); assert(source->calls.size() == count);
    source->Ack(); assert(source->calls.front().session == "replacement" && source->calls.front().params->GetInt("contextId") == 10);
    source->Ack(); assert(done == 1);
    source->ArtifactDocumentCleanup(1,false); assert(source->operations_.Finish(1)); DrainTasks();
  }
  {
    auto source = CefRefPtr<Source>(new Source()); source->Admit(1);
    source->frame_states_["alpha"].utility_context = 0; int done = 0;
    source->ArtifactDocuments(1,"(()=>true)()",[&done](bool,const std::string&) { ++done; });
    assert(source->calls.empty() && !scheduled.empty());
    source->ArtifactDocumentCleanup(1,false); assert(source->operations_.Finish(1));
    source->frame_states_["alpha"].utility_context = 4; DrainTasks();
    assert(done == 0 && source->calls.empty() && source->fires.empty() && source->artifact_document_work_.empty());
  }
  {
    auto source = CefRefPtr<Source>(new Source()); source->Admit(1); int done = 0;
    source->ArtifactDocuments(1,"(()=>true)()",[&done](bool,const std::string&) { ++done; });
    source->closing_page_ = true; source->ArtifactDocumentCleanup(1,true);
    assert(source->operations_.Finish(1)); source->Ack(); DrainTasks();
    assert(done == 0 && source->fires.empty() && source->artifact_document_work_.empty());
  }
  {
    auto source = CefRefPtr<Source>(new Source()); source->Admit(1); int done = 0;
    source->ArtifactDocuments(1,"(()=>true)()",[&done,raw = source.get()](bool ok,const std::string& error) {
      assert(!ok && error == "Native screenshot child-document preparation failed"); ++done; raw->ArtifactDocumentCleanup(1,false);
    });
    auto bad = std::move(source->calls.front()); source->calls.pop_front(); bad.callback(true,"{\"result\":{\"value\":false}}");
    assert(done == 1 && source->fires.size() == 1 && source->artifact_document_work_.empty());
    assert(source->operations_.Finish(1)); DrainTasks();
  }
  {
    auto source = CefRefPtr<Source>(new Source()); source->Admit(1); int done = 0;
    source->frame_states_["alpha"].context = 0; source->frame_states_["alpha"].utility_context = 0;
    source->frame_states_["alpha"].url.clear(); source->frame_states_["alpha"].milestones.clear();
    source->ArtifactDocuments(1,"(()=>true)()",[&done](bool ok,const std::string&) { assert(ok); ++done; });
    // An uncommitted child has no actual world to prepare. Native screenshot
    // readiness must not wait for a deliberately stalled subframe navigation.
    assert(source->calls.size() == 1 && source->calls.front().session == "oopif");
    assert(source->ArtifactDocumentContextCount() == 1); source->Ack(); assert(done == 1);
    source->ArtifactDocumentCleanup(1,false); assert(source->operations_.Finish(1)); DrainTasks();
  }
  {
    auto source = CefRefPtr<Source>(new Source()); source->Admit(1); int done = 0;
    source->frame_states_["alpha"].context = 0; source->frame_states_["alpha"].utility_context = 0;
    // The committed URL and milestone identify an existing document. Temporary
    // context recreation must still wait rather than omit its style/font work.
    source->ArtifactDocuments(1,"(()=>true)()",[&done](bool,const std::string&) { ++done; });
    assert(source->calls.empty() && !scheduled.empty()); source->ArtifactDocumentCleanup(1,false);
    assert(source->operations_.Finish(1)); DrainTasks(); assert(done == 0 && source->fires.empty());
  }
  std::puts("Native artifact document ownership, per-session restoration, cancellation, swapped-target retry and original deadline passed");
}
