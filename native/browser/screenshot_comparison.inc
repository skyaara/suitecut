// SPDX-License-Identifier: MIT
// Included inside Source. One original registry owner/deadline spans all real
// paint gates, ArtifactController captures and bounded CPU comparison jobs.
struct ScreenshotComparisonSample { uint64_t generation=0,sample=0; Dict config; bool painting=true; double deadline=0; };
std::map<int,ScreenshotComparisonSample> screenshot_comparison_samples_;
void ScreenshotComparisonCancel(int owner) {
  screenshot_comparison_samples_.erase(owner);
  engine_.screenshot_comparisons()->Cancel(page_id_,owner);
  engine_.screenshot_inputs()->CancelCommand(page_id_,owner);
}
suitecut::ScreenshotInputs::Owner ScreenshotInputOwner()const{return {network_context_id_,page_id_,renderer_views_};}
void ScreenshotInputPrepare(int owner,Dict message){
  const auto operation=operations_.Get(owner);if(!operation)return;
  const auto png=message->GetType("pngBytes"),config=message->GetType("configBytes");
  if(message->GetType("clientToken")!=VTYPE_STRING||message->GetType("file")!=VTYPE_BOOL||png!=VTYPE_INT||config!=VTYPE_INT||message->GetInt("pngBytes")<0||message->GetInt("configBytes")<=0){Complete(owner,false,"Invalid native screenshot input preparation");return;}
  std::string error;const auto ticket=engine_.screenshot_inputs()->Prepare(ScreenshotInputOwner(),message->GetString("clientToken"),message->GetBool("file"),size_t(message->GetInt("pngBytes")),size_t(message->GetInt("configBytes")),operation->deadline_ms,error);
  if(ticket.token.empty()){Complete(owner,false,error);return;}
  auto result=CefDictionaryValue::Create();result->SetString("token",ticket.token);result->SetInt("contextId",network_context_id_);result->SetInt("pageId",page_id_);Complete(owner,true,"",Json(result));
}
void ScreenshotInputCommit(int owner,Dict message){
  const auto operation=operations_.Get(owner);if(!operation)return;
  std::string json,error;const auto token=message->GetString("inputToken").ToString();
  if(!engine_.screenshot_inputs()->Inspect(ScreenshotInputOwner(),token,json,error)){Complete(owner,false,error);return;}
  const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto config=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr;
  if(!config){engine_.screenshot_inputs()->Cancel(ScreenshotInputOwner(),token);Complete(owner,false,"Invalid native screenshot assertion configuration JSON");return;}
  const auto generation=operation->generation;
  if(!engine_.screenshot_inputs()->Claim(ScreenshotInputOwner(),token,config->GetString("baselinePath"),owner,[self=CefRefPtr<Source>(this),owner,generation,config,token](std::shared_ptr<suitecut::ScreenshotInputs::Data> input,double deadline,const std::string&error){
    if(!self->operations_.IsCurrent(owner,generation)||self->closing_page_||!self->browser_)return;
    if(!error.empty()){self->Complete(owner,false,error);return;}
    if(!self->ScreenshotAssertionStart(owner,generation,deadline,token,config)){self->Complete(owner,false,"Native screenshot assertion admission rejected");return;}
    self->ScreenshotComparisonBegin(owner,config,std::move(input),deadline);
  },error))Complete(owner,false,error);
  WakeDeadlines();
}
void ScreenshotComparisonStart(int owner,Dict message) {
  const auto operation=operations_.Get(owner);if(operation)ScreenshotComparisonBegin(owner,message,nullptr,operation->deadline_ms);
}
void ScreenshotComparisonBegin(int owner,Dict message,std::shared_ptr<suitecut::ScreenshotInputs::Data> input,double original_deadline) {
  const auto operation=operations_.Get(owner);if(!operation)return;
  const auto generation=operation->generation;
  auto capture=
    [self=CefRefPtr<Source>(this),owner,generation,original_deadline](uint64_t sample,Dict config) {
      if(!self->operations_.IsCurrent(owner,generation)||self->closing_page_||!self->browser_)return;
      if(self->ScreenshotAssertionOwns(owner)){const auto entry=self->screenshot_assertion_retirement_.Get({self->page_id_,owner,generation});if(!entry||entry->frozen)return;}
      if(self->artifacts_)self->artifacts_->Cancel(owner);
      self->context_retries_.erase(owner);self->artifact_configs_.erase(owner);self->artifact_attempts_.erase(owner);
      self->screenshot_comparison_samples_[owner]={generation,sample,config,true,original_deadline};
      if(!CefPostTask(TID_UI,new Task([self,owner,generation,sample] {
        const auto pending=self->screenshot_comparison_samples_.find(owner);
        if(pending==self->screenshot_comparison_samples_.end()||pending->second.sample!=sample||!self->operations_.IsCurrent(owner,generation)||!self->browser_||self->closing_page_)return;
        self->browser_->GetHost()->Invalidate(PET_VIEW);
      })))self->engine_.screenshot_comparisons()->Captured(self->page_id_,owner,sample,false,"Native screenshot presentation task was rejected",nullptr);
    };
  auto reply=[self=CefRefPtr<Source>(this),owner,generation](bool ok,Dict result,const std::string& error) {
      if(!self->operations_.IsCurrent(owner,generation))return;
      if(self->ScreenshotAssertionOwns(owner)){self->ScreenshotAssertionFreeze({self->page_id_,owner,generation},ok,result,error);return;}
      self->Complete(owner,ok,error,result?Json(result):"{}");
    };
  const double deadline=original_deadline>0?original_deadline:std::numeric_limits<double>::infinity();
  if(input)engine_.screenshot_comparisons()->HandlePrepared(network_context_id_,page_id_,owner,message,std::move(input),deadline,std::move(capture),std::move(reply),ScreenshotAssertionOwns(owner)?ScreenshotAssertionObserver({page_id_,owner,generation}):nullptr);
  else engine_.screenshot_comparisons()->Handle(network_context_id_,page_id_,owner,message,deadline,std::move(capture),std::move(reply),ScreenshotAssertionOwns(owner)?ScreenshotAssertionObserver({page_id_,owner,generation}):nullptr);
  WakeDeadlines();
}
void ScreenshotComparisonPaint() {
  // Called only after a genuine accepted PET_VIEW paint. Tasks start capture
  // after OnPaint unwinds, rather than dispatching CDP from its callback.
  std::vector<int> ready;for(const auto&[owner,sample]:screenshot_comparison_samples_)if(sample.painting)ready.push_back(owner);
  for(const int owner:ready){
    const auto found=screenshot_comparison_samples_.find(owner);if(found==screenshot_comparison_samples_.end()||!found->second.painting)continue;
    found->second.painting=false;const auto generation=found->second.generation,sample=found->second.sample;
    if(!CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this),owner,generation,sample] {
      const auto current=self->screenshot_comparison_samples_.find(owner);
      if(current==self->screenshot_comparison_samples_.end()||current->second.sample!=sample||!self->operations_.IsCurrent(owner,generation)||self->closing_page_||!self->browser_)return;
      const auto operation=self->operations_.Get(owner);if(!operation)return;
      const double deadline=current->second.deadline;
      if(deadline>0&&Now()>=deadline){self->engine_.screenshot_comparisons()->Deadline(self->page_id_,owner);return;}
      auto config=self->ScreenshotAssertionOwns(owner)?current->second.config:current->second.config->Copy(false);config->SetInt("timeoutMs",deadline>0?int(std::min(120000.0,std::max(1.0,deadline-Now()))):0);
      self->artifact_configs_[owner]=config;self->context_retries_[owner]=[self,owner]{self->ArtifactStart(owner);};self->ArtifactStart(owner);
    })))engine_.screenshot_comparisons()->Captured(page_id_,owner,sample,false,"Native screenshot capture task was rejected",nullptr);
  }
}
bool ScreenshotComparisonReceipt(int owner,bool ok,const std::string& error,const std::string& json) {
  const auto found=screenshot_comparison_samples_.find(owner);if(found==screenshot_comparison_samples_.end())return false;
  const auto sample=found->second.sample,generation=found->second.generation;
  if(!operations_.IsCurrent(owner,generation))return true;
  // Real capture/restoration has completed. Cancel its finished queue record
  // before a decoder reply may request a new actual presentation opportunity.
  if(artifacts_)artifacts_->Cancel(owner);
  auto parsed=ok?CefParseJSON(json,JSON_PARSER_RFC):nullptr;
  auto result=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr;
  engine_.screenshot_comparisons()->Captured(page_id_,owner,sample,ok,error,result);
  return true;
}
