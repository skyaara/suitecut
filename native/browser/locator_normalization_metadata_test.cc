// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "locator_normalization_metadata.h"
#include <cassert>
#include <iostream>
int main(){assert(cef_api_hash(CEF_API_VERSION,0));std::string error;
  const auto valid=[&](const std::string&json){const auto before=json;assert(suitecut::LocatorNormalizationMetadata(json,error)&&error.empty()&&json==before);};
  const auto invalid=[&](const std::string&json){assert(!suitecut::LocatorNormalizationMetadata(json,error)&&!error.empty());};
  valid(R"JSON({"kind":"id","query":[{"engine":"css","selector":"#actual"}]})JSON");
  valid(R"JSON({"kind":"role","query":[{"engine":"role","role":"button","name":{"text":"Actual","exact":true}}]})JSON");
  for(const char*engine:{"placeholder","alt","title"})valid("{\"kind\":\"attribute\",\"query\":[{\"engine\":\""+std::string(engine)+"\",\"text\":\"actual\",\"exact\":true}]}");
  valid(R"JSON({"kind":"testId","query":[{"engine":"testId","attribute":"data-custom","text":"{\"__suitecutNativeUTF16Scalar\":\"d800\"}","exact":true}]})JSON");
  valid(R"JSON({"kind":"path","query":[{"engine":"css","selector":"html > body > div:nth-of-type(2)"},{"engine":"css","selector":"button:nth-of-type(3)"}]})JSON");
  valid(R"JSON({"kind":"testId","query":[{"engine":"testId","attribute":"data-units","text":"Actual\ud800\u0000\\\"\udfff","exact":true}]})JSON");
  for(const char*value:{"{","null","[]","\"actual\""})invalid(value);
  invalid(R"JSON({"kind":"id","query":[{"engine":"css","selector":"#actual"}],"__suitecutNativeUTF16Scalar":"d800"})JSON");
  for(const char*engine:{"reference","custom","nth","filter","text"})invalid("{\"kind\":\"path\",\"query\":[{\"engine\":\""+std::string(engine)+"\",\"selector\":\"actual\"}]}");
  invalid(R"JSON({"kind":"id","query":[{"engine":"css","selector":"button"}]})JSON");invalid(R"JSON({"kind":"testId","query":[{"engine":"css","selector":"#actual"}]})JSON");
  invalid(R"JSON({"kind":"attribute","query":[{"engine":"alt","text":"Actual","exact":false}]})JSON");invalid(R"JSON({"kind":"attribute","query":[{"engine":"alt","text":"Actual","exact":"true"}]})JSON");
  invalid(R"JSON({"kind":"role","query":[{"engine":"role","role":"button","name":{"pattern":"Actual","flags":""}}]})JSON");
  invalid(R"JSON({"kind":"role","query":[{"engine":"role","role":"forged role","name":{"text":"Actual","exact":true}}]})JSON");
  invalid(R"JSON({"kind":"path","query":[]})JSON");std::string many="{\"kind\":\"path\",\"query\":[";for(int i=0;i<65;++i){if(i)many+=',';many+=R"JSON({"engine":"css","selector":"button"})JSON";}many+="]}";invalid(many);
  invalid(std::string(32769,'x'));invalid("{\"kind\":\"id\",\"query\":[{\"engine\":\"css\",\"selector\":\"#"+std::string(16400,'x')+"\"}]}");
  invalid(R"JSON({"kind":"testId","query":[{"engine":"testId","attribute":"data-real","text":{"__suitecutNativeUTF16Scalar":"d800"},"exact":true}]})JSON");
  std::cout<<"PASS16 controlled real-CEF normalized builtin AST shape/byte/engine/Unicode/tag-collision groups; no browser identity or installed normalization claim\n";
}
