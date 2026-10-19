// SPDX-License-Identifier: MIT
// Included inside Source. Each descendant's real utility world owns its styles,
// animations and caret ledger; cleanup uses its original context and CDP session.
struct ArtifactDocumentResource {
  std::string frame, session, loader;
  int context = 0;
};
struct ArtifactDocumentWork {
  int owner = 0; uint64_t generation = 0, attempt = 0, pending_token = 0;
  std::string expression;
  SuiteCutArtifactController::Callback done;
  std::vector<std::string> frames;
  std::set<std::string> seen;
  std::vector<ArtifactDocumentResource> resources;
  size_t index = 0;
  bool pending = false;
};
std::map<int, std::shared_ptr<ArtifactDocumentWork>> artifact_document_work_;

bool ArtifactDocumentsCurrent(const std::shared_ptr<ArtifactDocumentWork>& work) const {
  const auto current = artifact_document_work_.find(work->owner);
  return operations_.IsCurrent(work->owner,work->generation) && current != artifact_document_work_.end() &&
      current->second == work && artifact_attempts_.contains(work->owner) && artifact_attempts_.at(work->owner) == work->attempt;
}
void ArtifactDocumentCleanup(int id, bool teardown) {
  const auto current = artifact_document_work_.find(id); if (current == artifact_document_work_.end()) return;
  const auto work = current->second; artifact_document_work_.erase(current);
  if (teardown || closing_page_ || !browser_) return;
  for (const auto& resource : work->resources) {
    if (!resource.session.empty() && !sessions_.contains(resource.session)) continue;
    auto params = CefDictionaryValue::Create(); params->SetInt("contextId",resource.context);
    params->SetString("expression",SuiteCutArtifactController::RestoreExpression(id)); params->SetBool("returnByValue",true);
    SendDevTools("Runtime.evaluate",params,resource.session);
  }
}
void ArtifactDocumentsFinish(const std::shared_ptr<ArtifactDocumentWork>& work, bool ok, const std::string& error) {
  if (!ArtifactDocumentsCurrent(work)) return;
  work->pending = false;
  operation_frames_[work->owner] = main_frame_id_; operation_sessions_[work->owner] = "";
  auto done = std::move(work->done); if (done) done(ok,error);
}
void ArtifactDocuments(int id, const std::string& expression, SuiteCutArtifactController::Callback done) {
  const auto operation = operations_.Get(id);
  if (!operation || !artifact_attempts_.contains(id)) return;
  if (artifact_document_work_.contains(id)) { done(false,"Native screenshot document preparation is already active"); return; }
  if (artifact_document_work_.size() >= 64) { done(false,"Native screenshot document preparation capacity exceeded"); return; }
  auto work = std::make_shared<ArtifactDocumentWork>(); work->owner = id; work->generation = operation->generation;
  work->attempt = artifact_attempts_.at(id); work->expression = expression; work->done = std::move(done);
  artifact_document_work_[id] = work; ArtifactDocumentsNext(work);
}
void ArtifactDocumentsNext(const std::shared_ptr<ArtifactDocumentWork>& work) {
  if (!ArtifactDocumentsCurrent(work)) return;
  // Reconcile genuine newly attached children before finishing the native tree
  // preparation. One attempt remains bounded even if the page creates frames.
  for (const auto& [id, frame] : frame_states_) {
    if (id == main_frame_id_ || !Descendant(id,main_frame_id_) || work->seen.contains(id)) continue;
    if (work->frames.size() >= 1024) { ArtifactDocumentsFinish(work,false,"Screenshot document limit exceeded"); return; }
    work->seen.insert(id); work->frames.push_back(id);
  }
  if (work->index == work->frames.size()) { ArtifactDocumentsFinish(work,true,""); return; }
  const auto id = work->frames[work->index]; const auto frame = frame_states_.find(id);
  if (frame == frame_states_.end() || !Descendant(id,main_frame_id_)) { ++work->index; ArtifactDocumentsNext(work); return; }
  // A provisional/stalled child navigation can have no current document realm.
  // Capture its current blank presentation; do not wait for the network commit.
  if (!frame->second.context && frame->second.url.empty() && !frame->second.milestones.contains("commit")) {
    ++work->index; ArtifactDocumentsNext(work); return;
  }
  EnsureUtilityWorld(id);
  if (!frame->second.context || !frame->second.utility_context ||
      (!frame->second.session.empty() && !sessions_.contains(frame->second.session))) {
    CefPostDelayedTask(TID_UI,new Task([self = CefRefPtr<Source>(this),work] { self->ArtifactDocumentsNext(work); }),10); return;
  }
  const auto resource = ArtifactDocumentResource{id,frame->second.session,frame->second.loader,frame->second.utility_context};
  // Record ownership before dispatch. Even an already canceled Runtime result
  // cannot leave its actual DOM effects without a cleanup request in that realm.
  if (!work->expression.empty()) {
    if (work->expression.size() > 64 * 1048576 / (work->resources.size() + 1)) {
      ArtifactDocumentsFinish(work,false,"Screenshot document preparation exceeds the 64 MiB script budget"); return;
    }
    work->resources.push_back(resource);
  }
  auto params = CefDictionaryValue::Create(); params->SetInt("contextId",resource.context);
  params->SetString("expression","(async()=>{" + work->expression + ";await document.fonts.ready;return true})()");
  params->SetBool("awaitPromise",true); params->SetBool("returnByValue",true);
  work->pending = true; const auto token = ++work->pending_token;
  operation_sessions_[work->owner] = resource.session;
  // Keep operation_frames_ on the page. A disappearing child restarts this
  // original page operation through the metadata watcher instead of closing it.
  operation_frames_[work->owner] = main_frame_id_;
  DevTools(work->owner,"Runtime.evaluate",params,[this,work,resource,token](bool ok,const std::string& json) {
    if (!ArtifactDocumentsCurrent(work) || !work->pending || token != work->pending_token) return;
    work->pending = false; operation_sessions_[work->owner] = ""; operation_frames_[work->owner] = main_frame_id_;
    const auto frame = frame_states_.find(resource.frame);
    if (frame == frame_states_.end() || frame->second.utility_context != resource.context || frame->second.session != resource.session || frame->second.loader != resource.loader) {
      RetryContext(work->owner); return;
    }
    auto parsed = CefParseJSON(json,JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
    auto remote = result ? result->GetDictionary("result") : nullptr;
    if (!ok || !remote || result->HasKey("exceptionDetails") || remote->GetType("value") != VTYPE_BOOL || !remote->GetBool("value")) {
      ArtifactDocumentsFinish(work,false,"Native screenshot child-document preparation failed"); return;
    }
    ++work->index; ArtifactDocumentsNext(work);
  });
  if (operations_.IsCurrent(work->owner,work->generation)) operation_sessions_[work->owner] = "";
  if (ArtifactDocumentsCurrent(work) && work->pending && token == work->pending_token) ArtifactDocumentsWatch(work,resource,token);
}
void ArtifactDocumentsWatch(const std::shared_ptr<ArtifactDocumentWork>& work, const ArtifactDocumentResource& resource, uint64_t token) {
  if (!ArtifactDocumentsCurrent(work) || !work->pending || token != work->pending_token) return;
  const auto frame = frame_states_.find(resource.frame);
  if (frame == frame_states_.end() || !frame->second.context || frame->second.utility_context != resource.context ||
      frame->second.session != resource.session || frame->second.loader != resource.loader ||
      (!resource.session.empty() && !sessions_.contains(resource.session))) { RetryContext(work->owner); return; }
  // A detached OOPIF can drop a pending CDP promise without returning a result.
  // Watch its native metadata while waiting, always under the original deadline.
  CefPostDelayedTask(TID_UI,new Task([self = CefRefPtr<Source>(this),work,resource,token] { self->ArtifactDocumentsWatch(work,resource,token); }),10);
}
size_t ArtifactDocumentContextCount() const {
  size_t count = 0; for (const auto& [owner,work] : artifact_document_work_) count += work->resources.size(); return count;
}
