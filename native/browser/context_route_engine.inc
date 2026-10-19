// SPDX-License-Identifier: MIT
// Included inside Engine. The existing owner registry resolves real Sources
// and page-less brokers; no external command can select another context owner.
  suitecut::NativeContextRoutes& routes(){return *context_routes_;}
  void ContextRoute(int context,const std::string&owner,int page,Dict worker,Dict event){
    if(context<1||owner.empty()||owner.size()>128||!event||(event->GetString("event")!="route"&&event->GetString("event")!="routematch"))return;
    auto record=CefDictionaryValue::Create();record->SetInt("contextId",context);record->SetString("owner",owner);record->SetDictionary("network",event->Copy(false));
    if(worker)record->SetDictionary("worker",worker->Copy(false));else if(page>0)record->SetInt("pageId",page);else return;
    const auto json=Json(record);if(json.size()>1048576){HARPolicyFailure(owner,"Native context route packet exceeds 1 MiB; actual request owner must close");return;}
    writer_.Send(Header("contextroute"),std::vector<uint8_t>(json.begin(),json.end()));
  }
  void ContextRouteMutation(int context,const std::string&mutation,bool applied,const std::vector<std::string>&ids){
    if(context<1||mutation.empty()||mutation.size()>128||ids.size()>256)return;
    auto event=CefDictionaryValue::Create();event->SetInt("contextId",context);event->SetString("mutationId",mutation);event->SetString("phase",applied?"applied":"admitted");
    auto registrations=CefListValue::Create();for(size_t index=0;index<ids.size();++index)registrations->SetString(index,ids[index]);event->SetList("registrationIds",registrations);
    const auto json=Json(event);if(json.size()>65536){writer_.Error("Native context route mutation receipt exceeds 64 KiB");return;}
    writer_.Send(Header("contextroutemutation"),std::vector<uint8_t>(json.begin(),json.end()));
  }
  void HandleContextRoutes(int id,Dict params,double deadline,uint64_t generation){
    const int context=params->GetInt("contextId");
    auto reply=[this,alive=alive_,id,generation](bool ok,const std::string&error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,nullptr,error);};
    if(!contexts_->RequestContextForContext(context)){reply(false,"Actual context route owner is closed");return;}
    const auto action=params->GetString("action").ToString();bool accepted=false;
    if(action=="register"&&params->GetType("registrationId")==VTYPE_STRING&&params->GetType("url")==VTYPE_STRING&&params->GetType("times")==VTYPE_INT&&(!params->HasKey("hostMatcher")||params->GetType("hostMatcher")==VTYPE_BOOL)){
      auto route=suitecut::NativeContextRouteRegistration::Create(params->GetString("registrationId"),params->GetString("url"),params->GetInt("times"),params->GetBool("hostMatcher"));
      if(route)accepted=context_routes_->Install(id,context,std::move(route),deadline,reply);
    }else if(action=="remove"){
      auto values=params->GetList("registrationIds");std::vector<std::string>ids;bool valid=values&&values->GetSize()<=128;
      if(valid)for(size_t index=0;index<values->GetSize();++index){if(values->GetType(index)!=VTYPE_STRING){valid=false;break;}ids.push_back(values->GetString(index));}
      const bool has_behavior=params->HasKey("behavior");const auto behavior=params->GetString("behavior").ToString();
      if(has_behavior&&(params->GetType("behavior")!=VTYPE_STRING||(behavior!="default"&&behavior!="wait"&&behavior!="ignoreErrors")))valid=false;
      if(valid&&(behavior=="wait"||behavior=="ignoreErrors")){
        const auto mutation=params->GetString("mutationId").ToString();
        if(!ids.empty()||params->GetType("mutationId")!=VTYPE_STRING||mutation.empty()||mutation.size()>128||mutation.find('\0')!=std::string::npos)valid=false;
        if(valid)accepted=context_unroute_all_->Remove(id,context,generation,behavior=="wait",deadline,
          [this,alive=alive_,id,generation,context,mutation](bool applied,const std::vector<std::string>&selected){if(*alive&&operations_.IsCurrent(id,generation))ContextRouteMutation(context,mutation,applied,selected);},reply);
      }else if(valid){if(ids.empty())accepted=context_unroute_all_->Remove(id,context,generation,false,deadline,{},reply);else accepted=context_routes_->Remove(id,context,ids,deadline,reply);}
    }
    if(!accepted)reply(false,"Invalid, pending or excessive native context route policy");
  }
