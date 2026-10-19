// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <string>
#include <vector>
#include "include/cef_values.h"
namespace suitecut {
// Materializes actual DOMSnapshot protocol node data as a native HTML tree.
// No script runs in an author realm and no author DOM is mutated.
class TraceDOMTree {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using List=CefRefPtr<CefListValue>;
  static constexpr size_t kNodes=100000,kStrings=1000000;
  struct Document {std::string frame,url,html_json;};
  bool Convert(Dict captured,const std::vector<std::string>& computed_styles,
               std::vector<Document>& documents,std::string& error);
 private:
  std::string String(List strings,List fields,size_t node,bool& ok) const;
  bool Node(size_t index,size_t depth,std::string& json);
  bool Append(std::string& json,const std::string& value);
  List strings_,types_,names_,values_,attributes_;
  std::vector<std::vector<size_t>> children_;
  std::map<size_t,std::string> styles_;
  std::vector<bool> blocked_;
  bool ok_=true;size_t materialized_=0;
};
} // namespace suitecut
