// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include "cookie_controls.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
using Dict=CefRefPtr<CefDictionaryValue>;
Dict Parse(const char* json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));
 struct Call{int owner,context;suitecut::CookieMethod method;Dict params;double deadline;suitecut::CookieReply reply;};std::deque<Call> calls;
 auto controls=std::make_shared<suitecut::CookieControls>([&](int owner,int context,suitecut::CookieMethod method,Dict params,double deadline,suitecut::CookieReply reply){calls.push_back({owner,context,method,params->Copy(false),deadline,std::move(reply)});});
 int complete=0;Dict output;auto done=[&](bool ok,Dict result,const std::string& error){assert(ok&&error.empty());++complete;output=result;};
 auto params=Parse(R"({"cookies":[{"name":"chip","value":"real","domain":"localhost","path":"/","secure":true,"expires":-1,"partitionKey":"https://127.0.0.1","partitionKeyHasCrossSiteAncestor":false}]})");
 controls->Handle(1,7,"addCookies",params,1234,done);assert(calls.size()==1&&controls->Physical()==1);auto call=std::move(calls.front());calls.pop_front();assert(call.method==suitecut::CookieMethod::Set&&call.context==7&&call.deadline==1234);auto cookie=call.params->GetList("cookies")->GetDictionary(0);assert(!cookie->HasKey("expires")&&!cookie->GetDictionary("partitionKey")->GetBool("hasCrossSiteAncestor"));call.reply(true,Parse("{}"),"");assert(complete==1&&controls->Physical()==0);
 auto raw=Parse(R"({"cookies":[{"name":"chip","value":"real","domain":"localhost","path":"/","secure":true,"expires":-1,"httpOnly":false,"sameSite":"None","partitionKey":{"topLevelSite":"https://127.0.0.1","hasCrossSiteAncestor":false}},{"name":"other","value":"plain","domain":"localhost","path":"/","secure":false,"expires":-1,"httpOnly":false}]})");
 controls->Handle(2,7,"cookies",Parse(R"({"urls":["https://localhost/"]})"),2345,done);call=std::move(calls.front());calls.pop_front();call.reply(true,raw,"");assert(output->GetList("cookies")->GetSize()==2);cookie=output->GetList("cookies")->GetDictionary(0);assert(cookie->GetString("partitionKey")=="https://127.0.0.1"&&!cookie->GetBool("partitionKeyHasCrossSiteAncestor"));
 controls->Handle(3,7,"clearCookies",Parse(R"({"filter":{"name":"chip"}})"),3456,done);call=std::move(calls.front());calls.pop_front();call.reply(true,raw,"");assert(calls.size()==1);call=std::move(calls.front());calls.pop_front();assert(call.method==suitecut::CookieMethod::Delete&&call.deadline==3456&&call.params->GetString("name")=="chip"&&!call.params->GetDictionary("partitionKey")->GetBool("hasCrossSiteAncestor"));call.reply(true,Parse("{}"),"");assert(controls->Physical()==0&&complete==3);
 // A canceled get must retain its physical admission and never fan out deletes.
 controls->Handle(4,7,"clearCookies",Parse(R"({"filter":{"domain":"localhost"}})"),4567,done);controls->Cancel(4);assert(controls->Physical()==1);call=std::move(calls.front());calls.pop_front();call.reply(true,raw,"");assert(calls.empty()&&controls->Physical()==0&&complete==3);
 int closed=0;for(int owner=1;owner<=64;++owner){controls->Handle(owner,7,"cookies",Parse("{}"),5000,[&](bool ok,Dict,const std::string& error){assert(!ok&&error=="Native cookie context closed");++closed;});}
 assert(controls->Physical()==64&&calls.size()==64);int rejected=0;controls->Handle(65,7,"cookies",Parse("{}"),5000,[&](bool ok,Dict,const std::string&){assert(!ok);++rejected;});assert(rejected==1);controls->Closed(7);assert(controls->Physical()==64&&closed==64);while(!calls.empty()){call=std::move(calls.front());calls.pop_front();call.reply(false,nullptr,"Actual channel close");}assert(controls->Physical()==0&&complete==3);
 // Public URL selection agrees with the actual native loopback cookie wire
 // policy, while external HTTP and domain/path boundaries remain restricted.
 for(const auto* host:{"localhost","127.0.0.1","[::1]"}){
   auto actual=raw->Copy(false);auto list=actual->GetList("cookies");list->SetSize(1);auto scoped=list->GetDictionary(0);scoped->SetString("domain",host);scoped->Remove("partitionKey");
   const auto select=[&](const std::string&url,size_t expected){auto input=CefDictionaryValue::Create();auto urls=CefListValue::Create();urls->SetString(0,url);input->SetList("urls",urls);controls->Handle(90,7,"cookies",input,6000,done);auto delivery=std::move(calls.front());calls.pop_front();delivery.reply(true,actual,"");assert(output->GetList("cookies")->GetSize()==expected);};
   select(std::string("http://")+host+"/",1);select("http://not-loopback.test/",0);
   scoped->SetString("domain","remote.test");select("http://remote.test/",0);select("https://remote.test/",1);select("https://remote.test.evil/",0);
   scoped->SetString("path","/scope");select("https://remote.test/scope/child",1);select("https://remote.test/scope-other",0);
 }
 std::string error;assert(!suitecut::CookieControls::Partition(Parse(R"({"partitionKey":"https://example.test","secure":false})"),error));assert(!suitecut::CookieControls::Partition(Parse(R"({"partitionKeyHasCrossSiteAncestor":false})"),error));
 // Actual cookie records remain native-owned through private V8 matching and
 // exact partition-aware deletion acknowledgements under the same deadline.
 const auto pattern=Parse(R"({"filter":{"name":{"sourceJSON":"\"^chip\"","flags":"g"}}})");
 controls->Handle(100,7,"clearCookies",pattern,7000,done);call=std::move(calls.front());calls.pop_front();call.reply(true,raw,"");assert(calls.size()==1&&controls->Physical()==1);call=std::move(calls.front());calls.pop_front();assert(call.method==suitecut::CookieMethod::Filter&&call.owner==100&&call.context==7&&call.deadline==7000&&call.params->GetList("cookies")->GetSize()==2);call.reply(true,Parse(R"({"result":{"value":{"matches":[0]}}})"),"");assert(calls.size()==1&&controls->Physical()==1);call=std::move(calls.front());calls.pop_front();assert(call.method==suitecut::CookieMethod::Delete&&call.params->GetString("name")=="chip"&&call.params->GetDictionary("partitionKey")&&!call.params->GetDictionary("partitionKey")->GetBool("hasCrossSiteAncestor"));call.reply(true,Parse("{}"),"");assert(controls->Physical()==0);
 controls->Handle(101,7,"clearCookies",pattern,7100,done);call=std::move(calls.front());calls.pop_front();call.reply(true,raw,"");assert(calls.size()==1);controls->Cancel(101);assert(controls->Physical()==1);call=std::move(calls.front());calls.pop_front();call.reply(true,Parse(R"({"result":{"value":{"matches":[0]}}})"),"");assert(calls.empty()&&controls->Physical()==0);
 for(const auto* receipt:{R"({"result":{"value":{"matches":[1,0]}}})",R"({"result":{"value":{"matches":[2]}}})",R"({"result":{"value":{"matches":[0,0]}}})",R"({"exceptionDetails":{"text":"actual V8 syntax failure"}})"}){int failed=0;controls->Handle(102,7,"clearCookies",pattern,7200,[&](bool ok,Dict,const std::string& error){assert(!ok&&!error.empty());++failed;});call=std::move(calls.front());calls.pop_front();call.reply(true,raw,"");call=std::move(calls.front());calls.pop_front();call.reply(true,Parse(receipt),"");assert(failed==1&&calls.empty()&&controls->Physical()==0);}
 int filtered_closed=0;controls->Handle(103,7,"clearCookies",pattern,7300,[&](bool ok,Dict,const auto&error){assert(!ok&&error=="Native cookie context closed");++filtered_closed;});call=std::move(calls.front());calls.pop_front();call.reply(true,raw,"");controls->Closed(7);assert(filtered_closed==1&&controls->Physical()==1);call=std::move(calls.front());calls.pop_front();call.reply(false,nullptr,"actual close");assert(calls.empty()&&controls->Physical()==0);

}
