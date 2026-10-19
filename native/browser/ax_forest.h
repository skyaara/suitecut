// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
#include "devtools_utf16.h"

// Projects only Chromium's actual AX node graph. DOM backend IDs establish the
// requested scope; no role, name, state, or hidden rule is inferred from HTML.
class SuiteCutAXForest {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using List=CefRefPtr<CefListValue>;
  struct Link {int backend=0;Dict node;};
  static constexpr size_t kCaptureNodes=100000,kOutputNodes=4096,kOutputBytes=65536,kWork=1000000;
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
    if(!nodes||nodes->GetSize()>kCaptureNodes){error="Native accessibility capture node limit exceeded";return false;}
    std::map<std::string,Dict> graph;std::string root;
    for(size_t i=0;i<nodes->GetSize();++i){
      if(!Tick(error))return false;
      auto node=nodes->GetDictionary(i);if(!node||node->GetType("nodeId")!=VTYPE_STRING){error="Invalid native accessibility node identity";return false;}
      auto id=node->GetString("nodeId").ToString();if(id.empty()||id.size()>128){error="Invalid native accessibility node identity";return false;}
      graph[id]=node;if(root.empty()&&node->GetInt("backendDOMNodeId")==backend)root=id;
    }
    if(root.empty()){error="Native accessibility scope is absent from the actual AX tree";return false;}
    struct Frame {std::string id;Dict node;List children,result;size_t next=0;bool entered=false;};
    std::vector<Frame> stack;std::set<std::string> visited,active;
    stack.push_back({root,graph.at(root),nullptr,CefListValue::Create()});
    forest=CefListValue::Create();
    while(!stack.empty()){
      if(!Tick(error))return false;
      auto& frame=stack.back();
      if(!frame.entered){
        if(stack.size()>64){error="Native accessibility snapshot depth limit exceeded";return false;}
        if(active.contains(frame.id)){error="Native accessibility tree contains a cycle";return false;}
        if(!visited.insert(frame.id).second){stack.pop_back();continue;}
        active.insert(frame.id);frame.entered=true;
        const auto r=Value(frame.node,"role");
        if(Hidden(frame.node)||r=="InlineTextBox"||r=="ListMarker")frame.children=CefListValue::Create();
        else frame.children=frame.node->GetList("childIds");
      }
      if(frame.children&&frame.next<frame.children->GetSize()){
        if(frame.children->GetType(frame.next)!=VTYPE_STRING){error="Invalid native accessibility child identity";return false;}
        const auto id=frame.children->GetString(frame.next++).ToString();const auto child=graph.find(id);
        // A child document can belong to a different DevTools session. This
        // capture projects the exact selected frame's returned AX graph only.
        if(child!=graph.end())stack.push_back({id,child->second,nullptr,CefListValue::Create()});
        continue;
      }
      auto output=CefListValue::Create();const auto raw_role=Value(frame.node,"role");
      auto layout=frame.node->GetDictionary("__nativeLayout");
      if(layout&&layout->GetBool("block"))SeparateBlockText(frame.result);
      if(!Hidden(frame.node)&&raw_role!="InlineTextBox"&&raw_role!="ListMarker"){
        if(raw_role=="DisclosureTriangle"||frame.node->GetBool("ignored")||raw_role=="generic"||raw_role=="none"||raw_role=="presentation"||raw_role.empty())output=frame.result;
        else{
          if(++output_nodes_>kOutputNodes){error="Native accessibility snapshot node limit exceeded";return false;}
          auto node=CefDictionaryValue::Create();
          auto r=raw_role=="StaticText"||raw_role=="LineBreak"?std::string("text"):raw_role=="RootWebArea"||raw_role=="WebArea"?std::string("document"):raw_role=="Iframe"?std::string("iframe"):raw_role=="image"?std::string("img"):raw_role;
          node->SetString("role",r);
          auto box=layout?layout->GetList("box"):nullptr;if(box)node->SetList("box",box->Copy());
          const auto label=Value(frame.node,"name");if(!Budget(label.size()+r.size(),error))return false;
          if(r=="text")suitecut::DevToolsSetString(node,"text",raw_role=="LineBreak"?" ":label);
          else{
            suitecut::DevToolsSetString(node,"name",Normalize(label));
            if((r=="textbox"||r=="searchbox")&&frame.node->HasKey("value")){
              const auto value=Value(frame.node,"value");if(!Budget(value.size(),error))return false;suitecut::DevToolsSetString(node,"text",Normalize(value));
            }
            auto states=CefDictionaryValue::Create(),properties=CefDictionaryValue::Create();auto source=frame.node->GetList("properties");
            if(source){if(source->GetSize()>256){error="Native accessibility property limit exceeded";return false;}
              for(size_t i=0;i<source->GetSize();++i){
                if(!Tick(error))return false;
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
                  if(level>=1&&level<=100000&&std::floor(level)==level)states->SetInt("level",static_cast<int>(level));
                }else if(key=="url"&&r=="link"&&(value->GetType("value")==VTYPE_STRING||value->GetType("value")==VTYPE_DICTIONARY)){const auto url=suitecut::DevToolsString(value->GetValue("value"));if(!Budget(url.size(),error))return false;suitecut::DevToolsSetString(properties,"url",url);}
              }
            }
            // Preserve genuine AX invalid state. The actual DOM token only
            // enriches the missing subtype; public projection may use it when
            // AX invalid is true, never to promote absent/false native state.
            if(layout&&layout->HasKey("invalidToken"))node->SetString("invalidToken",layout->GetString("invalidToken"));
            if(layout&&(r=="textbox"||r=="searchbox")&&layout->HasKey("placeholder")){
              const auto placeholder=suitecut::DevToolsString(layout->GetValue("placeholder"));if(!Budget(placeholder.size(),error))return false;
              if(Normalize(placeholder)!=Normalize(label))suitecut::DevToolsSetString(properties,"placeholder",placeholder);
            }
            if(states->GetSize())node->SetDictionary("states",states);
            if(properties->GetSize())node->SetDictionary("properties",properties);
            // A sole actual StaticText duplicate of the accessible name or
            // exposed textbox value carries no additional snapshot information.
            if(frame.result->GetSize()==1){auto child=frame.result->GetDictionary(0);if(child&&child->GetString("role")=="text"){
              const auto text=Normalize(suitecut::DevToolsString(child->GetValue("text")));
              if((!label.empty()&&text==Normalize(label))||(node->HasKey("text")&&text==suitecut::DevToolsString(node->GetValue("text"))))frame.result=CefListValue::Create();
            }}
            node->SetList("children",frame.result);
            if(r=="link"&&frame.node->GetInt("backendDOMNodeId")>0)node->SetInt("__backend",frame.node->GetInt("backendDOMNodeId"));
          }
          output->SetDictionary(0,node);
        }
      }
      const auto id=frame.id;stack.pop_back();active.erase(id);
      auto destination=stack.empty()?forest:stack.back().result;
      if(!Append(destination,output,error))return false;
    }
    // Normalize text after joining adjacent real StaticText nodes, preserving
    // whitespace that spans separate browser text nodes.
    std::vector<List> pending{forest};size_t projected=0;
    while(!pending.empty()){
      auto list=pending.back();pending.pop_back();
      for(size_t i=0;i<list->GetSize();){if(++projected>kOutputNodes){error="Native accessibility snapshot node limit exceeded";return false;}auto node=list->GetDictionary(i);
        if(node->HasKey("__backend")){links.push_back({node->GetInt("__backend"),node});node->Remove("__backend");}
        if(node->GetString("role")=="text"){const auto value=Normalize(suitecut::DevToolsString(node->GetValue("text")));if(value.empty()){list->Remove(i);continue;}suitecut::DevToolsSetString(node,"text",value);}
        auto children=node->GetList("children");if(children)pending.push_back(children);++i;
      }
    }
    auto value=CefValue::Create();value->SetList(forest->Copy());
    std::string lossless;
    if(!suitecut::DevToolsLosslessJSON(value,lossless,kOutputBytes)){error="Native accessibility snapshot exceeds 64 KiB";return false;}
    return true;
  }
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
  static void SeparateBlockText(List nodes){
    if(!nodes||!nodes->GetSize())return;
    auto first=nodes->GetDictionary(0),last=nodes->GetDictionary(nodes->GetSize()-1);
    if(first&&first->GetString("role")=="text")suitecut::DevToolsSetString(first,"text"," "+suitecut::DevToolsString(first->GetValue("text")));
    if(last&&last->GetString("role")=="text")suitecut::DevToolsSetString(last,"text",suitecut::DevToolsString(last->GetValue("text"))+" ");
  }
  static void JoinBoxes(Dict destination,Dict source){
    auto a=destination->GetList("box"),b=source->GetList("box");if(!a||!b||a->GetSize()!=4||b->GetSize()!=4)return;
    const auto number=[](List list,size_t i){return list->GetType(i)==VTYPE_INT?static_cast<double>(list->GetInt(i)):list->GetDouble(i);};
    const double x=std::min(number(a,0),number(b,0)),y=std::min(number(a,1),number(b,1));
    auto box=CefListValue::Create();box->SetDouble(0,x);box->SetDouble(1,y);box->SetDouble(2,std::max(number(a,0)+number(a,2),number(b,0)+number(b,2))-x);box->SetDouble(3,std::max(number(a,1)+number(a,3),number(b,1)+number(b,3))-y);destination->SetList("box",box);
  }
  bool Tick(std::string& error){if(++work_<=kWork)return true;error="Native accessibility projection work limit exceeded";return false;}
  bool Budget(size_t bytes,std::string& error){bytes_+=bytes;if(bytes_<=kOutputBytes)return true;error="Native accessibility snapshot exceeds 64 KiB";return false;}
  bool Append(List destination,List source,std::string& error){
    for(size_t i=0;i<source->GetSize();++i){if(!Tick(error))return false;auto node=source->GetDictionary(i);const auto size=destination->GetSize();
      if(size&&node->GetString("role")=="text"){auto last=destination->GetDictionary(size-1);if(last->GetString("role")=="text"){suitecut::DevToolsSetString(last,"text",suitecut::DevToolsString(last->GetValue("text"))+suitecut::DevToolsString(node->GetValue("text")));JoinBoxes(last,node);continue;}}
      destination->SetDictionary(size,node->Copy(false));
    }
    return true;
  }
  size_t work_=0,bytes_=0,output_nodes_=0;
};
