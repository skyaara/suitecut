// SPDX-License-Identifier: MIT
#include "cdp_json.h"
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data,size_t size) {
  if(size>4194304)return 0;
  const std::string_view input(reinterpret_cast<const char*>(data),size);
  const auto masked=suitecut::CDPMetadataJSON(input);
  if(masked.size()!=size)std::abort();
  for(const std::string_view key : {"id","params","sessionId","result","message"}) {
    if(const auto span=suitecut::CDPFieldSpan(input,key)) {
      if(span->first>=span->second || span->second>size)std::abort();
      const auto raw=suitecut::CDPRawField(input,key);
      if(raw!=input.substr(span->first,span->second-span->first))std::abort();
    }
    (void)suitecut::CDPReplaceField(std::string(input),key,"null");
  }
  return 0;
}
