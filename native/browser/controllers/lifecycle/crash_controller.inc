// SPDX-License-Identifier: MIT
// A crash observation owns a real event journal cursor and original native
// deadline even after this page's renderer has terminated.
  std::set<int> crash_wait_owners_;
  void ChildRendererCrash(const char* reason) {
    const auto sequence = ++event_sequence_;
    JournalEvent(sequence, "crash", "{}");
    std::vector<int> waiters;
    for (const auto& [id,event] : event_waiters_) if(event == "crash") waiters.push_back(id);
    for(const int id : waiters) Complete(id,true,"","{}");
    for(const int id : operations_.Ids()) if(!crash_wait_owners_.contains(id)) Complete(id,false,reason);
    auto header=Header("pagecrash");header->SetString("error",reason);header->SetDouble("sequence",static_cast<double>(sequence));SendPacket(header);
  }
