// SPDX-License-Identifier: MIT
// Existing native device queue, shared by all keyboard/pointer intents.
  void StartInput(const std::string& kind) {
    int& active = kind == "mouse" ? mouse_input_ : keyboard_input_;
    auto& queue = kind == "mouse" ? mouse_queue_ : keyboard_queue_;
    if (active || !browser_ || NativeDropBusy()) return;
    while (!queue.empty()) {
      const int id = queue.front(); queue.pop_front();
      const auto found = input_work_.find(id); if (found == input_work_.end() || !operations_.Contains(id)) continue;
      active = id; found->second->start_x = mouse_x_; found->second->start_y = mouse_y_;
      ArmActionNavigation(found->second);
      InputStep(found->second); break;
    }
  }
