// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
namespace suitecut {
// Only native Inspector DOM.describeNode metadata may populate this record.
// This display caption never participates in handle/backend-node identity.
struct DOMHandlePreviewNode {
  int type=0;
  std::u16string name,local_name,value;
  std::vector<std::pair<std::u16string,std::u16string>> attributes;
  std::vector<std::pair<int,std::u16string>> children;
  bool children_omitted=false;
};
inline std::u16string DOMHandlePreviewText(const std::u16string& input){
  std::u16string text;text.reserve(std::min<size_t>(input.size(),101));
  for(size_t i=0;i<input.size()&&text.size()<=100;++i){
    if(input[i]==u'\r'){if(i+1<input.size()&&input[i+1]==u'\n')++i;text+=u'↵';}
    else if(input[i]==u'\n')text+=u'↵';else text+=input[i];
  }
  if(text.size()>100){
    size_t end=99;
    if(text[end-1]>=0xd800&&text[end-1]<=0xdbff&&text[end]>=0xdc00&&text[end]<=0xdfff)--end;
    text.resize(end);text+=u'…';
  }
  return text;
}
inline std::u16string DOMHandlePreview(const DOMHandlePreviewNode& node){
  if(node.type==3)return u"#text="+DOMHandlePreviewText(node.value);
  if(node.type==8)return u"<!--"+DOMHandlePreviewText(node.value)+u"-->";
  if(node.type!=1)return node.name;
  const auto& tag=node.local_name.empty()?node.name:node.local_name;
  auto attributes=node.attributes;
  // Boolean attributes first, then id; all remaining real names sort lexically.
  std::stable_sort(attributes.begin(),attributes.end(),[](const auto& a,const auto& b){
    const auto rank=[](const auto& attribute){return attribute.second.empty()?0:attribute.first==u"id"?1:2;};
    const int left=rank(a),right=rank(b);return left!=right?left<right:a.first<b.first;
  });
  std::u16string out=u"<"+tag;
  for(const auto& [name,value]:attributes){
    if(name==u"style")continue;
    out+=u" "+name;
    if(!value.empty())out+=u"=\""+value+u"\"";
  }
  static const std::vector<std::u16string> void_tags={u"area",u"base",u"br",u"col",u"embed",u"hr",u"img",u"input",u"link",u"meta",u"param",u"source",u"track",u"wbr"};
  if(std::find(void_tags.begin(),void_tags.end(),tag)!=void_tags.end())return out+u"/>";
  out+=u">";
  bool nested=node.children_omitted;
  std::u16string text;
  for(const auto& [type,value]:node.children){if(type==1)nested=true;else if(type==3)text+=value;}
  out+=nested?u"…":DOMHandlePreviewText(text);
  return out+u"</"+tag+u">";
}
}  // namespace suitecut
