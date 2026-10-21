// Included inside Source. Native target enrollment and exact result ownership.
bool geolocation_ready_=false,geolocation_started_=false;
std::set<std::string>geolocation_sessions_;
std::string GeolocationKey(const std::string&session)const{return "page:"+std::to_string(page_id_)+":"+session;}
void GeolocationAttach(const std::string&session,suitecut::NativeContextGeolocation::Result ready){
  if(closing_page_||!browser_||geolocation_sessions_.contains(session)){ready(false,"Native geolocation target is closed or already registered");return;}
  geolocation_sessions_.insert(session);
  const bool accepted=engine_.geolocation().Attach(engine_.contexts()->ContextId(page_id_),GeolocationKey(session),
    [self=CefRefPtr<Source>(this),session](const suitecut::NativeGeolocationValue&value,double requested_deadline,suitecut::NativeContextGeolocation::Result done){
      if(self->closing_page_||!self->browser_||(!session.empty()&&!self->sessions_.contains(session))){done(false,"Native geolocation target is closed");return;}
      const double now=Now(),deadline=requested_deadline>0?std::min(requested_deadline,now+30000):now+30000;
      if(deadline<=now){done(false,"Native operation timed out before geolocation submission");return;}
      const int id=self->AllocateDevToolsId();if(!id||self->network_calls_.size()>=1024){done(false,"Native geolocation command capacity exceeded");return;}
      auto params=CefDictionaryValue::Create();if(value.coordinates){params->SetDouble("latitude",value.coordinates->latitude);params->SetDouble("longitude",value.coordinates->longitude);params->SetDouble("accuracy",value.coordinates->accuracy);}
      self->network_calls_.emplace(id,NetworkCall{session,std::move(done),deadline});self->WakeDeadlines();
      if(!self->SubmitDevTools(id,"Emulation.setGeolocationOverride",params,session)){const auto row=self->network_calls_.find(id);if(row!=self->network_calls_.end()){auto done=std::move(row->second.callback);self->network_calls_.erase(row);done(false,"Native geolocation command submission failed");}}
    },ready);
  if(!accepted){geolocation_sessions_.erase(session);ready(false,"Native geolocation target capacity exceeded");}
}
void GeolocationStart(){
  if(geolocation_started_||closing_page_)return;
  geolocation_started_=true;
  GeolocationAttach("",[self=CefRefPtr<Source>(this)](bool ok,const std::string&error){if(self->closing_page_)return;if(!ok){self->SettingsFail("Native geolocation initialization failed: "+error);return;}self->geolocation_ready_=true;self->SettingsCommit();self->MaybeReady();});
}
void GeolocationDetach(const std::string&session){if(geolocation_sessions_.erase(session))engine_.geolocation().Detach(GeolocationKey(session));}
void GeolocationClear(){const auto sessions=std::move(geolocation_sessions_);geolocation_sessions_.clear();for(const auto&session:sessions)engine_.geolocation().Detach(GeolocationKey(session));}
