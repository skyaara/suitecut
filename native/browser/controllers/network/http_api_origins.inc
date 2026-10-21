// SPDX-License-Identifier: MIT
// Called only after common storage semantic validation, on the exact original
// initializeStorage owner. Acceptance is provisional until real restore ACK.
bool SuiteCutHTTPControls::ValidateInternalStorage(int owner,int browser_context,Dict state,double deadline,std::string&error){
  if(!state||!ValidateInternalCookies(owner,browser_context,state->GetList("cookies"),deadline,error))return false;
  const auto command=scope_commands_.find(owner);
  if(command==scope_commands_.end()||!command->second.initialize){error="Native API constructor origins lost their initialization owner";return false;}
  const auto context=contexts_.find(command->second.context);
  if(context==contexts_.end()||!context->second.owned||!context->second.initializing||context->second.closing||context->second.browser_context!=browser_context){error="Native API constructor origin context is unavailable";return false;}
  if(command->second.origins){error="Native API constructor origins already have an acceptance receipt";return false;}
  const auto origins=state->GetList("origins");if(!origins||origins->GetSize()>1024){error="Invalid native API constructor origins";return false;}
  const double creation=context->second.creation_deadline;deadline=creation&&deadline?std::min(creation,deadline):creation?creation:deadline;
  const suitecut::APIOriginsSnapshot::Clock clock{hooks_.now,deadline};
  auto pending=std::make_shared<suitecut::APIOriginsSnapshot>(origins_budget_);
  for(size_t i=0;i<origins->GetSize();++i){
    const auto origin=origins->GetDictionary(i);if(!origin){error="Invalid native API constructor origin";return false;}
    std::string json;
    if(origin->GetType("stateJSON")==VTYPE_STRING)json=origin->GetString("stateJSON").ToString();
    else{auto value=CefValue::Create();value->SetDictionary(origin->Copy(false));json=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();}
    if(!pending->AddValidated(origin->GetString("origin").ToString(),json,clock,error))return false;
  }
  if(!suitecut::APIOriginsSnapshot::Current(clock,error))return false;
  command->second.origins=std::move(pending);return true;
}
bool SuiteCutHTTPControls::PublishOrigins(int context_id,Dict result,bool indexed_db,bool opfs,double deadline,std::string&error){
  const auto found=contexts_.find(context_id);const suitecut::APIOriginsSnapshot::Clock clock{hooks_.now,deadline};
  if(found==contexts_.end()||found->second.closing||found->second.initializing||!found->second.owned||!result){error="Native API constructor origins are unavailable";return false;}
  if(!suitecut::APIOriginsSnapshot::Current(clock,error))return false;
  auto output=CefListValue::Create();const auto snapshot=found->second.origins;
  // Bound the serialized publication before accumulating another origin. CEF
  // UTF16 values and bounded serialization scratch are not PMR-accounted heap.
  auto base=result->Copy(false);base->Remove("origins");auto base_value=CefValue::Create();base_value->SetDictionary(base);
  size_t bytes=CefWriteJSON(base_value,JSON_WRITER_DEFAULT).ToString().size();
  constexpr size_t overhead=sizeof(",\"origins\":[]")-1;
  if(bytes>8*1048576-overhead){error="Native API storage publication exceeds 8 MiB";return false;}bytes+=overhead;
  if(snapshot){
    for(size_t i=0;i<snapshot->Size();++i){
      std::pmr::string json(snapshot->Resource());if(!snapshot->Project(i,indexed_db,opfs,clock,json,error))return false;
      auto origin=CefDictionaryValue::Create();origin->SetString("origin",std::string(snapshot->At(i).origin));origin->SetString("stateJSON",std::string(json));
      auto value=CefValue::Create();value->SetDictionary(origin->Copy(false));const size_t amount=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString().size()+(i?1:0);
      if(amount>8*1048576-bytes){error="Native API origins publication capacity exceeded";return false;}bytes+=amount;
      output->SetDictionary(output->GetSize(),origin);
    }
  }
  result->SetList("origins",output);
  return suitecut::APIOriginsSnapshot::Current(clock,error);
}
