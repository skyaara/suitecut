// SPDX-License-Identifier: MIT
// Include inside Source; action_pauses.h is included before the Source definition.
  SuiteCutActionPauseRegistry action_pauses_;
  void PauseActionBeforeStable(int id, std::uint64_t generation,
      const std::string& selector, const std::string& action,
      const std::string& text, Dict config) {
    if (!operations_.IsCurrent(id, generation)) return;
    const auto frame = FrameId(config);
    const auto found = frame_states_.find(frame);
    if (found == frame_states_.end()) { Complete(id, false, "Native frame detached"); return; }
    const auto context = found->second.context;
    const auto loader = found->second.loader;
    const auto client_token = config->GetString("clientToken").ToString();
    auto resumed = config->Copy(false);
    resumed->SetBool("resumeBeforeStable", true);
    auto token = action_pauses_.Pause(id, generation, client_token,
      [this,id,generation,selector,action,text,resumed,frame,context,loader] {
        if (!operations_.IsCurrent(id, generation)) return;
        const auto current = frame_states_.find(frame);
        if (current == frame_states_.end()) { Complete(id, false, "Native frame detached"); return; }
        if (current->second.context != context || current->second.loader != loader) {
          auto& log = locator_logs_[id];
          if (log.size() < 4096) {
            log += "\n - element was detached from the DOM, retrying";
            if (log.size() > 4096) log.resize(4096);
          }
        }
        LocatorAction(id,generation,selector,action,text,resumed);
      });
    if (!token) { Complete(id, false, "Native action pause capacity or identity is invalid"); return; }
    auto header = Header("actionstage");
    header->SetInt("id", id);
    header->SetDouble("generation", static_cast<double>(generation));
    header->SetDouble("token", static_cast<double>(*token));
    header->SetString("clientToken", client_token);
    header->SetString("stage", "beforeStable");
    SendPacket(header);
  }
  static bool ActionHookInteger(Dict message, const char* key, std::uint64_t& value) {
    const auto type = message->GetType(key);
    if (type != VTYPE_INT && type != VTYPE_DOUBLE) return false;
    const double number = type == VTYPE_INT ? message->GetInt(key) : message->GetDouble(key);
    if (!std::isfinite(number) || number < 1 || std::floor(number) != number ||
        number > static_cast<double>(SuiteCutActionPauseRegistry::kMaxWireInteger)) return false;
    value = static_cast<std::uint64_t>(number); return true;
  }
  // Control traffic remains available when every ordinary operation is paused.
  void ContinueAction(int reply_id, Dict message) {
    std::uint64_t generation = 0, token = 0;
    if (message->GetType("target") != VTYPE_INT || message->GetInt("target") < 1 ||
        message->GetInt("target") == reply_id ||
        message->GetType("clientToken") != VTYPE_STRING ||
        !ActionHookInteger(message,"generation",generation) ||
        !ActionHookInteger(message,"token",token)) {
      writer_.Reply(reply_id,false,"Invalid native action continuation"); return;
    }
    const int target = message->GetInt("target");
    if (!operations_.IsCurrent(target,generation)) {
      writer_.Reply(reply_id,false,"Native action continuation is stale"); return;
    }
    if (operations_.IsExpired(target,generation,Now())) {
      writer_.Reply(reply_id,false,"Native action continuation deadline expired");
      Complete(target,false,"Native action Timeout exceeded"); return;
    }
    auto resume = action_pauses_.Take(target,generation,token,message->GetString("clientToken"));
    if (!resume) { writer_.Reply(reply_id,false,"Native action continuation is stale"); return; }
    writer_.Reply(reply_id,true);
    (*resume)();
  }
