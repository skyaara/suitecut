// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include "inbound_string.h"

namespace suitecut {
inline constexpr size_t kNativeFrameNameUnits=1024;
// Only Chromium's native Page.Frame.name field is accepted here. Author object
// dictionaries are never passed to this decoder or recursively interpreted.
inline bool ReadInspectorFrameName(CefRefPtr<CefDictionaryValue> frame,std::string& name){
 name.clear();if(!frame)return false;if(!frame->HasKey("name"))return true;
 const auto value=frame->GetValue("name");std::u16string units;
 if(value&&value->GetType()==VTYPE_STRING)units=value->GetString().ToString16();
 else{
  const auto scalar=value&&value->GetType()==VTYPE_DICTIONARY?value->GetDictionary():nullptr;
  if(!scalar||scalar->GetSize()!=1||scalar->GetType(kDevToolsUTF16Tag)!=VTYPE_STRING||scalar->GetString(kDevToolsUTF16Tag).length()>kNativeFrameNameUnits*4||!DevToolsReadTag(value,units))return false;
 }
 if(units.size()>kNativeFrameNameUnits)return false;
 name=DevToolsUnitsWTF8(units);return true;
}

// Public Page.getFrameTree contains native schema objects, not author values.
// Only Page.Frame.name may consume the private scalar transport representation.
// Everything else is emitted by CEF's ordinary JSON writer without decoding
// arbitrary dictionaries. Counts/depth/output are bounded independently.
inline bool InspectorFrameTreeJSON(CefRefPtr<CefDictionaryValue> reply,std::string& json){
 json.clear();size_t nodes=0;constexpr size_t limit=1048576;
 const auto append=[&json](const std::string& part){if(part.size()>limit-json.size())return false;json+=part;return true;};
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
 const auto tree_json=[&](auto&& self,CefRefPtr<CefDictionaryValue> tree,size_t depth)->bool{
  if(!tree||depth>64||++nodes>4096||tree->GetType("frame")!=VTYPE_DICTIONARY||!append("{"))return false;
  bool first=true;CefDictionaryValue::KeyList keys;tree->GetKeys(keys);
  for(const auto& key:keys){if(!first&&!append(","))return false;first=false;if(!append(key_json(key))||!append(":"))return false;
   if(key=="frame"){if(!frame_json(tree->GetDictionary(key)))return false;}
   else if(key=="childFrames"){
    const auto children=tree->GetList(key);if(!children||children->GetSize()>4096||!append("["))return false;
    for(size_t index=0;index<children->GetSize();++index){if(index&&!append(","))return false;if(children->GetType(index)!=VTYPE_DICTIONARY||!self(self,children->GetDictionary(index),depth+1))return false;}
    if(!append("]"))return false;
   }else if(!ordinary(tree->GetValue(key)))return false;
  }
  return append("}");
 };
 if(!reply||reply->GetType("frameTree")!=VTYPE_DICTIONARY||!append("{"))return false;
 bool first=true;CefDictionaryValue::KeyList keys;reply->GetKeys(keys);
 for(const auto& key:keys){if(!first&&!append(","))return false;first=false;if(!append(key_json(key))||!append(":"))return false;
  if(key=="frameTree"){if(!tree_json(tree_json,reply->GetDictionary(key),0))return false;}
  else if(!ordinary(reply->GetValue(key)))return false;
 }
 return append("}");
}
} // namespace suitecut
