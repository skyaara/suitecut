// SPDX-License-Identifier: MIT
// Included after Source is complete. Control-plane chunks/watch/cleanup remain
// usable after a public page closes; actual leases retain their native owner.
void Engine::ScreenshotInputControl(int id,Dict params,uint64_t generation){
  const auto page_type=params->GetType("pageId");const int page=params->GetInt("pageId");
  const auto action=params->GetString("action").ToString();
  if(page_type!=VTYPE_INT||page<=0){CompleteContext(id,false,nullptr,"Invalid native screenshot input page");return;}
  if(action=="discard"){
    if(params->GetType("clientToken")!=VTYPE_STRING||params->GetString("clientToken").empty()||params->GetString("clientToken").length()>128){CompleteContext(id,false,nullptr,"Invalid native screenshot input cleanup token");return;}
    screenshot_inputs_->DiscardClient(page,params->GetString("clientToken"));CompleteContext(id,true);return;
  }
  const auto source=sources_.find(page);
  if(source==sources_.end()||source->second->closing_page_||!source->second->browser_||source->second->renderer_failed_||params->GetType("contextId")!=VTYPE_INT||params->GetInt("contextId")!=contexts_->ContextId(page)||params->GetType("token")!=VTYPE_STRING){CompleteContext(id,false,nullptr,"Native screenshot input source/context is unavailable");return;}
  const auto owner=source->second->ScreenshotInputOwner();const auto token=params->GetString("token").ToString();std::string error;
  if(action=="watch"){
    if(!screenshot_inputs_->Watch(owner,token,id,[this,alive=alive_,id,generation](bool committed,const std::string&error){if(*alive&&operations_.IsCurrent(id,generation)){auto result=CefDictionaryValue::Create();result->SetBool("committed",committed);CompleteContext(id,error.empty(),result,error);}},error))CompleteContext(id,false,nullptr,error);
    return;
  }
  if(action!="append"||params->GetType("section")!=VTYPE_STRING||(params->GetString("section")!="config"&&params->GetString("section")!="png")||params->GetType("offset")!=VTYPE_INT||params->GetInt("offset")<0||params->GetType("data")!=VTYPE_STRING){CompleteContext(id,false,nullptr,"Invalid native screenshot input chunk");return;}
  const auto text=params->GetString("data").ToString();
  if(text.empty()||text.size()>32768||text.size()%4){CompleteContext(id,false,nullptr,"Native screenshot input chunk exceeds24KiB");return;}
  const auto binary=CefBase64Decode(text);
  if(!binary||!binary->GetSize()||binary->GetSize()>24576){CompleteContext(id,false,nullptr,"Invalid native screenshot input chunk encoding");return;}
  std::vector<uint8_t>bytes(binary->GetSize());
  if(binary->GetData(bytes.data(),bytes.size(),0)!=bytes.size()||CefBase64Encode(bytes.data(),bytes.size()).ToString()!=text){CompleteContext(id,false,nullptr,"Noncanonical native screenshot input chunk encoding");return;}
  const bool accepted=screenshot_inputs_->Append(owner,token,params->GetString("section")=="config",size_t(params->GetInt("offset")),bytes,error);CompleteContext(id,accepted,nullptr,error);
}

size_t Engine::ArtifactInputReceipts()const{size_t result=0;for(const auto&[page,source]:sources_)result+=source->ArtifactInputReceiptCount();return result;}
