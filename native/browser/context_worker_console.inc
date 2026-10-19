// SPDX-License-Identifier: MIT
// Included in the actual context broker; all references are worker Runtime objects.
  static double ConsoleWallTime() {
    return std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
  }
  std::string ConsolePreview(Dict remote,int depth=0) {
    if(!remote||depth>3)return "";
    const auto type=remote->GetString("type").ToString(),subtype=remote->GetString("subtype").ToString();
    if(type=="string"&&(remote->GetType("value")==VTYPE_STRING||remote->GetType("value")==VTYPE_DICTIONARY))return suitecut::DevToolsString(remote->GetValue("value"));
    if(remote->HasKey("value"))return CefWriteJSON(remote->GetValue("value"),JSON_WRITER_DEFAULT).ToString();
    if(remote->HasKey("unserializableValue"))return remote->GetString("unserializableValue");
    if(subtype=="null")return "null";
    const auto description=suitecut::DevToolsString(remote->GetValue("description"));
    if(remote->GetString("className")=="DedicatedWorkerGlobalScope"||remote->GetString("className")=="SharedWorkerGlobalScope"||remote->GetString("className")=="ServiceWorkerGlobalScope")return description;
    if(subtype=="error"||remote->GetString("className")=="Window"||remote->GetString("className")=="Promise")return description;
    auto preview=remote->GetDictionary("preview");
    auto properties=preview?preview->GetList("properties"):nullptr;
    if(properties&&(type=="object"||subtype=="array")) {
      const bool array=subtype=="array";
      if(array) {
        std::vector<SuiteCutConsoleArrayProperty> items;
        for(std::size_t i=0;i<std::min(properties->GetSize(),std::size_t(32));++i) {
          const auto property=properties->GetDictionary(i);if(!property)continue;
          auto child=property->GetDictionary("valuePreview");
          const auto rendered=child?ConsolePreview(child,depth+1):property->GetString("type")=="function"?"Function":suitecut::DevToolsString(property->GetValue("value"));
          items.push_back({suitecut::DevToolsString(property->GetValue("name")),rendered});
        }
        const auto text=SuiteCutFormatConsoleArray(description,items,preview->GetBool("overflow")||properties->GetSize()>32);return suitecut::DevToolsTruncateWTF8(text,text.size());
      }
      std::string text="{";
      for(std::size_t i=0;i<std::min(properties->GetSize(),std::size_t(32));++i) {
        const auto property=properties->GetDictionary(i);if(!property)continue;
        if(i)text+=", ";
        if(!array)text+=suitecut::DevToolsString(property->GetValue("name"))+": ";
        auto child=property->GetDictionary("valuePreview");
        if(child)text+=ConsolePreview(child,depth+1);
        else if(property->GetString("type")=="function")text+="Function";
        else text+=suitecut::DevToolsString(property->GetValue("value"));
        if(text.size()>65536){text=suitecut::DevToolsTruncateWTF8(text,65536);break;}
      }
      if(preview->GetBool("overflow"))text+=", …";
      return text+(array?"]":"}");
    }
    return description.empty()?type:description;
  }

SuiteCutConsoleJournal console_journal_;
void NativeWorkerInitialRequestSession(const std::string&,const std::string&,const std::string&,const std::string&) {} // Broker has no document requests.
void ContextConsoleEvent(Dict data,const std::string& session) {
  if(!worker_sessions_.contains(session))return;
  if(!WorkerEventAvailable())return;
  const auto original_sequence=event_sequence_+1;
  auto args=data->GetList("args");std::string text;
  if(args)for(size_t i=0;i<args->GetSize();++i){if(i)text+=" ";text+=ConsolePreview(args->GetDictionary(i));if(text.size()>1000000)break;}
  const auto type=data->GetString("type").ToString();if(text.size()>1000000||type.size()>64){Error("Native context worker console payload exceeds limit");return;}
  auto info=CefDictionaryValue::Create();info->SetString("type",type);suitecut::DevToolsSetString(info,"text",text);info->SetDouble("sequence",double(event_sequence_+1));WorkerConsoleMetadata(info,session);
  const auto timestamp=data->GetType("timestamp")==VTYPE_INT?double(data->GetInt("timestamp")):data->GetDouble("timestamp");info->SetDouble("timestamp",std::isfinite(timestamp)&&timestamp>=0?timestamp:ConsoleWallTime());
  auto stack=data->GetDictionary("stackTrace");auto calls=stack?stack->GetList("callFrames"):nullptr;auto original=calls&&calls->GetSize()?calls->GetDictionary(0):nullptr;
  auto location=CefDictionaryValue::Create();const auto url=original?suitecut::DevToolsString(original->GetValue("url")):"";if(url.size()>8192){Error("Native context worker console source exceeds limit");return;}
  const int line=original?std::max(0,original->GetInt("lineNumber")):0;suitecut::DevToolsSetString(location,"url",url);location->SetInt("line",line);location->SetInt("lineNumber",line);location->SetInt("columnNumber",original?std::max(0,original->GetInt("columnNumber")):0);info->SetDictionary("location",location);
  auto metadata=CefListValue::Create();std::vector<int> retained;
  if(args&&args->GetSize()>64)info->SetString("argumentsError","Native context worker console argument capacity exceeded");
  else if(args)for(size_t i=0;i<args->GetSize();++i){auto value=WorkerConsoleRetain(args->GetDictionary(i),session,data->GetInt("executionContextId"));if(!value){info->SetString("argumentsError","Native context worker console argument retention unavailable");break;}retained.push_back(value->GetInt("handle"));metadata->SetDictionary(i,value);}
  if(info->HasKey("argumentsError")){for(int id:retained)WorkerConsoleDrop(id);metadata=CefListValue::Create();if(args)for(size_t i=retained.size();i<args->GetSize();++i){auto value=args->GetDictionary(i);if(value&&value->GetType("objectId")==VTYPE_STRING){auto release=CefDictionaryValue::Create();release->SetString("objectId",value->GetString("objectId"));SendDevTools("Runtime.releaseObject",release,session);}}}
  info->SetList("arguments",metadata);const auto json=Json(info);if(json.size()>1048576){for(int id:retained)WorkerConsoleDrop(id);Error("Native context worker console payload exceeds limit");return;}
  const auto admitted=WorkerEventSequence();
  if(!admitted||*admitted!=original_sequence){for(int id:retained)WorkerConsoleDrop(id);if(admitted)EventCursorFailed("Native context worker console sequence changed during capture");return;}
  const auto sequence=*admitted;console_journal_.Push(sequence,json);WorkerConsoleEvicted();JournalEvent(sequence,"console",json);
  if(auto owner=owner_.lock())owner->hooks_.console(context_,info);
}
void ContextConsoleControl(int owner,Dict config) {
  const auto action=config->GetString("action").ToString();
  if(action=="claimConsoleArguments"){auto ids=config->GetList("handles");if(!ids||ids->GetSize()>64){Complete(owner,false,"Invalid native context worker console claim");return;}for(size_t i=0;i<ids->GetSize();++i)if(ids->GetType(i)!=VTYPE_INT||ids->GetInt(i)<1){Complete(owner,false,"Invalid native context worker console claim");return;}for(size_t i=0;i<ids->GetSize();++i)WorkerConsoleClaim(ids->GetInt(i));Complete(owner,true);return;}
  if(action=="acknowledgeConsole"){const auto type=config->GetType("sequence");const auto sequence=type==VTYPE_INT?double(config->GetInt("sequence")):config->GetDouble("sequence");if((type!=VTYPE_INT&&type!=VTYPE_DOUBLE)||!std::isfinite(sequence)||sequence<1||sequence>double(event_sequence_)||std::floor(sequence)!=sequence){Complete(owner,false,"Invalid native context worker console delivery cursor");return;}WorkerConsoleDelivered(uint64_t(sequence));WorkerConsoleEvicted();Complete(owner,true);return;}
  if(action=="console"){const auto params=config->GetDictionary("params");if(!params){Complete(owner,false,"Invalid native context worker console command");return;}const auto operation=params->GetString("action").ToString();if(operation=="clear"){console_journal_.Clear();WorkerConsoleClear();Complete(owner,true);return;}if(operation!="list"){Complete(owner,false,"Unsupported native context worker console command");return;}auto list=CefListValue::Create();const auto records=console_journal_.Read(true);for(size_t i=0;i<records.size();++i)list->SetValue(i,CefParseJSON(records[i],JSON_PARSER_RFC));auto value=CefValue::Create();value->SetList(list);Complete(owner,true,"",CefWriteJSON(value,JSON_WRITER_DEFAULT));return;}
  if(action=="waitEventPredicate"){const auto operation=operations_.Get(owner);const auto event=config->GetString("event").ToString();const auto type=config->GetType("afterSequence");const double cursor=config->HasKey("afterSequence")?(type==VTYPE_INT?double(config->GetInt("afterSequence")):config->GetDouble("afterSequence")):double(event_sequence_);if(event!="console"||!operation||!std::isfinite(cursor)||cursor<0||std::floor(cursor)!=cursor||cursor>double(event_sequence_)||(config->HasKey("afterSequence")&&type!=VTYPE_INT&&type!=VTYPE_DOUBLE)||config->GetType("clientToken")!=VTYPE_STRING||!event_predicates_.Start(owner,operation->generation,config->GetString("clientToken"),event,uint64_t(cursor))){Complete(owner,false,"Invalid native context worker console predicate");return;}PollContextPredicate(owner,operation->generation);return;}
  if(action=="continueEventPredicate"){const int target=config->GetInt("target");auto number=[&](const char* key){const auto kind=config->GetType(key);const double value=kind==VTYPE_INT?double(config->GetInt(key)):config->GetDouble(key);return (kind==VTYPE_INT||kind==VTYPE_DOUBLE)&&std::isfinite(value)&&value>=1&&value<=9007199254740991.0&&std::floor(value)==value?uint64_t(value):uint64_t(0);};const auto generation=number("generation"),token=number("token");if(target<1||target==owner||!generation||!token||config->GetType("matches")!=VTYPE_BOOL||config->GetType("clientToken")!=VTYPE_STRING||!operations_.IsCurrent(target,generation)){Complete(owner,false,"Native context worker console predicate continuation is stale");return;}std::string json;const auto decision=event_predicates_.Answer(target,generation,token,config->GetString("clientToken"),config->GetBool("matches"),json);if(decision==SuiteCutEventPredicateController::Decision::Stale){Complete(owner,false,"Native context worker console predicate continuation is stale");return;}Complete(owner,true);if(operations_.IsExpired(target,generation,Now()))Complete(target,false,"Native context worker console predicate Timeout exceeded");else if(decision==SuiteCutEventPredicateController::Decision::Matched)Complete(target,true,"",json);else PollContextPredicate(target,generation);return;}
  Complete(owner,false,"Unsupported native context worker console control");
}
void PollContextPredicate(int id,uint64_t generation){
  if(!operations_.IsCurrent(id,generation))return;
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native context worker console predicate Timeout exceeded");return;}
  if(event_predicates_.Pending(id,generation))return;
  const auto cursor=event_predicates_.Cursor(id,generation);if(!cursor)return;
  if(!journal_.empty()&&*cursor<journal_.front().sequence-1){Complete(id,false,"Native context worker console journal cursor expired");return;}
  for(const auto& record:journal_){const auto status=event_predicates_.Offer(id,generation,record.sequence,record.event,record.json);if(status==SuiteCutEventPredicateController::Status::PayloadCapacity){Complete(id,false,"Native context worker console predicate payload capacity exceeded");return;}if(status!=SuiteCutEventPredicateController::Status::CandidateReady)continue;const auto candidate=event_predicates_.Pending(id,generation);if(!candidate)return;auto info=CefDictionaryValue::Create();info->SetInt("id",id);info->SetDouble("generation",double(generation));info->SetDouble("token",double(candidate->token));info->SetDouble("sequence",double(candidate->sequence));info->SetString("clientToken",candidate->client_token);info->SetString("event",candidate->event);info->SetValue("value",CefParseJSON(candidate->json,JSON_PARSER_RFC));if(auto parent=owner_.lock())parent->hooks_.predicate(context_,info);return;}
}
SuiteCutEventPredicateController event_predicates_;
