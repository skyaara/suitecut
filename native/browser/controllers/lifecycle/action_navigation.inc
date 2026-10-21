// Authored native input/navigation barrier. Included inside Source.
struct ActionNavigationBaseline { uint64_t sequence; std::string loader; };
struct ActionNavigationIntent { uint64_t sequence; std::string loader; bool canceled = false; std::string url = ""; };
struct ActionNavigationWait {
  uint64_t generation = 0;
  bool barrier = false;
  std::map<std::string,ActionNavigationBaseline> baseline;
  std::map<std::string,ActionNavigationIntent> intents;
};
std::map<int,ActionNavigationWait> action_navigations_;
void ArmActionNavigation(const std::shared_ptr<InputWork>& work) {
  if (!work->message->GetBool("waitForNavigation") || (work->action != "click"&&work->action!="tap")) return;
  ActionNavigationWait wait; wait.generation = work->generation;
  for (const auto& [frame,state] : frame_states_) wait.baseline.emplace(frame,ActionNavigationBaseline{state.navigation,state.loader});
  action_navigations_.emplace(work->owner,std::move(wait));
}
bool ActionNavigationEvent(const std::string& method, Dict data) {
  if (method != "Page.frameRequestedNavigation" && method != "Page.frameStartedNavigating" &&
      method != "Page.frameStoppedLoading") return false;
  const auto frame = data->GetString("frameId").ToString();
  if (method == "Page.frameStoppedLoading") {
    for (auto& [owner,wait] : action_navigations_) {
      const auto intent = wait.intents.find(frame); const auto state = frame_states_.find(frame);
      if (intent != wait.intents.end() && state != frame_states_.end() &&
          state->second.navigation <= intent->second.sequence && state->second.loader == intent->second.loader)
        intent->second.canceled = true;
    }
    CheckActionNavigations(); return true;
  }
  if (method == "Page.frameRequestedNavigation" && data->GetString("disposition") != "currentTab") return true;
  const auto wait = action_navigations_.find(mouse_input_);
  if (wait == action_navigations_.end()) return true;
  const auto baseline = wait->second.baseline.find(frame);
  if (baseline == wait->second.baseline.end() || wait->second.intents.size() >= 64) return true;
  auto [intent,inserted]=wait->second.intents.try_emplace(frame,ActionNavigationIntent{baseline->second.sequence,baseline->second.loader});
  (void)inserted;if(data->GetType("url")==VTYPE_STRING)intent->second.url=data->GetString("url");
  // A child click can navigate its parent, which then removes the child's old
  // document. Transfer cancellation ownership to the actual navigation frame.
  operation_frames_[mouse_input_] = frame;
  return true;
}
void FinishActionInput(const std::shared_ptr<InputWork>& work) {
  const auto wait = action_navigations_.find(work->owner);
  if (wait == action_navigations_.end()) { Complete(work->owner,true); return; }
  work->busy = true;
  operation_sessions_[work->owner] = "";
  auto params = CefDictionaryValue::Create(); params->SetString("expression","0"); params->SetBool("returnByValue",true);
  // An acknowledged renderer round trip drains synchronous click handlers and
  // their navigation intent. The native operation retains its original deadline.
  DevTools(work->owner,"Runtime.evaluate",params,[this,work](bool ok,const std::string&) {
    const auto wait = action_navigations_.find(work->owner);
    if (wait == action_navigations_.end() || !operations_.IsCurrent(work->owner,work->generation)) return;
    if (!ok) { Complete(work->owner,false,"Native action renderer barrier failed"); return; }
    wait->second.barrier = true; CleanupInput(work->owner); CheckActionNavigations();
  });
}
void CheckActionNavigations() {
  std::vector<std::pair<int,std::string>> ready;
  for (const auto& [owner,wait] : action_navigations_) {
    if (!wait.barrier || !operations_.IsCurrent(owner,wait.generation)) continue;
    bool done = true; std::string error;
    for (const auto& [frame,intent] : wait.intents) {
      if (intent.canceled) continue;
      const auto state = frame_states_.find(frame);
      if (state == frame_states_.end()) { error = "Native action navigation frame detached"; break; }
      if (state->second.navigation <= intent.sequence || !state->second.milestones.contains("commit")) { done = false; break; }
      for (const auto& [key,document] : documents_)
        if (document.document && document.frame == frame && document.loader == state->second.loader && !document.error.empty()) error = document.error;
    }
    if (done || !error.empty()) ready.emplace_back(owner,error);
  }
  for (const auto& [owner,error] : ready) Complete(owner,error.empty(),error);
}
