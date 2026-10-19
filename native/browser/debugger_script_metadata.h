// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include "devtools_utf16.h"
namespace suitecut {
// Protocol-owned metadata only. False attestation means unproven, not author
// ownership; this projection never authorizes a script mutation.
class DebuggerScriptMetadata {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  static bool Build(Dict metadata,bool attested,std::string&output){
    DebuggerScriptMetadata writer;
    if(!metadata||metadata->GetType("scriptId")!=VTYPE_STRING||metadata->GetString("scriptId").empty())return false;
    writer.out_="{\"scriptId\":";
    if(!writer.Text(metadata->GetValue("scriptId"),256))return false;
    writer.out_+=",\"url\":";
    if(!writer.Text(metadata->GetValue("url"),32768))return false;
    for(const auto*key:{"hash","scriptLanguage"})if(metadata->HasKey(key)){
      writer.out_+=std::string(",\"")+key+"\":";
      if(!writer.Text(metadata->GetValue(key),32768))return false;
    }
    if(metadata->HasKey("hasSourceURL")){
      if(metadata->GetType("hasSourceURL")!=VTYPE_BOOL)return false;
      writer.out_+=metadata->GetBool("hasSourceURL")?",\"hasSourceURL\":true":",\"hasSourceURL\":false";
    }
    writer.out_+=attested?",\"attestedNative\":true}":",\"attestedNative\":false}";
    if(writer.out_.size()>kMaximum)return false;
    output=std::move(writer.out_);return true;
  }
 private:
  static constexpr size_t kMaximum=512*1024;
  std::string out_;
  bool Text(CefRefPtr<CefValue>value,size_t maximum){
    if(!value)return false;
    std::u16string units;
    if(value->GetType()==VTYPE_STRING)units=value->GetString().ToString16();else if(!DevToolsReadTag(value,units))return false;
    if(units.size()>maximum)return false;
    const auto quoted=DevToolsQuoteUnits(units);
    if(quoted.size()>kMaximum-std::min(kMaximum,out_.size()))return false;
    out_+=quoted;return true;
  }
};
}
