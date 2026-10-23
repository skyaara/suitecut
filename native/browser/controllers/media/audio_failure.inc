// Audio errors may arrive on the capture thread. Serialize their ownership
// decision with page close on UI; closing_page_ and context state are UI-owned.
auto Source::OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString& message) -> void {
  audio_valid_ = false;
  const auto detail = suitecut::DevToolsTruncateWTF8(message.ToString(), 2048);
  auto report = [self = CefRefPtr<Source>(this), alive = engine_.Alive(), detail] {
    if (!*alive || self->closing_page_ || self->stopped_) return;
    const auto contexts = self->engine_.contexts();
    // A pending, cancelable beforeunload dialog is not a committed close.
    const bool committed = contexts->PageClosePending(self->page_id_) &&
                           !contexts->BeforeUnloadCloseToken(self->page_id_);
    if (committed) return;
    self->Error(("Native audio capture failed: " + detail).c_str());
  };
  if (CefCurrentlyOn(TID_UI)) {
    report();
  } else if (!CefPostTask(TID_UI, new Task(std::move(report))) && !stopped_) {
    // UI is shutting down. Error() only queues a packet on the locked writer,
    // so report directly without the UI-owned close checks. A source whose
    // reader already stopped is closing deliberately; an audio error then is
    // part of teardown, not a capture failure.
    Error(("Native audio capture failed: " + detail + " (UI dispatch rejected)").c_str());
  }
}
