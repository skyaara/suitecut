// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "query_world_plan.h"
#include <cassert>
#include <deque>
#include <iostream>
struct Harness{
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Call{std::string method;Dict params;SuiteCutQueryNodeTransfer::Result done;};
  std::deque<Call> calls;SuiteCutQueryWorldPlan::Nodes delivered;std::string error,token,scope;
  std::function<void(std::string)> resolve;bool current=true;
  void Start(bool resolver=true,bool css=false,bool bad=false,std::string root={}){
    auto plan=CefListValue::Create();size_t index=0;
    if(css){auto step=CefDictionaryValue::Create();step->SetString("engine","css");step->SetString("selector","section");plan->SetDictionary(index++,step);}
    auto reference=CefDictionaryValue::Create();reference->SetString("engine","reference");if(!bad)reference->SetString("ref","rControlledSource_1_0");plan->SetDictionary(index,reference);
    SuiteCutQueryWorldPlan::Reference lookup;if(resolver)lookup=[this](const std::string& ref,SuiteCutQueryWorldPlan::Nodes nodes,std::function<void(std::string)> done){token=ref;scope=nodes.object;assert(nodes.context==7);resolve=std::move(done);};
    SuiteCutQueryWorldPlan::Run([this](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result done){calls.push_back({method,params->Copy(false),std::move(done)});},[this]{return current;},[this](const std::string& cause){error=cause;},3,7,"controlled-operation-group","function(){throw new Error('Controlled helper is never executed');}","{}",plan,[](const std::string&){return false;},[this](auto nodes){delivered=std::move(nodes);},true,std::move(root),std::move(lookup));
  }
  void Root(std::string method="Runtime.evaluate"){
    assert(calls.size()==1);auto call=std::move(calls.front());calls.pop_front();assert(call.method==method);
    if(method=="Runtime.evaluate")assert(call.params->GetInt("contextId")==7&&call.params->GetString("expression")=="[document]");
    else assert(call.params->GetString("objectId")=="controlled-retained-root");
    call.done(true,R"({"result":{"objectId":"controlled-scope-array"}})");
  }
};
int main(){cef_api_hash(CEF_API_VERSION,0);
  {Harness h;h.Start();h.Root();assert(h.calls.empty()&&h.resolve&&h.token=="rControlledSource_1_0"&&h.scope=="controlled-scope-array");h.resolve("controlled-reference-array");assert(h.error.empty()&&h.delivered.object=="controlled-reference-array"&&h.delivered.context==7&&h.calls.empty());}
  {Harness h;h.Start(false);h.Root();assert(!h.error.empty()&&!h.resolve&&h.delivered.object.empty()&&h.calls.empty());}
  {Harness h;h.Start();h.Root();assert(h.resolve);h.current=false;h.resolve("late-controlled-reference-array");assert(!h.error.empty()&&h.delivered.object.empty()&&h.calls.empty());}
  {Harness h;h.Start(true,true);h.Root();assert(h.calls.size()==1&&!h.resolve);auto css=std::move(h.calls.front());h.calls.pop_front();assert(css.method=="Runtime.callFunctionOn"&&css.params->GetString("objectId")=="controlled-scope-array");css.done(true,R"({"result":{"subtype":"array","objectId":"controlled-css-scope-array"}})");assert(h.resolve&&h.scope=="controlled-css-scope-array");h.resolve("controlled-relative-reference-array");assert(h.delivered.object=="controlled-relative-reference-array"&&h.calls.empty());}
  {Harness h;h.Start(true,false,true);h.Root();assert(!h.error.empty()&&!h.resolve&&h.delivered.object.empty()&&h.calls.empty());}
  {Harness h;h.Start(true,false,false,"controlled-retained-root");h.Root("Runtime.callFunctionOn");assert(h.resolve&&h.scope=="controlled-scope-array");h.resolve("controlled-relative-root-reference");assert(h.error.empty()&&h.delivered.object=="controlled-relative-root-reference");}
  std::cout<<"PASS6 controlled native plan/reference resolver scope, utility realm, root and late-generation groups; no actual browser-reference claim\n";
}
