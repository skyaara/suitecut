// SPDX-License-Identifier: MIT
#include "cdp_json.h"
#include <cstdlib>
#include <iostream>

void Check(bool value) {if(!value) {std::cerr<<"CDP JSON transport regression\n";std::abort();}}
int main() {
  using namespace suitecut;
  const std::string raw=R"({"id":7,"result":{"value":"\udafe","literal":"\\udafe","pair":"\ud83d\ude00","nested":[{"id":999}]}})";
  Check(CDPMetadataJSON(raw)==R"({"id":7,"result":{"value":"\ufffd","literal":"\\udafe","pair":"\ud83d\ude00","nested":[{"id":999}]}})");
  const auto rewritten=CDPReplaceField(CDPReplaceField(raw,"id","42"),"sessionId",R"("page-1::worker-1")");
  Check(CDPRawField(rewritten,"id")=="42");
  Check(CDPRawField(rewritten,"result")==CDPRawField(raw,"result"));
  Check(CDPRawField(rewritten,"sessionId")==R"("page-1::worker-1")");
  Check(CDPMetadataJSON(R"({"v":"\udc00\uD800x\ud800\udfff"})")==R"({"v":"\ufffd\ufffdx\ud800\udfff"})");
  Check(CDPRawField(R"({"params":{"a":["}","\\\"",1]},"id":2})","params")==R"({"a":["}","\\\"",1]})");
  Check(CDPReplaceField("{}","id","1")==R"({"id":1})");
  Check(CDPRawField(R"({"id":1,"id":2})","id")=="2");
  for(const std::string bad : {"", "{", "{\"id\":", "{\"id\":\"\\", "[]"})Check(!CDPFieldSpan(bad,"id"));
  std::string deep(300,'[');deep+="0";deep+=std::string(300,']');
  Check(!CDPFieldSpan("{\"v\":"+deep+"}","v"));
  std::cout<<"CDP JSON transport regressions passed\n";
}
