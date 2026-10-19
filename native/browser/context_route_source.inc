// SPDX-License-Identifier: MIT
// Included inside the actual Source. This gate precedes public READY and does
// not require an SDK Page to exist before a genuine intercepted first request.
  void NativeStartContextRoutes(){
    if(closing_page_||!browser_)return;
    if(!engine_.routes().Attach(engine_.contexts()->ContextId(page_id_),"page:"+std::to_string(page_id_),
      [self=CefRefPtr<Source>(this)](suitecut::NativeContextRoutes::Mutation mutation,suitecut::NativeContextRoutes::Route route,double deadline,suitecut::NativeContextRoutes::Result done){
        if(self->closing_page_||!self->network_){done(false,"Actual Source context route owner is closed");return;}
        if(mutation==suitecut::NativeContextRoutes::Mutation::Install)self->network_->BindContextRoute(std::move(route),std::move(done),deadline);
        else self->network_->UnbindContextRoutePolicy(route,std::move(done),deadline);
      },[self=CefRefPtr<Source>(this)](bool ok,const std::string&error){if(self->closing_page_)return;if(!ok){self->NetworkFatal(error);return;}self->network_policy_ready_=true;self->SettingsAuthenticate();}
    ))NetworkFatal("Native Source context route capacity exceeded");
  }
