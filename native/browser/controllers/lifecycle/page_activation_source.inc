// SPDX-License-Identifier: MIT
// Included in Source. The existing retained DevTools lane owns the physical
// request until its actual ACK/target retirement, including after cancellation.
void NativeBringToFront(int id,uint64_t generation){
  if(!operations_.IsCurrent(id,generation))return;
  if(!browser_||closing_page_){Complete(id,false,"Native page is closed");return;}
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native page activation timed out at its original deadline");return;}
  DevTools(id,"Page.bringToFront",CefDictionaryValue::Create(),
    [this,id,generation](bool ok,const std::string& json){
      if(!operations_.IsCurrent(id,generation))return;
      if(!browser_||closing_page_){Complete(id,false,"Native page is closed");return;}
      if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native page activation timed out at its original deadline");return;}
      const auto value=CefParseJSON(json,JSON_PARSER_RFC);const auto result=value?value->GetDictionary():nullptr;
      if(!ok){
        const auto error=result&&result->GetType("message")==VTYPE_STRING?result->GetString("message").ToString():"Native page activation failed";
        Complete(id,false,error);return;
      }
      if(!result){Complete(id,false,"Invalid native page activation acknowledgement");return;}
      Complete(id,true,"","{}");
    },std::string{},true,8192);
}
