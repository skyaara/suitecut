// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "coverage_retention.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
using Dict=suitecut::CoverageRetention::Dict;
Dict Parse(const std::string& json){auto value=CefParseJSON(json,JSON_PARSER_RFC);assert(value&&value->GetType()==VTYPE_DICTIONARY);return value->GetDictionary();}
Dict Chunk(int sequence,int destroyed,double timestamp,const std::string& ranges,int context=7,const std::string& script="42"){
  auto chunk=Parse("{\"sequence\":"+std::to_string(sequence)+",\"destroyedContextId\":"+std::to_string(destroyed)+",\"timestamp\":"+std::to_string(timestamp)+",\"result\":[{\"executionContextId\":"+std::to_string(context)+",\"source\":\"\",\"coverage\":{\"scriptId\":\""+script+"\",\"url\":\"https://actual/script.js\",\"functions\":[{\"functionName\":\"actual\",\"isBlockCoverage\":true,\"ranges\":"+ranges+"}]}}]}");
  chunk->GetList("result")->GetDictionary(0)->SetString("source",std::string(100,'x'));return chunk;
}
Dict Payload(Dict first,Dict second=nullptr){auto payload=CefDictionaryValue::Create();auto chunks=CefListValue::Create();chunks->SetDictionary(0,first->Copy(false));if(second)chunks->SetDictionary(1,second->Copy(false));payload->SetList("result",chunks);return payload;}
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));using Retention=suitecut::CoverageRetention;Dict output;std::string error;
  const auto select=[](int context,const std::string& script){return context==7&&script=="42";};
  const std::string one=R"([{"startOffset":0,"endOffset":100,"count":2},{"startOffset":20,"endOffset":40,"count":0}])";
  const std::string two=R"([{"startOffset":0,"endOffset":100,"count":3},{"startOffset":30,"endOffset":60,"count":1}])";
  auto first=Chunk(1,9,1,one),last=Chunk(2,0,2,two);auto payload=Payload(first,last);
  assert(Retention::JavaScript(payload,select,output,error));auto entries=output->GetList("result");assert(entries->GetSize()==1);auto ranges=entries->GetDictionary(0)->GetList("functions")->GetDictionary(0)->GetList("ranges");assert(ranges->GetSize()==4);assert(ranges->GetDictionary(0)->GetDouble("count")==5);assert(ranges->GetDictionary(1)->GetInt("startOffset")==20&&ranges->GetDictionary(1)->GetInt("endOffset")==30&&ranges->GetDictionary(1)->GetDouble("count")==3);assert(ranges->GetDictionary(2)->GetInt("startOffset")==30&&ranges->GetDictionary(2)->GetInt("endOffset")==40&&ranges->GetDictionary(2)->GetDouble("count")==1);assert(ranges->GetDictionary(3)->GetInt("startOffset")==40&&ranges->GetDictionary(3)->GetInt("endOffset")==60&&ranges->GetDictionary(3)->GetDouble("count")==3);
  // Equal text/URL never merges different actual contexts or script identities.
  last=Chunk(2,0,2,two,8);assert(Retention::JavaScript(Payload(first,last),[](int,const std::string&){return true;},output,error));assert(output->GetList("result")->GetSize()==2);
  assert(Retention::JavaScript(payload,[](int,const std::string&){return false;},output,error));assert(output->GetList("result")->GetSize()==0);
  auto invalid=Payload(Chunk(1,9,1,one));assert(!Retention::JavaScript(invalid,select,output,error)&&!output);
  invalid=Payload(Chunk(1,0,1,one),Chunk(2,0,2,two));assert(!Retention::JavaScript(invalid,select,output,error));
  last=Chunk(2,0,2,two);last->SetInt("sequence",3);assert(!Retention::JavaScript(Payload(first,last),select,output,error));
  last=Chunk(2,0,2,two);last->GetList("result")->GetDictionary(0)->SetString("source",std::string(100,'y'));assert(!Retention::JavaScript(Payload(first,last),select,output,error));
  const auto overflow=R"([{"startOffset":0,"endOffset":100,"count":9007199254740991}])";assert(!Retention::JavaScript(Payload(Chunk(1,9,1,overflow),Chunk(2,0,2,one)),select,output,error));
  const auto crossing=R"([{"startOffset":0,"endOffset":100,"count":2},{"startOffset":10,"endOffset":40,"count":1},{"startOffset":30,"endOffset":70,"count":0}])";assert(!Retention::JavaScript(Payload(Chunk(1,0,1,crossing)),select,output,error));
  const auto duplicate=R"([{"startOffset":0,"endOffset":100,"count":2},{"startOffset":0,"endOffset":100,"count":1}])";assert(!Retention::JavaScript(Payload(Chunk(1,0,1,duplicate)),select,output,error));
  auto css=Parse(R"({"styleSheets":[{"header":{"styleSheetId":"actual-sheet","sourceURL":"https://actual/sheet.css","origin":"regular"},"text":"div{color:red}p{color:green}"}],"ruleUsage":[{"styleSheetId":"actual-sheet","startOffset":0,"endOffset":14,"used":false},{"styleSheetId":"actual-sheet","startOffset":0,"endOffset":14,"used":true},{"styleSheetId":"actual-sheet","startOffset":14,"endOffset":27,"used":true}]})");
  assert(Retention::CSS(css,output,error));auto cssranges=output->GetList("result")->GetDictionary(0)->GetList("ranges");assert(cssranges->GetSize()==1&&cssranges->GetDictionary(0)->GetInt("start")==0&&cssranges->GetDictionary(0)->GetInt("end")==27);
  css->GetList("ruleUsage")->GetDictionary(0)->SetString("styleSheetId","unknown");assert(!Retention::CSS(css,output,error)&&!output);
  std::cout<<"Retained native coverage identity, final receipts, nested delta merging, counts, source validation and bounds passed\n";
}
