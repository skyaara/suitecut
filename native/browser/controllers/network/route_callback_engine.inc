// Insert in Engine::Command before page lookup; no page must remain open to
// acknowledge a physically settled author callback. NetworkControls supplies
// the shared global lease table through route_callback_leases.h.
if (command == "routeCallback") {
  const auto parse_nonce = [&message](const char* name, uint64_t& value) {
    if(message->GetType(name)!=VTYPE_STRING)return false;
    const auto text=message->GetString(name).ToString();
    if(text.empty() || text.size()>20)return false;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && value>0;
  };
  uint64_t scope=0,token=0;
  const auto invocation=message->GetString("invocation").ToString();
  const auto action=message->GetString("action").ToString();
  if(!parse_nonce("scope",scope) || !parse_nonce("token",token) || invocation.empty() || invocation.size()>128 || (action!="start" && action!="finish")) {
    writer_.Reply(id,false,"Invalid native route callback lease acknowledgment");return;
  }
  auto& leases=suitecut::GlobalRouteCallbackLeases();
  const bool ok=action=="start"?leases.Start(token,scope,invocation):leases.Finish(token,scope,invocation);
  writer_.Reply(id,ok,ok?"":"Native route callback lease is stale or has a different owner");return;
}
