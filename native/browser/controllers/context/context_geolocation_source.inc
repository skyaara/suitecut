// Included inside Source. Native target enrollment and exact result ownership.
bool geolocation_ready_ = false, geolocation_started_ = false;
std::set<std::string> geolocation_sessions_;
std::string GeolocationKey(const std::string& session) const {
  return "page:" + std::to_string(page_id_) + ":" + session;
}
void GeolocationAttach(const std::string& session,
                       suitecut::NativeContextGeolocation::Result ready) {
  if (closing_page_ || !browser_ || geolocation_sessions_.contains(session)) {
    ready(false, "Native geolocation target is closed or already registered");
    return;
  }
  geolocation_sessions_.insert(session);
  const bool accepted = engine_.geolocation().Attach(
      engine_.contexts()->ContextId(page_id_), GeolocationKey(session),
      [self = CefRefPtr<Source>(this), session](const suitecut::NativeGeolocationValue& value,
                                                double requested_deadline,
                                                suitecut::NativeContextGeolocation::Result done) {
        if (self->closing_page_ || !self->browser_ ||
            (!session.empty() && !self->sessions_.contains(session))) {
          done(false, "Native geolocation target is closed");
          return;
        }
        const double now = Now(), deadline = requested_deadline > 0
                                                 ? std::min(requested_deadline, now + 30000)
                                                 : now + 30000;
        if (deadline <= now) {
          done(false, "Native operation timed out before geolocation submission");
          return;
        }
        const int id = self->AllocateDevToolsId();
        if (!id) {
          done(false, "Native geolocation command id unavailable");
          return;
        }
        auto params = CefDictionaryValue::Create();
        if (value.coordinates) {
          params->SetDouble("latitude", value.coordinates->latitude);
          params->SetDouble("longitude", value.coordinates->longitude);
          params->SetDouble("accuracy", value.coordinates->accuracy);
        }
        // After the native override, the macOS fallback (if this state needs it)
        // is in place before the update reports success.
        std::optional<suitecut::NativeGeolocationFallbackCoordinates> coordinates;
        if (value.coordinates)
          coordinates = suitecut::NativeGeolocationFallbackCoordinates{
              value.coordinates->latitude, value.coordinates->longitude,
              value.coordinates->accuracy};
        auto finish = [self, session, coordinates, done = std::move(done)](
                          bool ok, const std::string& json) mutable {
          if (!ok) {
            done(false, json);
            return;
          }
          self->geolocation_emulated_ = coordinates;
          self->GeolocationFallbackSync(session, std::move(done));
        };
        self->network_calls_.emplace(id, NetworkCall{session, std::move(finish), deadline});
        self->WakeDeadlines();
        if (!self->SubmitDevTools(id, "Emulation.setGeolocationOverride", params, session)) {
          const auto row = self->network_calls_.find(id);
          if (row != self->network_calls_.end()) {
            auto done = std::exchange(row->second.callback, nullptr);
            self->network_calls_.erase(row);
            done(false, "Native geolocation command submission failed");
          }
        }
      },
      // A session that joins after the page's own emulation (an out-of-process
      // frame) still takes the current fallback state before it runs.
      [self = CefRefPtr<Source>(this), session, ready](bool ok, const std::string& error) {
        if (!ok || self->closing_page_ || session.empty()) {
          ready(ok, error);
          return;
        }
        self->GeolocationFallbackSync(session, ready);
      });
  if (!accepted) {
    geolocation_sessions_.erase(session);
    ready(false, "Native geolocation target registration failed");
  }
}
void GeolocationStart() {
  if (geolocation_started_ || closing_page_) return;
  geolocation_started_ = true;
  GeolocationAttach("", [self = CefRefPtr<Source>(this)](bool ok, const std::string& error) {
    if (self->closing_page_) return;
    if (!ok) {
      self->SettingsFail("Native geolocation initialization failed: " + error);
      return;
    }
    self->geolocation_ready_ = true;
    self->SettingsCommit();
    self->MaybeReady();
  });
}
void GeolocationDetach(const std::string& session) {
  geolocation_fallback_scripts_.erase(session);
  if (geolocation_sessions_.erase(session)) engine_.geolocation().Detach(GeolocationKey(session));
}
void GeolocationClear() {
  const auto sessions = std::move(geolocation_sessions_);
  geolocation_sessions_.clear();
  geolocation_fallback_scripts_.clear();
  for (const auto& session : sessions) engine_.geolocation().Detach(GeolocationKey(session));
}

// macOS JavaScript fallback (context/context_geolocation_fallback.h). Each
// session holds at most one new-document script; every sync adds the current
// state with runImmediately, so existing realms update in place, then removes
// the previous script. An inactive state removes both. Syncs of one session
// run one at a time; requests made meanwhile share one follow-up run.
struct GeolocationFallbackRun {
  bool running = false, again = false;
  std::vector<suitecut::NativeContextGeolocation::Result> current, next;
};
// The position Chromium last emulated for this page, from the context state or
// the page's own emulation command (Emulation overrides are per WebContents).
std::optional<suitecut::NativeGeolocationFallbackCoordinates> geolocation_emulated_;
std::map<std::string, std::string> geolocation_fallback_scripts_;
std::map<std::string, GeolocationFallbackRun> geolocation_fallback_runs_;
suitecut::NativeGeolocationFallbackState GeolocationFallbackState() {
  const auto& coordinates = geolocation_emulated_;
  const auto origins = engine_.contexts()->PermissionOrigins(page_id_, "geolocation");
  // Ask the OS only when its answer can matter.
  const auto system = coordinates && !origins.empty() ? suitecut::NativeSystemLocationStatus()
                                                      : suitecut::NativeSystemLocation::kAllowed;
  return suitecut::NativeGeolocationFallbackDecide(system, coordinates, origins);
}
void GeolocationFallbackRefresh() { GeolocationFallbackSyncAll(nullptr); }
// The page's own emulation command changed the position: every geolocation
// session of this page takes the new state before the command completes.
void GeolocationFallbackSyncAll(suitecut::NativeContextGeolocation::Result done) {
  if (closing_page_) {
    if (done) done(false, "Native geolocation target is closed");
    return;
  }
  struct Join {
    size_t pending = 0;
    bool ok = true;
    std::string error;
    suitecut::NativeContextGeolocation::Result done;
  };
  const auto join = std::make_shared<Join>();
  join->done = std::move(done);
  const std::vector<std::string> sessions(geolocation_sessions_.begin(),
                                          geolocation_sessions_.end());
  join->pending = sessions.size() + 1;
  const auto arrive = [join](bool ok, const std::string& error) {
    if (!ok && join->ok) {
      join->ok = false;
      join->error = error;
    }
    if (--join->pending == 0 && join->done) std::exchange(join->done, nullptr)(join->ok, join->error);
  };
  for (const auto& session : sessions) GeolocationFallbackSync(session, arrive);
  arrive(true, "");
}
void GeolocationFallbackCommand(const std::string& session, const std::string& method,
                                Dict params, suitecut::NetworkControls::Result done) {
  const int id = closing_page_ || !browser_ ? 0 : AllocateDevToolsId();
  if (!id) {
    done(false, "Native geolocation fallback command unavailable or page closed");
    return;
  }
  network_calls_.emplace(id, NetworkCall{session, std::move(done), Now() + 30000});
  WakeDeadlines();
  if (!SubmitDevTools(id, method, params, session)) {
    const auto row = network_calls_.find(id);
    if (row != network_calls_.end()) {
      auto callback = std::exchange(row->second.callback, nullptr);
      network_calls_.erase(row);
      callback(false, "Native geolocation fallback command submission failed");
    }
  }
}
void GeolocationFallbackRemove(const std::string& session, const std::string& identifier) {
  if (identifier.empty()) return;
  auto params = CefDictionaryValue::Create();
  params->SetString("identifier", identifier);
  GeolocationFallbackCommand(session, "Page.removeScriptToEvaluateOnNewDocument", params,
                             [](bool, const std::string&) {});
}
void GeolocationFallbackSync(const std::string& session,
                             suitecut::NativeContextGeolocation::Result done) {
  auto& run = geolocation_fallback_runs_[session];
  if (run.running) {
    run.again = true;
    if (done) run.next.push_back(std::move(done));
    return;
  }
  if (done) run.current.push_back(std::move(done));
  run.running = true;
  const auto state = GeolocationFallbackState();
  if (!state.active && !geolocation_fallback_scripts_.contains(session)) {
    GeolocationFallbackFinish(session, true, "");
    return;
  }
  auto params = CefDictionaryValue::Create();
  params->SetString("source", suitecut::NativeGeolocationFallbackScript(
                                  suitecut::NativeGeolocationFallbackToken(),
                                  suitecut::NativeGeolocationFallbackStateJson(state)));
  params->SetBool("runImmediately", true);
  GeolocationFallbackCommand(
      session, "Page.addScriptToEvaluateOnNewDocument", params,
      [self = CefRefPtr<Source>(this), session, active = state.active](bool ok,
                                                                       const std::string& json) {
        if (!ok) {
          self->GeolocationFallbackFinish(session, false,
                                          "Native geolocation fallback installation failed: " +
                                              json);
          return;
        }
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
        auto result = parsed ? parsed->GetDictionary() : nullptr;
        const auto identifier = result ? result->GetString("identifier").ToString() : "";
        std::string previous;
        const auto installed = self->geolocation_fallback_scripts_.find(session);
        if (installed != self->geolocation_fallback_scripts_.end()) {
          previous = installed->second;
          self->geolocation_fallback_scripts_.erase(installed);
        }
        if (active && self->geolocation_sessions_.contains(session) && !identifier.empty())
          self->geolocation_fallback_scripts_[session] = identifier;
        else
          self->GeolocationFallbackRemove(session, identifier);
        self->GeolocationFallbackRemove(session, previous);
        self->GeolocationFallbackFinish(session, true, "");
      });
}
void GeolocationFallbackFinish(const std::string& session, bool ok, const std::string& error) {
  const auto found = geolocation_fallback_runs_.find(session);
  if (found == geolocation_fallback_runs_.end()) return;
  auto waiting = std::move(found->second.current);
  found->second.current.clear();
  found->second.running = false;
  const bool again = std::exchange(found->second.again, false);
  if (again) {
    found->second.current = std::move(found->second.next);
    found->second.next.clear();
  } else
    geolocation_fallback_runs_.erase(found);
  for (auto& callback : waiting) callback(ok, error);
  if (!again) return;
  if (!closing_page_) {
    GeolocationFallbackSync(session, nullptr);
    return;
  }
  const auto closed = geolocation_fallback_runs_.find(session);
  if (closed == geolocation_fallback_runs_.end()) return;
  auto pending = std::move(closed->second.current);
  geolocation_fallback_runs_.erase(closed);
  for (auto& callback : pending) callback(false, "Native geolocation target is closed");
}
