// SPDX-License-Identifier: MIT
// Included inside Source. Arguments retain their original Runtime identities.
  SuiteCutConsoleJournal console_journal_, page_error_journal_;
  struct ConsoleRawArgument { Dict value; std::string frame,session; int context=0; std::size_t bytes=0; bool claimed=false; std::uint64_t sequence=0; bool delivered=false; };
  std::map<int,ConsoleRawArgument> console_arguments_;
  std::map<int,std::set<int>> console_materializations_;
  std::size_t console_argument_bytes_=0;
  std::uint64_t console_delivered_=0;
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
  void ReleaseConsoleRemote(const ConsoleRawArgument& argument) {
    if(argument.value->GetType("objectId")!=VTYPE_STRING||!browser_)return;
    auto release=CefDictionaryValue::Create();release->SetString("objectId",argument.value->GetString("objectId"));
    SendDevTools("Runtime.releaseObject",release,argument.session);
  }
  void DropConsoleArgument(int handle,bool release=true) {
    if(WorkerConsoleDrop(handle,release))return;
    const auto found=console_arguments_.find(handle);if(found==console_arguments_.end())return;
    if(release)ReleaseConsoleRemote(found->second);
    console_argument_bytes_-=found->second.bytes;console_arguments_.erase(found);
  }
  void ClearConsoleRawArguments(bool include_claimed=false) {
    WorkerConsoleClear(include_claimed);
    std::vector<int> removed;
    for(const auto& [handle,argument]:console_arguments_){
      if(argument.claimed&&!include_claimed)continue;
      ReleaseConsoleRemote(argument);
      const auto retained=handles_.find(handle);
      if(retained!=handles_.end()&&browser_){auto release=CefDictionaryValue::Create();release->SetString("objectGroup",retained->second.group);SendDevTools("Runtime.releaseObjectGroup",release,argument.session);}
      handles_.erase(handle);
      removed.push_back(handle);
    }
    for(const int handle:removed)DropConsoleArgument(handle,false);
  }
  void ReleaseEvictedConsoleArguments() {
    WorkerConsoleEvicted();
    const auto oldest=console_journal_.Oldest();std::vector<int> removed;
    for(const auto& [handle,argument]:console_arguments_)if(!argument.claimed&&argument.delivered&&(!oldest||argument.sequence<oldest))removed.push_back(handle);
    for(const int handle:removed){DropConsoleArgument(handle);handles_.erase(handle);}
  }
  void ConsoleFrameRemoved(const std::string& frame) {
    std::vector<int> removed;for(const auto& [handle,argument]:console_arguments_)if(argument.frame==frame)removed.push_back(handle);
    for(const auto handle:removed)DropConsoleArgument(handle);
  }
  void CancelConsoleMaterialization(int owner) {
    const auto pending=console_materializations_.find(owner);if(pending==console_materializations_.end())return;
    for(const int handle:pending->second) {
      const auto found=handles_.find(handle);if(found==handles_.end()||!browser_)continue;
      auto release=CefDictionaryValue::Create();release->SetString("objectGroup",found->second.group);
      SendDevTools("Runtime.releaseObjectGroup",release,found->second.session);
    }
    console_materializations_.erase(pending);
  }
  Dict RetainConsoleArgument(Dict argument,const std::string& session,int context) {
    if(worker_sessions_.contains(session))return WorkerConsoleRetain(argument,session,context);
    if(!argument||handles_.size()+pending_handle_groups_.size()>=1024||next_handle_==std::numeric_limits<int>::max())return nullptr;
    const auto bytes=Json(argument).size();if(bytes>1048576||console_argument_bytes_+bytes>4194304)return nullptr;
    const auto frame=std::find_if(frame_states_.begin(),frame_states_.end(),[&](const auto& item){return item.second.session==session&&(item.second.context==context||item.second.utility_context==context);});
    if(frame==frame_states_.end())return nullptr;
    const int handle=++next_handle_;const std::string group="suitecut-handle-"+std::to_string(handle);
    const bool node=argument->GetString("subtype")=="node";
    handles_.emplace(handle,ObjectHandle{"",group,frame->first,context,session,node?argument->GetString("objectId").ToString():"",frame->second.context==context?frame->second.context_identity:frame->second.utility_identity,native_pristine_renderer_epoch_});
    console_arguments_.emplace(handle,ConsoleRawArgument{argument->Copy(false),frame->first,session,context,bytes,false,event_sequence_+1,false});console_argument_bytes_+=bytes;
    auto metadata=CefDictionaryValue::Create();metadata->SetInt("handle",handle);metadata->SetString("guid",HandleGuid(handle));
    suitecut::DevToolsSetString(metadata,"preview",suitecut::DevToolsTruncateWTF8(ConsolePreview(argument),8192));metadata->SetBool("node",node);metadata->SetString("frame",frame->first);return metadata;
  }
  void MaterializeConsoleArgument(int owner,int handle,std::function<void()> done) {
    const auto raw=console_arguments_.find(handle);const auto stored=handles_.find(handle);
    if(raw==console_arguments_.end()){done();return;}
    if(stored==handles_.end()){Complete(owner,false,MissingHandle(handle));return;}
    raw->second.claimed=true;
    const auto frame=frame_states_.find(raw->second.frame);
    if(frame==frame_states_.end()||(frame->second.context!=raw->second.context&&frame->second.utility_context!=raw->second.context)){Complete(owner,false,"Native console argument execution context was destroyed");return;}
    const auto argument=raw->second;const auto group=stored->second.group;
    auto release=CefDictionaryValue::Create();release->SetString("objectGroup",group);SendDevTools("Runtime.releaseObjectGroup",release,argument.session);
    auto params=CefDictionaryValue::Create();params->SetInt("executionContextId",argument.context);params->SetString("objectGroup",group);
    params->SetString("functionDeclaration","function(value){return {value};}");
    auto values=CefListValue::Create();auto value=CefDictionaryValue::Create();
    if(argument.value->HasKey("objectId"))value->SetString("objectId",argument.value->GetString("objectId"));
    else if(argument.value->HasKey("unserializableValue"))value->SetString("unserializableValue",argument.value->GetString("unserializableValue"));
    else if(argument.value->GetString("type")=="string"&&argument.value->GetType("value")==VTYPE_DICTIONARY){std::u16string units;if(!suitecut::DevToolsReadTag(argument.value->GetValue("value"),units)){Complete(owner,false,"Invalid native console string sidecar");return;}params->SetString("functionDeclaration","function(){return {value:"+suitecut::DevToolsQuoteUnits(units)+"};}");}
    else if(argument.value->HasKey("value"))value->SetValue("value",argument.value->GetValue("value"));
    values->SetDictionary(0,value);params->SetList("arguments",values);
    operation_sessions_[owner]=argument.session;console_materializations_[owner].insert(handle);
    DevTools(owner,"Runtime.callFunctionOn",params,[this,owner,handle,argument,done=std::move(done)](bool ok,const std::string& json) mutable {
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
      const auto remote=result?result->GetDictionary("result"):nullptr;
      const auto stored=handles_.find(handle);
      if(!ok||!remote||result->HasKey("exceptionDetails")||stored==handles_.end()||remote->GetType("objectId")!=VTYPE_STRING){Complete(owner,false,"Native console argument adoption failed");return;}
      stored->second.object_id=remote->GetString("objectId");
      auto finish=[this,owner,handle,done=std::move(done)]() mutable {
        DropConsoleArgument(handle);const auto pending=console_materializations_.find(owner);
        if(pending!=console_materializations_.end()){pending->second.erase(handle);if(pending->second.empty())console_materializations_.erase(pending);}
        done();
      };
      if(argument.value->GetString("subtype")!="node"){finish();return;}
      auto properties=CefDictionaryValue::Create();properties->SetString("objectId",stored->second.object_id);properties->SetBool("ownProperties",true);
      DevTools(owner,"Runtime.getProperties",properties,[this,owner,handle,finish](bool ok,const std::string& json) mutable {
        const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
        const auto fields=result?result->GetList("result"):nullptr;const auto stored=handles_.find(handle);
        if(ok&&fields&&stored!=handles_.end())for(std::size_t i=0;i<fields->GetSize();++i){const auto field=fields->GetDictionary(i);const auto value=field?field->GetDictionary("value"):nullptr;if(field&&field->GetString("name")=="value"&&value&&value->GetType("objectId")==VTYPE_STRING){stored->second.node_id=value->GetString("objectId");finish();return;}}
        Complete(owner,false,"Native console DOM argument identity was lost");
      },std::nullopt,false,1048576,true);
    });
  }
  bool MaterializeConsoleArguments(int owner,Dict message,std::function<void()> done) {
    const auto ids=message->GetList("handles");if(!ids)return false;
    for(std::size_t index=0;index<ids->GetSize();++index)if(ids->GetType(index)==VTYPE_INT&&console_arguments_.contains(ids->GetInt(index))){MaterializeConsoleArgument(owner,ids->GetInt(index),std::move(done));return true;}
    return false;
  }
  void ConsoleEvent(const std::string& type,const std::string& text,Dict location,double timestamp=-1,CefRefPtr<CefListValue> arguments=nullptr,const std::string& session="",int context=0) {
    if(!ready_)return;
    if(text.size()>1000000||type.size()>64){Error("Native console message limit exceeded");return;}
    auto info=CefDictionaryValue::Create();info->SetString("type",type);suitecut::DevToolsSetString(info,"text",text);
    info->SetDouble("sequence",static_cast<double>(event_sequence_+1));WorkerConsoleMetadata(info,session);
    info->SetDouble("timestamp",timestamp>=0&&std::isfinite(timestamp)?timestamp:ConsoleWallTime());
    auto source=CefDictionaryValue::Create();const auto url=location?suitecut::DevToolsString(location->GetValue("url")):"";
    if(url.size()>8192){Error("Native console source limit exceeded");return;}
    const int line=location?std::max(0,location->GetInt("lineNumber")):0;
    suitecut::DevToolsSetString(source,"url",url);source->SetInt("line",line);source->SetInt("lineNumber",line);source->SetInt("columnNumber",location?std::max(0,location->GetInt("columnNumber")):0);info->SetDictionary("location",source);
    auto metadata=CefListValue::Create();std::vector<int> retained;
    if(arguments&&arguments->GetSize()>64)info->SetString("argumentsError","Native console argument capacity exceeded");
    else if(arguments)for(std::size_t index=0;index<arguments->GetSize();++index) {
      const auto argument=arguments->GetDictionary(index);auto handle=RetainConsoleArgument(argument,session,context);
      if(!handle){info->SetString("argumentsError","Native console argument retention capacity or context is unavailable");break;}
      retained.push_back(handle->GetInt("handle"));metadata->SetDictionary(index,handle);
    }
    if(info->HasKey("argumentsError")) {
      for(const int handle:retained){DropConsoleArgument(handle);handles_.erase(handle);}metadata=CefListValue::Create();
      if(arguments)for(std::size_t i=retained.size();i<arguments->GetSize();++i){auto value=arguments->GetDictionary(i);if(value&&value->GetType("objectId")==VTYPE_STRING){auto release=CefDictionaryValue::Create();release->SetString("objectId",value->GetString("objectId"));SendDevTools("Runtime.releaseObject",release,session);}}
    }
    info->SetList("arguments",metadata);const auto retained_json=Json(info);auto typed=CefValue::Create();typed->SetDictionary(info->Copy(false));std::string json;
    if(!suitecut::DevToolsLosslessJSON(typed,json,1048576)){for(const int handle:retained){DropConsoleArgument(handle);handles_.erase(handle);}Error("Native console payload limit exceeded");return;}
    if(json.size()>1048576){for(const int handle:retained){DropConsoleArgument(handle);handles_.erase(handle);}Error("Native console payload limit exceeded");return;}
    NativeTraceConsole(info,arguments);
    const auto sequence=++event_sequence_;console_journal_.Push(sequence,retained_json);ReleaseEvictedConsoleArguments();
    auto header=Header("console");header->SetDouble("sequence",static_cast<double>(sequence));JournalEvent(sequence,"console",retained_json);
    SendPacket(header,std::vector<uint8_t>(json.begin(),json.end()));
    engine_.ContextConsole(page_id_,info);
    std::vector<int> waiters;for(const auto& [id,event]:event_waiters_)if(event=="console")waiters.push_back(id);
    for(const int id:waiters)Complete(id,true,"",json);
  }
  void ConsoleException(Dict data,const std::string& session="") {
    const auto raw=data->GetDictionary("exceptionDetails");if(!raw)return;
    const auto details=raw->Copy(false);const int context=details->GetInt("executionContextId");
    if(context>0){const auto frame=std::find_if(frame_states_.begin(),frame_states_.end(),[&](const auto& item){return item.second.session==session&&(item.second.context==context||item.second.utility_context==context);});if(frame!=frame_states_.end())details->SetDictionary("__suitecutPageErrorFrame",FrameJson(frame->second));}
    PageErrors().Start(details,session);WakeDeadlines();
  }
  void ConsoleNavigation(){console_journal_.Navigation(event_sequence_);page_error_journal_.Navigation(event_sequence_);}
  void ClaimConsoleArguments(int reply_id,Dict message) {
    const auto ids=message->GetList("handles");
    if(!ids||ids->GetSize()>64){writer_.Reply(reply_id,false,"Invalid native console argument claim");return;}
    for(std::size_t index=0;index<ids->GetSize();++index)if(ids->GetType(index)!=VTYPE_INT||ids->GetInt(index)<1){writer_.Reply(reply_id,false,"Invalid native console argument claim");return;}
    for(std::size_t index=0;index<ids->GetSize();++index){const auto found=console_arguments_.find(ids->GetInt(index));if(found!=console_arguments_.end())found->second.claimed=true;WorkerConsoleClaim(ids->GetInt(index));}
    writer_.Reply(reply_id,true);
  }
  void AcknowledgeConsoleDelivery(int reply_id,Dict message) {
    const auto type=message->GetType("sequence");const auto sequence=type==VTYPE_INT?double(message->GetInt("sequence")):message->GetDouble("sequence");
    if((type!=VTYPE_INT&&type!=VTYPE_DOUBLE)||!std::isfinite(sequence)||sequence<1||sequence>double(event_sequence_)||std::floor(sequence)!=sequence){writer_.Reply(reply_id,false,"Invalid native console delivery cursor");return;}
    console_delivered_=std::max(console_delivered_,std::uint64_t(sequence));
    for(auto& [handle,argument]:console_arguments_)if(argument.sequence<=console_delivered_)argument.delivered=true;
    WorkerConsoleDelivered(console_delivered_);ReleaseEvictedConsoleArguments();writer_.Reply(reply_id,true);
  }
  void ConsoleCommand(int owner,Dict message) {
    const auto action=message->GetString("action").ToString();
    if(action=="clear"){console_journal_.Clear();ClearConsoleRawArguments();Complete(owner,true);return;}
    if(action!="list"&&action!="errors"){Complete(owner,false,"Unknown native console action");return;}
    const auto filter=message->GetString("filter").ToString();
    if(filter!="all"&&filter!="since-navigation"){Complete(owner,false,"Unknown native console journal filter");return;}
    auto list=CefListValue::Create();const auto records=(action=="errors"?page_error_journal_:console_journal_).Read(filter=="all");
    for(std::size_t index=0;index<records.size();++index){const auto value=CefParseJSON(records[index],JSON_PARSER_RFC);if(value)list->SetValue(index,value);}
    auto value=CefValue::Create();value->SetList(list);std::string public_json;if(!suitecut::DevToolsLosslessJSON(value,public_json,8*1048576)){Complete(owner,false,"Native console journal output limit exceeded");return;}Complete(owner,true,"",public_json);
  }
