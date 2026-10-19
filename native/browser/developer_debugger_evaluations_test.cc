// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "developer_debugger_evaluations.h"
#include "developer_debugger_evaluation_projection.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <utility>
#include <vector>
#include "include/cef_api_hash.h"
using Eval=suitecut::DeveloperDebuggerEvaluations;using Dict=Eval::Dict;
static Dict JSON(const std::string&text){auto value=CefParseJSON(text,JSON_PARSER_RFC);assert(value);auto result=value->GetDictionary();assert(result);return result;}
struct Fixture{
  struct Call{std::string method;Dict params;double deadline;suitecut::DebuggerDomains::Reply reply;};
  struct Result{bool ok;std::string json,error;};
  double now=1;bool current=true,refuse=false,synchronous_failure=false,advance_clock=false;int clock_reads=0;std::vector<Call>calls;std::map<int,Result>results;std::vector<std::string>fatal;std::unique_ptr<Eval>eval;
  Fixture(){static size_t serial=0;Eval::Hooks h;h.session="actual-native-session-"+std::to_string(++serial);h.now=[this]{return advance_clock&&++clock_reads>1?now+10:now;};h.current=[this](const auto&pause,const auto&frame){return current&&pause=="actual-pause"&&frame=="actual-frame";};
    h.send=[this](const auto&method,Dict params,double deadline,auto reply){if(refuse){if(synchronous_failure)reply(false,nullptr,"actual refused");return false;}calls.push_back({method,params->Copy(false),deadline,std::move(reply)});return true;};
    h.close_channel=[this](const auto&error){fatal.push_back(error);};h.changed=[]{};eval=std::make_unique<Eval>(std::move(h));}
  ~Fixture(){Dead();}
  void Dead(){if(eval)eval->Closed();calls.clear();}
  Eval::Reply Reply(int id){return [this,id](bool ok,const auto&json,const auto&error){assert(!results.contains(id));results.emplace(id,Result{ok,json,error});};}
  void Run(int owner=1,double deadline=100,const std::string&source="\"actualLocal+1\""){eval->Evaluate(owner,"actual-pause","actual-frame",source,deadline,Reply(owner));}
  void Ack(const std::string&method,bool ok=true,Dict result=nullptr){auto found=std::find_if(calls.begin(),calls.end(),[&](const auto&call){return call.method==method;});assert(found!=calls.end());auto reply=std::move(found->reply);calls.erase(found);reply(ok,result?result:CefDictionaryValue::Create(),ok?"":"actual Inspector error");}
  Dict Number(int value=42){return JSON("{\"result\":{\"type\":\"number\",\"value\":"+std::to_string(value)+"}}");}
};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
  {Fixture f;f.Run();assert(f.calls.size()==1&&f.eval->Retained()==1);const auto&call=f.calls.front();assert(call.params->GetString("expression")=="actualLocal+1"&&call.params->GetString("callFrameId")=="actual-frame"&&!call.params->GetBool("returnByValue")&&!call.params->GetBool("includeCommandLineAPI"));const auto group=call.params->GetString("objectGroup");f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());assert(!f.results.contains(1)&&f.eval->Retained()==1&&f.calls.front().params->GetString("objectGroup")==group);f.Ack("Runtime.releaseObjectGroup");assert(f.results.at(1).ok&&JSON(f.results.at(1).json)->GetDictionary("value")->GetInt("value")==42&&f.eval->Retained()==0);++groups;}
  {Fixture f;f.Run();auto response=JSON(R"({"result":{"type":"string","value":"placeholder"}})");suitecut::DevToolsSetString(response->GetDictionary("result"),"value",suitecut::DevToolsUnitsWTF8(std::u16string{0xd800}));f.Ack("Debugger.evaluateOnCallFrame",true,response);response->Clear();f.Ack("Runtime.releaseObjectGroup");assert(f.results.at(1).ok&&f.results.at(1).json.find("\\ud800")!=std::string::npos&&f.results.at(1).json.find("__suitecutNativeUTF16Scalar")==std::string::npos);++groups;}
  {for(const auto&text:{R"({"type":"undefined"})",R"({"type":"object","subtype":"null","value":null})",R"({"type":"boolean","value":true})",R"({"type":"number","unserializableValue":"NaN"})",R"({"type":"number","unserializableValue":"-0"})",R"({"type":"number","unserializableValue":"Infinity"})",R"({"type":"number","unserializableValue":"-Infinity"})",R"({"type":"bigint","unserializableValue":"-9007199254740993n"})"}){std::string out,error;assert(suitecut::DeveloperDebuggerEvaluationProjection::Build(JSON(std::string("{\"result\":")+text+"}"),out,error));assert(JSON(out)->GetString("kind")=="value");}++groups;}
  {Fixture f;f.Run();auto response=JSON(R"({"result":{"type":"object","objectId":"actual-exception-object"},"exceptionDetails":{"text":"Uncaught","lineNumber":3,"columnNumber":2,"scriptId":"actual-script","exception":{"type":"string","value":"actual thrown","description":"actual description"}}})");suitecut::DevToolsSetString(response->GetDictionary("exceptionDetails"),"text",suitecut::DevToolsUnitsWTF8(std::u16string{0xdfff}));f.Ack("Debugger.evaluateOnCallFrame",true,response);assert(f.calls.front().method=="Runtime.releaseObjectGroup"&&!f.results.contains(1));f.Ack("Runtime.releaseObjectGroup");assert(f.results.at(1).ok&&f.results.at(1).json.find("\\udfff")!=std::string::npos&&f.results.at(1).json.find("actual-exception-object")==std::string::npos);auto result=JSON(suitecut::StorageJSONValidationProjection(f.results.at(1).json));assert(result->GetString("kind")=="exception"&&result->GetDictionary("exception")->GetDictionary("thrown")->GetString("value")=="actual thrown");++groups;}
  {Fixture f;f.Run();f.Ack("Debugger.evaluateOnCallFrame",true,JSON(R"({"result":{"type":"object","value":{"__suitecutNativeUTF16Scalar":"d800"}}})"));assert(!f.results.contains(1)&&f.eval->Retained()==1);f.Ack("Runtime.releaseObjectGroup");assert(!f.results.at(1).ok&&f.results.at(1).error.find("primitive")!=std::string::npos);++groups;}
  {Fixture f;f.Run(1,5);f.now=5;f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());assert(f.calls.front().deadline==30005);f.Ack("Runtime.releaseObjectGroup");assert(!f.results.at(1).ok&&f.results.at(1).error.find("original deadline")!=std::string::npos&&f.eval->Retained()==0);++groups;}
  {Fixture f;f.Run();f.eval->Cancel(1);f.eval->Close();assert(f.eval->Retained()==1&&f.calls.size()==1);f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());assert(f.calls.front().deadline==30001&&!f.results.contains(1));f.Ack("Runtime.releaseObjectGroup");assert(f.eval->Retained()==0&&f.results.empty());++groups;}
  {Fixture f;f.Run();f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());f.current=false;f.Ack("Runtime.releaseObjectGroup");assert(!f.results.at(1).ok&&f.results.at(1).error.find("pause or frame changed")!=std::string::npos);++groups;}
  {Fixture f;f.Run();f.eval->Close();assert(f.eval->Retained()==1);f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());assert(f.eval->Retained()==1&&!f.results.contains(1));f.Ack("Runtime.releaseObjectGroup");assert(f.eval->Retained()==0&&f.results.empty());++groups;}
  {Fixture f;f.Run();f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());f.Ack("Runtime.releaseObjectGroup",false);assert(f.eval->Retained()==1&&f.fatal.size()==1&&!f.results.at(1).ok);f.Dead();assert(f.eval->Retained()==0);++groups;}
  {Fixture f;f.Run();f.eval->RealmDestroyed();assert(f.eval->Retained()==1&&f.calls.size()==1);f.Ack("Debugger.evaluateOnCallFrame",false);assert(f.eval->Retained()==0&&f.calls.empty()&&f.fatal.empty()&&!f.results.at(1).ok);++groups;}
  {Fixture f;f.Run();f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());f.eval->RealmDestroyed();assert(f.eval->Retained()==1&&f.calls.size()==1);f.Ack("Runtime.releaseObjectGroup",false);assert(f.eval->Retained()==0&&f.fatal.empty()&&!f.results.at(1).ok);++groups;}
  {Fixture f;f.Run();f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());f.Ack("Runtime.releaseObjectGroup",false);assert(f.eval->Retained()==1);f.eval->RealmDestroyed();assert(f.eval->Retained()==0);++groups;}
  {Fixture f;f.Run(1,0);f.eval->Cancel(1);assert(f.eval->NeedsTick());f.now=30001;f.eval->Tick(f.now);assert(f.fatal.size()==1&&!f.eval->NeedsTick()&&f.eval->Retained()==1);f.now=50000;f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());assert(f.calls.front().deadline==30001);f.Ack("Runtime.releaseObjectGroup");assert(f.eval->Retained()==0&&!f.results.contains(1));++groups;}
  {Fixture f;for(int owner=1;owner<=8;++owner)f.Run(owner);f.Run(9);assert(f.eval->Retained()==8&&f.calls.size()==8&&!f.results.at(9).ok);++groups;}
  {std::vector<std::unique_ptr<Fixture>>pool;for(int index=0;index<8;++index){pool.push_back(std::make_unique<Fixture>());for(int owner=1;owner<=8;++owner)pool.back()->Run(owner);}assert(Eval::ProcessRetained()==64);Fixture extra;extra.Run();assert(!extra.results.at(1).ok&&extra.calls.empty());for(auto&f:pool)f->Dead();assert(Eval::ProcessRetained()==0);extra.Run(2);assert(extra.eval->Retained()==1);++groups;}
  {Fixture f;f.refuse=true;f.Run();assert(!f.results.at(1).ok&&f.calls.empty()&&f.eval->Retained()==0&&f.fatal.empty());f.synchronous_failure=true;f.Run(2);assert(!f.results.at(2).ok&&f.calls.empty()&&f.eval->Retained()==0);++groups;}
  {Fixture f;f.Run();f.Ack("Debugger.evaluateOnCallFrame",false);assert(!f.results.contains(1)&&f.eval->Retained()==1);f.Ack("Runtime.releaseObjectGroup");assert(!f.results.at(1).ok&&f.eval->Retained()==0);++groups;}
  {Fixture f;f.Run(1,100,R"("'\ud800'")");assert(!f.results.at(1).ok&&f.calls.empty());f.Run(2,100,R"("'\\ud800'")");assert(f.calls.front().params->GetString("expression")=="'\\ud800'");f.Ack("Debugger.evaluateOnCallFrame",true,JSON(R"({"result":{"type":"string","value":"actual"}})"));f.Ack("Runtime.releaseObjectGroup");assert(f.results.at(2).ok);++groups;}
  {std::string out,error;auto response=JSON(R"({"result":{"type":"string","value":"actual"}})");response->GetDictionary("result")->SetString("value",std::string(8193,'x'));assert(!suitecut::DeveloperDebuggerEvaluationProjection::Build(response,out,error));assert(!suitecut::DeveloperDebuggerEvaluationProjection::Build(JSON(R"({"result":{"type":"bigint","unserializableValue":"1e6n"}})"),out,error));++groups;}
  {Fixture f;const double deadline=f.now+0.0001;f.Run(1,deadline);assert(f.calls.front().params->GetDouble("timeout")>0&&f.calls.front().params->GetDouble("timeout")<=deadline-f.now);f.Ack("Debugger.evaluateOnCallFrame",true,f.Number());f.Ack("Runtime.releaseObjectGroup");assert(f.results.at(1).ok);++groups;}
  {Fixture f;f.advance_clock=true;f.Run(1,5);assert(!f.results.at(1).ok&&f.results.at(1).error.find("original deadline")!=std::string::npos&&f.calls.empty()&&f.eval->Retained()==0);++groups;}
  assert(Eval::ProcessRetained()==0);std::cout<<"developer paused evaluation component "<<groups<<" groups passed\n";
}
