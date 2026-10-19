// SPDX-License-Identifier: MIT
#pragma once
#include "devtools_utf16.h"
#include "dom_handle_preview.h"
namespace suitecut {
inline bool DOMHandlePreviewUnits(CefRefPtr<CefValue> value,std::u16string& units){
  if(value&&value->GetType()==VTYPE_STRING){units=value->GetString().ToString16();return true;}
  return DevToolsReadTag(value,units);
}
inline bool DOMHandlePreviewFromInspector(CefRefPtr<CefDictionaryValue> node,std::string& preview){
  if(!node||node->GetType("nodeType")!=VTYPE_INT)return false;
  DOMHandlePreviewNode record;record.type=node->GetInt("nodeType");
  if(!DOMHandlePreviewUnits(node->GetValue("nodeName"),record.name)||!DOMHandlePreviewUnits(node->GetValue("nodeValue"),record.value))return false;
  if(node->HasKey("localName")&&!DOMHandlePreviewUnits(node->GetValue("localName"),record.local_name))return false;
  if(auto attrs=node->GetList("attributes")){
    if(attrs->GetSize()%2||attrs->GetSize()>200000)return false;
    for(size_t i=0;i<attrs->GetSize();i+=2){std::u16string name,value;if(!DOMHandlePreviewUnits(attrs->GetValue(i),name)||!DOMHandlePreviewUnits(attrs->GetValue(i+1),value))return false;record.attributes.emplace_back(std::move(name),std::move(value));}
  }
  auto children=node->GetList("children");
  record.children_omitted=node->GetType("childNodeCount")==VTYPE_INT&&node->GetInt("childNodeCount")>int(children?children->GetSize():0);
  if(children){
    if(children->GetSize()>100000)return false;
    for(size_t i=0;i<children->GetSize();++i){auto child=children->GetDictionary(i);std::u16string value;if(!child||child->GetType("nodeType")!=VTYPE_INT||!DOMHandlePreviewUnits(child->GetValue("nodeValue"),value))return false;record.children.emplace_back(child->GetInt("nodeType"),std::move(value));}
  }
  preview=DevToolsUnitsWTF8(DOMHandlePreview(record));return true;
}
}  // namespace suitecut
