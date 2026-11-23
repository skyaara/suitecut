// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include "work/inbound_string.h"

namespace suitecut {
// Only Chromium's native Page.Frame.name field is accepted here. Author object
// dictionaries are never passed to this decoder or recursively interpreted.
// The whole name is kept, however long, as Chromium reports it.
inline bool ReadInspectorFrameName(CefRefPtr<CefDictionaryValue> frame,std::string& name){
 name.clear();if(!frame)return false;if(!frame->HasKey("name"))return true;
 const auto value=frame->GetValue("name");std::u16string units;
 if(value&&value->GetType()==VTYPE_STRING)units=value->GetString().ToString16();
 else{
  const auto scalar=value&&value->GetType()==VTYPE_DICTIONARY?value->GetDictionary():nullptr;
  if(!scalar||scalar->GetSize()!=1||scalar->GetType(kDevToolsUTF16Tag)!=VTYPE_STRING||!DevToolsReadTag(value,units))return false;
 }
 name=DevToolsUnitsWTF8(units);return true;
}

// Public Page.getFrameTree contains native schema objects, not author values.
// Only Page.Frame.name may consume the private scalar transport representation.
// Everything else is emitted by CEF's ordinary JSON writer without decoding
// arbitrary dictionaries. Chromium bounds the frame count; nothing is capped here.
inline bool InspectorFrameTreeJSON(CefRefPtr<CefDictionaryValue> reply,std::string& json){
 json.clear();
 const auto append=[&json](const std::string& part){json+=part;return true;};
 const auto ordinary=[&append](CefRefPtr<CefValue> value){return value&&append(CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString());};
 const auto key_json=[](const CefString& key){auto value=CefValue::Create();value->SetString(key);return CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();};
 const auto frame_json=[&](CefRefPtr<CefDictionaryValue> frame){
  std::string name;if(!frame||frame->GetType("id")!=VTYPE_STRING||frame->GetType("url")!=VTYPE_STRING||!ReadInspectorFrameName(frame,name))return false;
  if(!append("{"))return false;
  bool first=true;CefDictionaryValue::KeyList keys;frame->GetKeys(keys);
  for(const auto& key:keys){if(!first&&!append(","))return false;first=false;if(!append(key_json(key))||!append(":"))return false;
   if(key=="name"){
    auto value=frame->GetValue(key);std::u16string units;
    if(value->GetType()==VTYPE_STRING){if(!ordinary(value))return false;}
    else if(!DevToolsReadTag(value,units)||!append(DevToolsQuoteUnits(units)))return false;
   }else if(!ordinary(frame->GetValue(key)))return false;
  }
  return append("}");
 };
 const auto tree_json=[&](auto&& self,CefRefPtr<CefDictionaryValue> tree)->bool{
  if(!tree||tree->GetType("frame")!=VTYPE_DICTIONARY||!append("{"))return false;
  bool first=true;CefDictionaryValue::KeyList keys;tree->GetKeys(keys);
  for(const auto& key:keys){if(!first&&!append(","))return false;first=false;if(!append(key_json(key))||!append(":"))return false;
   if(key=="frame"){if(!frame_json(tree->GetDictionary(key)))return false;}
   else if(key=="childFrames"){
    const auto children=tree->GetList(key);if(!children||!append("["))return false;
    for(size_t index=0;index<children->GetSize();++index){if(index&&!append(","))return false;if(children->GetType(index)!=VTYPE_DICTIONARY||!self(self,children->GetDictionary(index)))return false;}
    if(!append("]"))return false;
   }else if(!ordinary(tree->GetValue(key)))return false;
  }
  return append("}");
 };
 if(!reply||reply->GetType("frameTree")!=VTYPE_DICTIONARY||!append("{"))return false;
 bool first=true;CefDictionaryValue::KeyList keys;reply->GetKeys(keys);
 for(const auto& key:keys){if(!first&&!append(","))return false;first=false;if(!append(key_json(key))||!append(":"))return false;
  if(key=="frameTree"){if(!tree_json(tree_json,reply->GetDictionary(key)))return false;}
  else if(!ordinary(reply->GetValue(key)))return false;
 }
 return append("}");
}
} // namespace suitecut
