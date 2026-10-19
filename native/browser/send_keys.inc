// SPDX-License-Identifier: MIT
// Included inside Source. Reserve the actual native keyboard queue before focus
// or caret preparation so competing input cannot steal the prepared target.
void BeginSendKeys(int id, uint64_t generation, Dict config) {
  const int delay = config->GetInt("delay");
  if ((config->HasKey("delay") && config->GetType("delay") != VTYPE_INT) || delay < 0 || delay > 120000) {
    Complete(id, false, "Invalid native send-keys delay"); return;
  }
  if (input_work_.contains(id)) return;
  auto work = std::make_shared<InputWork>();
  work->owner = id; work->generation = generation;
  work->kind = "keyboard"; work->action = "prepareSendKeys"; work->standards_send_keys = true;
  work->message = config->Copy(false);
  work->text = config->GetString("value").ToString();
  input_work_.emplace(id, work);
  keyboard_queue_.push_back(id);
  StartInput("keyboard");
}
void SendKeysPrepared(int id, Dict state) {
  const auto found = input_work_.find(id);
  if (found == input_work_.end() || keyboard_input_ != id || found->second->action != "prepareSendKeys") {
    Complete(id, false, "Native send-keys physical owner is unavailable"); return;
  }
  auto work = found->second;
  work->busy = false; work->action = "type";
  work->message->SetBool("caretEnd", state->GetBool("caretEnd"));
  context_retries_.erase(id);
  ContinueInput(work);
}

// Logical cancellation cannot release a keyboard owner while its genuine DOM
// preparation is physically in flight. The retained CEF reply/session teardown
// retires the lease; the raw callback never resumes an expired operation.
std::map<int, size_t> send_keys_physical_;
size_t send_keys_physical_count_ = 0;
bool SendKeysPhysicalPending(int id) const {
  const auto found = send_keys_physical_.find(id);
  return found != send_keys_physical_.end() && found->second;
}
void SendKeysPhysicalFinished(int id) {
  const auto found = send_keys_physical_.find(id);
  if (found == send_keys_physical_.end()) return;
  --send_keys_physical_count_;
  if (--found->second == 0) send_keys_physical_.erase(found);
  if (!operations_.Contains(id)) CleanupInput(id);
}
