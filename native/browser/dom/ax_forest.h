// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
#include "debugger/devtools_utf16.h"

// Projects only Chromium's actual AX node graph. DOM backend IDs establish the
// requested scope; no role, name, state, or hidden rule is inferred from HTML.
class SuiteCutAXForest {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using List=CefRefPtr<CefListValue>;
  struct Link {int backend=0;Dict node;};
  static void SetRawURL(Dict node,const std::string& href){
    auto properties=node->GetDictionary("properties");
    if(properties)suitecut::DevToolsSetString(properties,"url",href);
    else{properties=CefDictionaryValue::Create();suitecut::DevToolsSetString(properties,"url",href);node->SetDictionary("properties",properties);}
  }
  static bool HasRoot(List nodes,int backend) {
    if(!nodes)return false;
    for(size_t i=0;i<nodes->GetSize();++i){auto node=nodes->GetDictionary(i);if(node&&node->GetInt("backendDOMNodeId")==backend)return true;}
    return false;
  }
  bool Project(List nodes,int backend,List& forest,std::vector<Link>& links,std::string& error) {
    if(!nodes){error="Native accessibility capture unavailable";return false;}
    std::map<std::string,Dict> graph;std::string root;
    for(size_t i=0;i<nodes->GetSize();++i){
      auto node=nodes->GetDictionary(i);if(!node||node->GetType("nodeId")!=VTYPE_STRING){error="Invalid native accessibility node identity";return false;}
      auto id=node->GetString("nodeId").ToString();if(id.empty()){error="Invalid native accessibility node identity";return false;}
      graph[id]=node;if(root.empty()&&node->GetInt("backendDOMNodeId")==backend)root=id;
    }
    if(root.empty()){error="Native accessibility scope is absent from the actual AX tree";return false;}
    // The AX tree depth is the page's DOM depth, not something CEF's JSON
    // parser bounds (getFullAXTree is a flat list), so nothing here recurses:
    // an explicit stack walks the graph, and each projected node collects its
    // children as fresh dictionaries that CEF adopts (moves) into the parent
    // once, instead of deep-copying each subtree at every level.
    struct Frame {std::string id;Dict node;List children;std::vector<Dict> result;size_t next=0;bool entered=false;};
    std::vector<Frame> stack;std::set<std::string> visited,active;std::vector<Dict> top;
    const auto fail=[&](const char* reason){error=reason;for(auto& frame:stack)Release(frame.result);Release(top);return false;};
    stack.push_back({root,graph.at(root),nullptr,{}});
    while(!stack.empty()){
      auto& frame=stack.back();
      if(!frame.entered){
        if(active.contains(frame.id))return fail("Native accessibility tree contains a cycle");
        if(!visited.insert(frame.id).second){stack.pop_back();continue;}
        active.insert(frame.id);frame.entered=true;
        const auto r=Value(frame.node,"role");
        if(Hidden(frame.node)||r=="InlineTextBox"||r=="ListMarker")frame.children=CefListValue::Create();
        else frame.children=frame.node->GetList("childIds");
      }
      if(frame.children&&frame.next<frame.children->GetSize()){
        if(frame.children->GetType(frame.next)!=VTYPE_STRING)return fail("Invalid native accessibility child identity");
        const auto id=frame.children->GetString(frame.next++).ToString();const auto child=graph.find(id);
        // A child document can belong to a different DevTools session. This
        // capture projects the exact selected frame's returned AX graph only.
        if(child!=graph.end())stack.push_back({id,child->second,nullptr,{}});
        continue;
      }
      std::vector<Dict> output;const auto raw_role=Value(frame.node,"role");
      auto layout=frame.node->GetDictionary("__nativeLayout");
      if(layout&&layout->GetBool("block"))SeparateBlockText(frame.result);
      if(!Hidden(frame.node)&&raw_role!="InlineTextBox"&&raw_role!="ListMarker"){
        if(raw_role=="DisclosureTriangle"||frame.node->GetBool("ignored")||raw_role=="generic"||raw_role=="none"||raw_role=="presentation"||raw_role.empty())output=std::move(frame.result);
        else{
          auto node=CefDictionaryValue::Create();
          auto r=raw_role=="StaticText"||raw_role=="LineBreak"?std::string("text"):raw_role=="RootWebArea"||raw_role=="WebArea"?std::string("document"):raw_role=="Iframe"?std::string("iframe"):raw_role=="image"?std::string("img"):raw_role;
          node->SetString("role",r);
          auto box=layout?layout->GetList("box"):nullptr;if(box)node->SetList("box",box->Copy());
          const auto label=Value(frame.node,"name");
          if(r=="text")suitecut::DevToolsSetString(node,"text",raw_role=="LineBreak"?" ":label);
          else{
            suitecut::DevToolsSetString(node,"name",Normalize(label));
            if((r=="textbox"||r=="searchbox")&&frame.node->HasKey("value")){
              const auto value=Value(frame.node,"value");suitecut::DevToolsSetString(node,"text",Normalize(value));
            }
            auto states=CefDictionaryValue::Create(),properties=CefDictionaryValue::Create();auto source=frame.node->GetList("properties");
            if(source){
              for(size_t i=0;i<source->GetSize();++i){
                auto property=source->GetDictionary(i);if(!property)continue;
                const auto key=property->GetString("name").ToString();auto value=property->GetDictionary("value");if(!value)continue;
                if(key=="invalid"){
                  if(value->GetType("value")==VTYPE_BOOL)states->SetBool(key,value->GetBool("value"));
                  else if(value->GetType("value")==VTYPE_STRING){const auto text=value->GetString("value").ToString();if(text=="grammar"||text=="spelling")states->SetString(key,text);else states->SetBool(key,text!="false"&&text!="");}
                }else if(key=="checked"||key=="pressed"||key=="disabled"||key=="expanded"||key=="selected"){
                  if(value->GetType("value")==VTYPE_BOOL)states->SetBool(key,value->GetBool("value"));
                  else if(value->GetType("value")==VTYPE_STRING){const auto text=value->GetString("value").ToString();if(text=="true"||text=="false")states->SetBool(key,text=="true");else if(text=="mixed"&&(key=="checked"||key=="pressed"))states->SetString(key,text);}
                }else if(key=="level"){
                  const auto type=value->GetType("value");const double level=type==VTYPE_INT?value->GetInt("value"):type==VTYPE_DOUBLE?value->GetDouble("value"):0;
                  if(level>=1&&level<=std::numeric_limits<int>::max()&&std::floor(level)==level)states->SetInt("level",static_cast<int>(level));
                }else if(key=="url"&&r=="link"&&(value->GetType("value")==VTYPE_STRING||value->GetType("value")==VTYPE_DICTIONARY)){const auto url=suitecut::DevToolsString(value->GetValue("value"));suitecut::DevToolsSetString(properties,"url",url);}
              }
            }
            // Preserve genuine AX invalid state. The actual DOM token only
            // enriches the missing subtype; public projection may use it when
            // AX invalid is true, never to promote absent/false native state.
            if(layout&&layout->HasKey("invalidToken"))node->SetString("invalidToken",layout->GetString("invalidToken"));
            if(layout&&(r=="textbox"||r=="searchbox")&&layout->HasKey("placeholder")){
              const auto placeholder=suitecut::DevToolsString(layout->GetValue("placeholder"));
              if(Normalize(placeholder)!=Normalize(label))suitecut::DevToolsSetString(properties,"placeholder",placeholder);
            }
            if(states->GetSize())node->SetDictionary("states",states);
            if(properties->GetSize())node->SetDictionary("properties",properties);
            // A sole actual StaticText duplicate of the accessible name or
            // exposed textbox value carries no additional snapshot information.
            if(frame.result.size()==1){auto child=frame.result.front();if(child->GetString("role")=="text"){
              const auto text=Normalize(suitecut::DevToolsString(child->GetValue("text")));
              if((!label.empty()&&text==Normalize(label))||(node->HasKey("text")&&text==suitecut::DevToolsString(node->GetValue("text"))))frame.result.clear();
            }}
            node->SetList("children",Adopt(frame.result));
            if(r=="link"&&frame.node->GetInt("backendDOMNodeId")>0)node->SetInt("__backend",frame.node->GetInt("backendDOMNodeId"));
          }
          output.push_back(node);
        }
      }
      const auto id=frame.id;stack.pop_back();active.erase(id);
      Append(stack.empty()?top:stack.back().result,output);
    }
    forest=Adopt(top);
    // Normalize text after joining adjacent real StaticText nodes, preserving
    // whitespace that spans separate browser text nodes.
    std::vector<List> pending{forest};
    while(!pending.empty()){
      auto list=pending.back();pending.pop_back();
      for(size_t i=0;i<list->GetSize();){const auto node=list->GetDictionary(i);
        if(node->HasKey("__backend")){links.push_back({node->GetInt("__backend"),node});node->Remove("__backend");}
        if(node->GetString("role")=="text"){const auto value=Normalize(suitecut::DevToolsString(node->GetValue("text")));if(value.empty()){list->Remove(i);continue;}suitecut::DevToolsSetString(node,"text",value);}
        auto children=node->GetList("children");if(children)pending.push_back(children);++i;
      }
    }
    return true;
  }
  // base::Value destroys nested values recursively. A deep projected forest is
  // torn down leaf-first here so destruction never recurses with its depth.
  static void Release(List forest){
    if(!forest)return;
    std::vector<List> lists{forest};
    while(!lists.empty()){
      const auto list=lists.back();const auto size=list->GetSize();
      if(!size){lists.pop_back();continue;}
      const auto node=list->GetDictionary(size-1);const auto children=node?node->GetList("children"):nullptr;
      if(children&&children->GetSize())lists.push_back(children);else list->Remove(size-1);
    }
  }
  static void Release(std::vector<Dict>& nodes){Release(Adopt(nodes));}
  static std::string Normalize(const std::string& input){
    std::string out;bool space=false;
    for(size_t i=0;i<input.size();){const auto c=static_cast<unsigned char>(input[i]);size_t width=1;bool whitespace=c==' '||(c>=9&&c<=13),remove=false;
      if(i+1<input.size()&&c==0xc2){const auto b=static_cast<unsigned char>(input[i+1]);if(b==0xa0){whitespace=true;width=2;}else if(b==0xad){remove=true;width=2;}}
      if(i+2<input.size()){
        const auto b=static_cast<unsigned char>(input[i+1]),d=static_cast<unsigned char>(input[i+2]);
        if(c==0xe2&&b==0x80){if((d>=0x80&&d<=0x8a)||d==0xa8||d==0xa9||d==0xaf){whitespace=true;width=3;}else if(d==0x8b){remove=true;width=3;}}
        else if((c==0xe2&&b==0x81&&d==0x9f)||(c==0xe3&&b==0x80&&d==0x80)||(c==0xef&&b==0xbb&&d==0xbf)){whitespace=true;width=3;}
      }
      if(whitespace)space=!out.empty();else if(!remove){if(space)out+=' ';space=false;out.append(input,i,width);}i+=width;
    }
    return out;
  }
 private:
  static std::string Value(Dict node,const char* key){auto value=node->GetDictionary(key);return value?suitecut::DevToolsString(value->GetValue("value")):"";}
  static bool Hidden(Dict node){auto layout=node->GetDictionary("__nativeLayout");if(layout&&layout->GetBool("hidden"))return true;auto reasons=node->GetList("ignoredReasons");if(!reasons)return false;for(size_t i=0;i<reasons->GetSize();++i){auto reason=reasons->GetDictionary(i);if(!reason)continue;const auto name=reason->GetString("name").ToString();if(name=="ariaHiddenElement"||name=="ariaHiddenSubtree"||name=="notRendered"||name=="notVisible"||name=="inertElement"||name=="inertSubtree")return true;}return false;}
  // CEF adopts each fresh dictionary without copying it; the vector is consumed.
  static List Adopt(std::vector<Dict>& nodes){
    auto list=CefListValue::Create();
    for(size_t i=0;i<nodes.size();++i)list->SetDictionary(i,nodes[i]);
    nodes.clear();return list;
  }
  static void SeparateBlockText(const std::vector<Dict>& nodes){
    if(nodes.empty())return;
    const auto& first=nodes.front();const auto& last=nodes.back();
    if(first->GetString("role")=="text")suitecut::DevToolsSetString(first,"text"," "+suitecut::DevToolsString(first->GetValue("text")));
    if(last->GetString("role")=="text")suitecut::DevToolsSetString(last,"text",suitecut::DevToolsString(last->GetValue("text"))+" ");
  }
  static void JoinBoxes(Dict destination,Dict source){
    auto a=destination->GetList("box"),b=source->GetList("box");if(!a||!b||a->GetSize()!=4||b->GetSize()!=4)return;
    const auto number=[](List list,size_t i){return list->GetType(i)==VTYPE_INT?static_cast<double>(list->GetInt(i)):list->GetDouble(i);};
    const double x=std::min(number(a,0),number(b,0)),y=std::min(number(a,1),number(b,1));
    auto box=CefListValue::Create();box->SetDouble(0,x);box->SetDouble(1,y);box->SetDouble(2,std::max(number(a,0)+number(a,2),number(b,0)+number(b,2))-x);box->SetDouble(3,std::max(number(a,1)+number(a,3),number(b,1)+number(b,3))-y);destination->SetList("box",box);
  }
  static void Append(std::vector<Dict>& destination,std::vector<Dict>& source){
    for(auto& node:source){
      if(!destination.empty()&&node->GetString("role")=="text"){const auto& last=destination.back();if(last->GetString("role")=="text"){suitecut::DevToolsSetString(last,"text",suitecut::DevToolsString(last->GetValue("text"))+suitecut::DevToolsString(node->GetValue("text")));JoinBoxes(last,node);continue;}}
      destination.push_back(std::move(node));
    }
    source.clear();
  }
};
