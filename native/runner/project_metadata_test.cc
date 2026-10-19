// SPDX-License-Identifier: MIT
#include "project_metadata.h"
#include <cassert>
using suitecut::runner::ProjectMetadataJSON;
int main(){
  for(const auto* json:{"{}"," {\"branch\":\"release\",\"nested\":[1,-2.5e+3,true,false,null,{\"key\":\"\\ud800\"}]}\n","{\"unicode\":\"π😀\"}","{\"escaped\":\"\\\"\\\\\\n\"}"})assert(ProjectMetadataJSON(json));
  for(const auto* json:{"","null","[]","{","{}{}","{\"a\":01}","{\"a\":1.}","{\"a\":1e}","{\"a\":+1}","{\"a\":truee}","{\"a\":1,}","{\"a\":[1,]}","{\"a\":\"\\x\"}","{\"a\":\"raw\nnewline\"}"})assert(!ProjectMetadataJSON(json));
  assert(!ProjectMetadataJSON("{\"a\":\""+std::string(16385,'x')+"\"}"));
  assert(!ProjectMetadataJSON("{\"a\":"+std::string(18,'[')+"0"+std::string(18,']')+"}"));
  assert(!ProjectMetadataJSON(std::string("{\"a\":\"")+char(0xff)+"\"}"));
}
