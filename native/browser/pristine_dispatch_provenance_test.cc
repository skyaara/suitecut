// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "pristine_dispatch_provenance.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include <cassert>
#include <cstdio>
using Dict=CefRefPtr<CefDictionaryValue>;
Dict Parse(const std::string&json){auto value=CefParseJSON(json,JSON_PARSER_RFC);assert(value&&value->GetType()==VTYPE_DICTIONARY);return value->GetDictionary();}
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
 const auto positive=R"({"internalProperties":[{"name":"[[FunctionLocation]]","value":{"type":"object","subtype":"internal#location","value":{"scriptId":"33","lineNumber":0,"columnNumber":9}}}]})";
 {assert(suitecut::PristineFunctionScript(Parse(positive))=="33");++groups;}
 {assert(!suitecut::PristineFunctionScript(nullptr));assert(!suitecut::PristineFunctionScript(Parse("{}")));assert(!suitecut::PristineFunctionScript(Parse(R"({"internalProperties":[]})")));++groups;}
 {assert(!suitecut::PristineFunctionScript(Parse(R"({"internalProperties":[{"name":"[[FunctionLocation]]","value":{"type":"object","value":{"scriptId":"33"}}}]})")));++groups;}
 {auto value=Parse(positive);auto fields=value->GetList("internalProperties");fields->SetDictionary(1,fields->GetDictionary(0)->Copy(false));assert(!suitecut::PristineFunctionScript(value));++groups;}
 {auto value=Parse(positive);value->SetDictionary("exceptionDetails",CefDictionaryValue::Create());assert(!suitecut::PristineFunctionScript(value));++groups;}
 {for(const auto&id:{std::string(),std::string(129,'x')}){auto value=Parse(positive);value->GetList("internalProperties")->GetDictionary(0)->GetDictionary("value")->GetDictionary("value")->SetString("scriptId",id);assert(!suitecut::PristineFunctionScript(value));}++groups;}
 {auto value=Parse(positive);value->GetList("internalProperties")->SetSize(65);assert(!suitecut::PristineFunctionScript(value));++groups;}
 {assert(!suitecut::PristineFunctionScript(Parse(R"({"result":[{"name":"scriptId","value":{"type":"string","value":"33"}}]})")));++groups;}
 std::printf("PristineFunctionScript: %d controlled groups PASS\n",groups);
}
