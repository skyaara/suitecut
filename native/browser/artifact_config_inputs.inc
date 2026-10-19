// SPDX-License-Identifier: MIT
// Included inside Source. Only actual page artifacts can consume these native
// purpose-bound leases; no placeholder PNG or baseline file task is involved.
std::map<int,std::shared_ptr<suitecut::ScreenshotInputs::Data>> artifact_input_configs_;
size_t ArtifactInputReceiptCount()const{size_t result=0;for(const auto&[id,call]:devtools_)if(call.artifact_generation)++result;return result;}
suitecut::ScreenshotInputs::Purpose ArtifactInputPurpose(const std::string&kind)const{
  return kind=="screenshot"?suitecut::ScreenshotInputs::Purpose::Screenshot:suitecut::ScreenshotInputs::Purpose::PDF;
}
void ArtifactInputPrepare(int owner,Dict message){
  const auto operation=operations_.Get(owner);if(!operation)return;
  const auto kind=message->GetString("kind").ToString();
  if((kind!="screenshot"&&kind!="pdf")||message->GetType("clientToken")!=VTYPE_STRING||message->GetType("configBytes")!=VTYPE_INT||message->GetInt("configBytes")<=0||message->HasKey("file")||message->HasKey("pngBytes")){Complete(owner,false,"Invalid native artifact configuration preparation");return;}
  std::string error;
  const auto ticket=engine_.screenshot_inputs()->Prepare(ScreenshotInputOwner(),message->GetString("clientToken"),false,0,size_t(message->GetInt("configBytes")),operation->deadline_ms,error,ArtifactInputPurpose(kind));
  if(ticket.token.empty()){Complete(owner,false,error);return;}
  auto result=CefDictionaryValue::Create();result->SetString("token",ticket.token);result->SetInt("contextId",network_context_id_);result->SetInt("pageId",page_id_);Complete(owner,true,"",Json(result));
}
void ArtifactInputCommit(int owner,Dict message){
  const auto operation=operations_.Get(owner);if(!operation)return;
  const auto kind=message->GetString("kind").ToString();
  if(kind!="screenshot"&&kind!="pdf"){Complete(owner,false,"Invalid native artifact configuration kind");return;}
  const auto purpose=ArtifactInputPurpose(kind);const auto source=ScreenshotInputOwner();
  const auto token=message->GetString("inputToken").ToString();std::string json,error;std::shared_ptr<suitecut::ScreenshotInputs::Data> inspection;
  if(!engine_.screenshot_inputs()->Inspect(source,token,json,error,purpose,&inspection)){Complete(owner,false,error);return;}
  auto parsed=CefParseJSON(json,JSON_PARSER_RFC);std::string().swap(json);
  auto dictionary=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr;const auto config=dictionary?dictionary->Copy(false):nullptr;dictionary=nullptr;parsed=nullptr;
  if(!config||config->GetString("action").ToString()!=kind){engine_.screenshot_inputs()->Cancel(source,token);Complete(owner,false,"Invalid native artifact configuration JSON or purpose");return;}
  for(const char*field:{"id","command","pageId","frame","contextId","traceStack","inputToken"})if(config->HasKey(field)){engine_.screenshot_inputs()->Cancel(source,token);Complete(owner,false,"Native artifact configuration cannot replace its actual command owner");return;}
  const auto generation=operation->generation;
  if(!engine_.screenshot_inputs()->Claim(source,token,"",owner,[self=CefRefPtr<Source>(this),owner,generation,config](auto input,double deadline,const std::string&reason){
    if(!self->operations_.IsCurrent(owner,generation)||self->closing_page_||!self->browser_)return;
    if(!reason.empty()){self->Complete(owner,false,reason);return;}
    if(!input||!input->png.empty()||!input->admission||!self->operations_.BindOriginalDeadline(owner,generation,deadline)){self->Complete(owner,false,"Native artifact configuration lost its original lease");return;}
    if(deadline>0&&Now()>=deadline){self->Complete(owner,false,"Native artifact original deadline expired before rendering");return;}
    std::string().swap(input->config);
    self->artifact_input_configs_[owner]=std::move(input);
    self->artifact_configs_[owner]=config;
    self->artifact_configs_[owner]->SetInt("timeoutMs",deadline>0?int(std::max(1.0,std::min(120000.0,deadline-Now()))):0);
    self->context_retries_[owner]=[self,owner]{self->ArtifactStart(owner);};self->ArtifactStart(owner);self->WakeDeadlines();
  },error,purpose))Complete(owner,false,error);
}
