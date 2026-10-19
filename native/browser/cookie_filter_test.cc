// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
#include "cookie_filter.h"
#include "include/cef_api_hash.h"
using Dict=CefRefPtr<CefDictionaryValue>;
Dict Parse(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
int main(int argc,char**){
 assert(cef_api_hash(CEF_API_VERSION,0));
 auto input=Parse(R"({"filter":{"name":{"sourceJSON":"\"^sess\"","flags":"g"},"domain":{"sourceJSON":"\"one\\\\.com$\"","flags":"i"}},"cookies":[{"name":"session","domain":"ONE.COM","path":"/","value":"NEVER_COPY_SECRET"},{"name":"session","domain":"one.com","path":"/"},{"name":"other","domain":"one.com","path":"/"},{"name":"session","domain":"two.com","path":"/"}]})");
 Dict output;std::string error;assert(suitecut::BuildCookieFilterEvaluation(input,100,75,output,error));assert(output->GetDouble("timeout")==25&&output->GetBool("returnByValue")&&!output->GetBool("awaitPromise"));const auto expression=output->GetString("expression").ToString();assert(expression.find("NEVER_COPY_SECRET")==std::string::npos);
 if(argc==2){std::cout<<expression;return 0;}
 assert(!suitecut::BuildCookieFilterEvaluation(input,100,100,output,error));assert(!suitecut::BuildCookieFilterEvaluation(input,std::numeric_limits<double>::infinity(),0,output,error));assert(!suitecut::BuildCookieFilterEvaluation(input,100,std::numeric_limits<double>::quiet_NaN(),output,error));
 for(const std::string json:{R"("")",R"("escaped\n\t\"\\")",R"("\ud800")",R"("\udfff")",R"("\u0000")"})assert(suitecut::CookiePatternJSONString(json));
 for(const std::string& json:std::vector<std::string>{"",R"(null)",R"("unterminated)",R"("bad\x")",R"("bad\u123")",R"("bad\uXX12")",R"("a"junk)",R"("a"b")",std::string("\"raw\nnewline\"")})assert(!suitecut::CookiePatternJSONString(json));
 bool regex=false;
 for(const auto*json:{R"({"unknown":"x"})",R"({"name":{"sourceJSON":"\"x\"","flags":"ii"}})",R"({"name":{"sourceJSON":"\"x\"","flags":"uv"}})",R"({"name":{"sourceJSON":"12","flags":""}})",R"({"name":{"sourceJSON":"\"x\"","flags":"q"}})",R"({"name":{"sourceJSON":"\"x\"","flags":"","expression":"bad"}})"}){assert(!suitecut::ValidateCookieFilter(Parse(json),regex,error));}
 auto large=Parse(R"({"name":"x"})");large->SetString("name",std::string(8193,'x'));assert(!suitecut::ValidateCookieFilter(large,regex,error));assert(suitecut::ValidateCookieFilter(Parse(R"({"name":{"sourceJSON":"\"\\ud800\"","flags":"u"}})"),regex,error)&&regex);
 input->GetDictionary("filter")->GetDictionary("domain")->SetString("sourceJSON",R"("a\"),globalThis.INJECTED=true,RegExp(\"")");assert(suitecut::BuildCookieFilterEvaluation(input,0,0,output,error)&&output->GetDouble("timeout")==30000);
 // Manager and coordinator both admit exactly this typed parameter envelope.
 for(const auto*json:{R"({})",R"({"filter":{}})",R"({"filter":{"name":""}})",R"({"filter":{"name":{"sourceJSON":"\"^sess\"","flags":"gy"},"domain":"localhost"}})"})assert(suitecut::ValidateCookieFilterParameters(Parse(json),"clearCookies",regex,error));
 for(const auto*json:{R"({"filter":null})",R"({"filter":42})",R"({"filter":[]})",R"({"filter":{"unknown":"x"}})",R"({"filter":{"name":true}})",R"({"filter":{"name":{"sourceJSON":"\"x\"","flags":"ii"}}})",R"({"filter":{"name":{"sourceJSON":"\"x\"","flags":"uv"}}})",R"({"filter":{"name":{"sourceJSON":"\"x\""}}})"})assert(!suitecut::ValidateCookieFilterParameters(Parse(json),"clearCookies",regex,error));
 assert(!suitecut::ValidateCookieFilterParameters(nullptr,"clearCookies",regex,error));
 assert(!suitecut::ValidateCookieFilterParameters(Parse(R"({"filter":{"name":{"sourceJSON":"\"x\"","flags":"g"}}})"),"cookies",regex,error));
 std::cout<<"Native cookie regex ingress/capacity/real-deadline request construction passed\n";
}
