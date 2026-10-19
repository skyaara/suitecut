// SPDX-License-Identifier: MIT
// Include inside Source after action_hooks.inc (validated wire integers).
  SuiteCutEventPredicateController event_predicates_;
  void PollEventPredicate(int id,std::uint64_t generation) {
    if(!operations_.IsCurrent(id,generation))return;
    if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native event predicate Timeout exceeded");return;}
    if(websocket_predicates_.contains(id)){PollWebSocketPredicate(id,generation);return;}
    if(event_predicates_.Pending(id,generation))return;
    const auto cursor=event_predicates_.Cursor(id,generation);if(!cursor)return;
    if(!event_history_.empty()&&*cursor<event_history_.front().sequence-1){Complete(id,false,"Native event journal cursor expired during host predicate");return;}
    for(const auto& record:event_history_) {
      const auto result=event_predicates_.Offer(id,generation,record.sequence,record.event,record.json);
      if(result==SuiteCutEventPredicateController::Status::PayloadCapacity){Complete(id,false,"Native event predicate payload capacity exceeded");return;}
      if(result!=SuiteCutEventPredicateController::Status::CandidateReady)continue;
      const auto candidate=event_predicates_.Pending(id,generation);if(!candidate)return;
      auto header=Header("eventpredicate");header->SetInt("id",id);header->SetDouble("generation",static_cast<double>(generation));
      header->SetDouble("token",static_cast<double>(candidate->token));header->SetDouble("sequence",static_cast<double>(candidate->sequence));
      header->SetString("clientToken",candidate->client_token);header->SetString("event",candidate->event);
      std::string public_json=candidate->json;
      if((candidate->event=="console"||candidate->event=="pageerror")){const auto value=CefParseJSON(candidate->json,JSON_PARSER_RFC);public_json.clear();if(!value||!suitecut::DevToolsLosslessJSON(value,public_json,1048576)){Complete(id,false,"Native runtime event predicate output limit exceeded");return;}}
      SendPacket(header,std::vector<uint8_t>(public_json.begin(),public_json.end()));return;
    }
  }
  void PumpEventPredicates(){for(const auto scope:event_predicates_.Active())PollEventPredicate(scope.owner,scope.generation);}
  void StartEventPredicate(int id,std::uint64_t generation,Dict config) {
    const auto event=config->GetString("event").ToString();
    if(event!="console"&&event!="pageerror"&&event!="crash"&&event!="frameattached"&&event!="framenavigated"&&event!="framedetached"&&event!="dialog"&&event!="dialogclosed"&&event!="filechooser"&&event!="download"&&event!="request"&&event!="response"&&event!="requestfinished"&&event!="requestfailed"&&event!="worker"){Complete(id,false,"Unsupported native event predicate");return;}
    std::uint64_t after=event_sequence_;
    if(config->HasKey("afterSequence")) {
      const auto type=config->GetType("afterSequence");const double value=type==VTYPE_INT?config->GetInt("afterSequence"):config->GetDouble("afterSequence");
      if((type!=VTYPE_INT&&type!=VTYPE_DOUBLE)||!std::isfinite(value)||value<0||std::floor(value)!=value||value>static_cast<double>(event_sequence_)){Complete(id,false,"Invalid native event predicate cursor");return;}
      after=static_cast<std::uint64_t>(value);
    }
    if(config->GetType("clientToken")!=VTYPE_STRING||!event_predicates_.Start(id,generation,config->GetString("clientToken"),event,after)){Complete(id,false,"Native event predicate capacity or identity is invalid");return;}
    if(event=="crash")crash_wait_owners_.insert(id);
    PollEventPredicate(id,generation);
  }
  void ContinueEventPredicate(int reply_id,Dict message) {
    std::uint64_t generation=0,token=0;
    if(message->GetType("target")!=VTYPE_INT||message->GetInt("target")<1||message->GetInt("target")==reply_id||message->GetType("matches")!=VTYPE_BOOL||message->GetType("clientToken")!=VTYPE_STRING||!ActionHookInteger(message,"generation",generation)||!ActionHookInteger(message,"token",token)){writer_.Reply(reply_id,false,"Invalid native event predicate continuation");return;}
    const int target=message->GetInt("target");
    if(!operations_.IsCurrent(target,generation)){writer_.Reply(reply_id,false,"Native event predicate continuation is stale");return;}
    const auto offered=event_predicates_.Pending(target,generation);const bool console=offered&&(offered->event=="console"||offered->event=="pageerror");
    std::string json;
    const auto decision=event_predicates_.Answer(target,generation,token,message->GetString("clientToken"),message->GetBool("matches"),json);
    if(decision==SuiteCutEventPredicateController::Decision::Stale){writer_.Reply(reply_id,false,"Native event predicate continuation is stale");return;}
    writer_.Reply(reply_id,true);
    if(operations_.IsExpired(target,generation,Now())){Complete(target,false,"Native event predicate Timeout exceeded");return;}
    if(decision==SuiteCutEventPredicateController::Decision::Matched){if(console){const auto value=CefParseJSON(json,JSON_PARSER_RFC);json.clear();if(!value||!suitecut::DevToolsLosslessJSON(value,json,1048576)){Complete(target,false,"Native runtime event predicate output limit exceeded");return;}}Complete(target,true,"",json);}
    else PollEventPredicate(target,generation);
  }
