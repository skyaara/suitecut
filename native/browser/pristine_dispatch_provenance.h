// SPDX-License-Identifier: MIT
#pragma once
#include <optional>
#include <string>
#include <utility>
#include "include/cef_values.h"
namespace suitecut {
// Parse only an actual Runtime.getProperties receipt for a retained function.
// Source separately pins the exact captured object/group/document and deadline.
inline std::optional<std::string> PristineFunctionScript(CefRefPtr<CefDictionaryValue> result){
  if(!result||result->HasKey("exceptionDetails"))return std::nullopt;
  auto fields=result->GetList("internalProperties");if(!fields||fields->GetSize()>64)return std::nullopt;
  std::optional<std::string> script;
  for(size_t i=0;i<fields->GetSize();++i){
    auto field=fields->GetDictionary(i);if(!field||field->GetString("name")!="[[FunctionLocation]]")continue;
    if(script)return std::nullopt;
    auto remote=field->GetDictionary("value"),location=remote?remote->GetDictionary("value"):nullptr;
    if(!remote||remote->GetString("type")!="object"||remote->GetString("subtype")!="internal#location"||!location||location->GetType("scriptId")!=VTYPE_STRING)return std::nullopt;
    auto id=location->GetString("scriptId").ToString();if(id.empty()||id.size()>128)return std::nullopt;
    script=std::move(id);
  }
  return script;
}
} // namespace suitecut
