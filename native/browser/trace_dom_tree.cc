// SPDX-License-Identifier: MIT
#include "trace_dom_tree.h"
#include "devtools_utf16.h"
#include <functional>
#include <utility>
namespace suitecut {
namespace {
// Scalar selection is explicit. Author attribute dictionaries are never passed
// to the private tagged-value serializer, including a literal attribute named
// __suitecutNativeUTF16Scalar. Keys retain the original UTF16 units too.
std::string Quote(const std::string& text){auto holder=CefDictionaryValue::Create();DevToolsSetString(holder,"value",text);const auto value=holder->GetValue("value");std::u16string units;return DevToolsReadTag(value,units)?DevToolsQuoteUnits(units):CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();}
}
std::string TraceDOMTree::String(List strings,List fields,size_t node,bool& ok) const {if(!fields||node>=fields->GetSize()||fields->GetType(node)!=VTYPE_INT){ok=false;return {};}const int index=fields->GetInt(node);if(index==-1)return {};if(index<0||size_t(index)>=strings->GetSize()){ok=false;return {};}const auto value=strings->GetValue(index);std::u16string units;if(value->GetType()!=VTYPE_STRING&&!DevToolsReadTag(value,units)){ok=false;return {};}return DevToolsString(value);}
bool TraceDOMTree::Append(std::string& json,const std::string& value){if(value.size()>16*1024*1024-materialized_){ok_=false;return false;}json+=value;materialized_+=value.size();return true;}
bool TraceDOMTree::Node(size_t index,size_t depth,std::string& json){
  if(!ok_||depth>512||index>=children_.size()){ok_=false;return false;}if(blocked_[index])return true;const auto type=types_->GetInt(index);
  if(type==3)return Append(json,Quote(String(strings_,values_,index,ok_)));
  if(type!=1&&type!=9&&type!=11)return true;
  if(!Append(json,"["+Quote(type==1?String(strings_,names_,index,ok_):type==9?"#document":"#shadow-root")+",{"))return false;
  bool comma=false;
  if(type==1){auto pairs=attributes_->GetList(index);if(!pairs||pairs->GetSize()%2){ok_=false;return false;}for(size_t i=0;i<pairs->GetSize();i+=2){const auto name=String(strings_,pairs,i,ok_),text=String(strings_,pairs,i+1,ok_);if(!ok_||name.empty()){ok_=false;return false;}if(name=="style"&&styles_.contains(index))continue;if(!Append(json,(comma?",":"")+Quote(name)+":"+Quote(text)))return false;comma=true;}if(const auto style=styles_.find(index);style!=styles_.end()){if(!Append(json,std::string(comma?",":"")+"\"style\":"+Quote(style->second)))return false;}}
  if(!Append(json,"}"))return false;
  for(const auto child:children_[index]){const auto type=types_->GetInt(child);if(blocked_[child]||(type!=1&&type!=3&&type!=9&&type!=11))continue;if(!Append(json,",")||!Node(child,depth+1,json))return false;}
  return Append(json,"]");
}
bool TraceDOMTree::Convert(Dict captured,const std::vector<std::string>& computed_styles,std::vector<Document>& output,std::string& error){
  output.clear();strings_=captured?captured->GetList("strings"):nullptr;auto documents=captured?captured->GetList("documents"):nullptr;
  if(!strings_||strings_->GetSize()>kStrings||!documents||documents->GetSize()>1024||computed_styles.size()>128){error="Invalid or excessive native trace DOM snapshot";return false;}
  size_t total_nodes=0;materialized_=0;std::vector<bool> blocked_documents(documents->GetSize(),false);
  for(size_t d=0;d<documents->GetSize();++d){if(blocked_documents[d])continue;auto document=documents->GetDictionary(d);auto nodes=document?document->GetDictionary("nodes"):nullptr;
    types_=nodes?nodes->GetList("nodeType"):nullptr;names_=nodes?nodes->GetList("nodeName"):nullptr;values_=nodes?nodes->GetList("nodeValue"):nullptr;attributes_=nodes?nodes->GetList("attributes"):nullptr;auto parents=nodes?nodes->GetList("parentIndex"):nullptr;
    const size_t count=types_?types_->GetSize():0;if(!count||count>kNodes||!names_||names_->GetSize()!=count||!values_||values_->GetSize()!=count||!attributes_||attributes_->GetSize()!=count||!parents||parents->GetSize()!=count){error="Invalid native trace DOM node table";return false;}
    total_nodes+=count;if(total_nodes>kNodes){error="Native trace document node capacity exceeded";return false;}
    children_.assign(count,{});blocked_.assign(count,false);styles_.clear();ok_=true;
    for(size_t i=0;i<count;++i){if(types_->GetType(i)!=VTYPE_INT||parents->GetType(i)!=VTYPE_INT){ok_=false;break;}const int parent=parents->GetInt(i);if((i==0&&parent!=-1)||(i>0&&(parent<0||size_t(parent)>=i))){ok_=false;break;}if(i)children_[parent].push_back(i);}
    // Skip genuine iframe nodes under the actual HEAD tree, including shadows.
    for(size_t i=0;i<count&&ok_;++i){const auto name=String(strings_,names_,i,ok_);const int parent=parents->GetInt(i);if(parent>=0)blocked_[i]=blocked_[parent];if(name=="IFRAME"){for(int ancestor=parent;ancestor>=0;ancestor=parents->GetInt(ancestor))if(String(strings_,names_,size_t(ancestor),ok_)=="HEAD"){blocked_[i]=true;break;}}}
    // CDP identifies actual embedded documents by document-table index.
    // A head iframe's descendant document cannot become a rendered snapshot.
    if(auto embedded=nodes->GetDictionary("contentDocumentIndex")){auto indices=embedded->GetList("index"),values=embedded->GetList("value");if(!indices||!values||indices->GetSize()!=values->GetSize())ok_=false;else for(size_t i=0;i<indices->GetSize()&&ok_;++i){const int node=indices->GetInt(i),child=values->GetInt(i);if(indices->GetType(i)!=VTYPE_INT||values->GetType(i)!=VTYPE_INT||node<0||size_t(node)>=count||child<0||size_t(child)>=documents->GetSize()||size_t(child)<=d){ok_=false;break;}if(blocked_[node])blocked_documents[child]=true;}}
    auto layout=document->GetDictionary("layout");auto indices=layout?layout->GetList("nodeIndex"):nullptr;auto styles=layout?layout->GetList("styles"):nullptr;
    if((indices&&!styles)||(!indices&&styles)||(indices&&indices->GetSize()!=styles->GetSize()))ok_=false;
    if(indices)for(size_t i=0;i<indices->GetSize()&&ok_;++i){const int index=indices->GetInt(i);auto row=styles->GetList(i);if(indices->GetType(i)!=VTYPE_INT||index<0||size_t(index)>=count||!row||row->GetSize()>computed_styles.size()){ok_=false;break;}std::string css;for(size_t j=0;j<row->GetSize();++j){const auto text=String(strings_,row,j,ok_);if(!text.empty())css+=computed_styles[j]+":"+text+";";}if(!css.empty())styles_[index]=std::move(css);}
    std::string tree;if(ok_)Node(0,0,tree);const int frame=document->GetInt("frameId"),url=document->GetInt("documentURL");if(!ok_||tree.empty()||document->GetType("frameId")!=VTYPE_INT||frame<0||size_t(frame)>=strings_->GetSize()||document->GetType("documentURL")!=VTYPE_INT||url<0||size_t(url)>=strings_->GetSize()){error="Invalid native trace DOM tree";return false;}
    const auto frame_value=strings_->GetValue(frame),url_value=strings_->GetValue(url);std::u16string units;if(frame_value->GetType()!=VTYPE_STRING||(url_value->GetType()!=VTYPE_STRING&&!DevToolsReadTag(url_value,units))){error="Invalid native trace document identity";return false;}output.push_back({DevToolsString(frame_value),DevToolsString(url_value),std::move(tree)});
  }
  strings_=nullptr;types_=nullptr;names_=nullptr;values_=nullptr;attributes_=nullptr;children_.clear();blocked_.clear();styles_.clear();return true;
}
} // namespace suitecut
