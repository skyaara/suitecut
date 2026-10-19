// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <charconv>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"

// Transfer actual DOM backend nodes between CEF execution worlds. Every remote
// array/node belongs to the caller's existing native operation object group.
class SuiteCutQueryNodeTransfer {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Result = std::function<void(bool,const std::string&)>;
  using Send = std::function<void(const std::string&,Dict,Result)>;
  using Current = std::function<bool()>;
  using Fail = std::function<void(const std::string&)>;
  using Done = std::function<void(const std::string&)>;
  using NodeDone = std::function<void(Dict)>;
  static void AdoptNode(Send send, Current current, Fail fail, std::string source_node,
                        int destination, std::string group, NodeDone done) {
    auto params=CefDictionaryValue::Create();params->SetString("objectId",source_node);
    send("DOM.describeNode",params,[send,current,fail,destination,group,done](bool ok,const std::string& json){
      if(!current()){fail("Native query document changed during node transfer");return;}
      auto result=Parse(json);auto node=result?result->GetDictionary("node"):nullptr;
      if(!ok||!node||node->GetType("backendNodeId")!=VTYPE_INT){fail("Native selector backend node unavailable");return;}
      auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",node->GetInt("backendNodeId"));
      params->SetInt("executionContextId",destination);params->SetString("objectGroup",group);
      send("DOM.resolveNode",params,[current,fail,done](bool ok,const std::string& json){
        if(!current()){fail("Native query document changed during node transfer");return;}
        auto result=Parse(json);auto node=result?result->GetDictionary("object"):nullptr;
        if(!ok||!node||node->GetType("objectId")!=VTYPE_STRING||node->GetString("subtype")!="node"){fail("Native selector DOM node adoption failed");return;}
        done(node);
      });
    });
  }
  static void Transfer(Send send, Current current, Fail fail, std::string source_array,
                       int destination, std::string group, Done done) {
    auto work=std::make_shared<Work>();work->send=std::move(send);work->current=std::move(current);
    work->fail=std::move(fail);work->done=std::move(done);work->destination=destination;work->group=std::move(group);work->source=source_array;
    auto params=CefDictionaryValue::Create();params->SetString("objectId",source_array);params->SetBool("ownProperties",true);
    work->send("Runtime.getProperties",params,[work](bool ok,const std::string& json){
      if(!Valid(work))return;
      auto result=Parse(json);auto fields=result?result->GetList("result"):nullptr;
      if(!ok||!fields){work->fail("Native isolated query array metadata unavailable");return;}
      std::map<size_t,std::string> nodes;
      for(size_t i=0;i<fields->GetSize();++i){
        auto field=fields->GetDictionary(i);if(!field)continue;
        const auto name=field->GetString("name").ToString();size_t index=0;
        const auto parsed=std::from_chars(name.data(),name.data()+name.size(),index);
        if(parsed.ec!=std::errc()||parsed.ptr!=name.data()+name.size())continue;
        if(index>=100000){work->fail("Native query node transfer limit exceeded");return;}
        auto value=field->GetDictionary("value");
        if(!value||value->GetString("subtype")!="node"||value->GetType("objectId")!=VTYPE_STRING){work->fail("Native isolated selector did not return DOM nodes");return;}
        nodes[index]=value->GetString("objectId").ToString();
      }
      for(const auto& [index,node]:nodes){if(index!=work->nodes.size()){work->fail("Native isolated query array is sparse");return;}work->nodes.push_back(node);}
      if(work->nodes.size()<128){CreateArray(work);return;}
      // Inspector deep serialization identifies these exact privately retained
      // nodes in one response. DOM attributes/text are not consumed. Invalid or
      // unsupported metadata falls back to each original node's real describe.
      auto params=CefDictionaryValue::Create();params->SetString("objectId",work->source);
      params->SetString("functionDeclaration","function(){return this;}");params->SetString("objectGroup",work->group);
      auto options=CefDictionaryValue::Create();options->SetString("serialization","deep");options->SetInt("maxDepth",1);
      auto additional=CefDictionaryValue::Create();additional->SetInt("maxNodeDepth",0);options->SetDictionary("additionalParameters",additional);params->SetDictionary("serializationOptions",options);
      work->send("Runtime.callFunctionOn",params,[work](bool ok,const std::string& json){
        if(!Valid(work))return;
        auto result=json.size()<=16*1048576?Parse(json):nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
        auto deep=remote&&remote->GetString("subtype")=="array"?remote->GetDictionary("deepSerializedValue"):nullptr;
        auto values=deep&&deep->GetString("type")=="array"?deep->GetList("value"):nullptr;
        if(ok&&result&&!result->HasKey("exceptionDetails")&&values&&values->GetSize()==work->nodes.size()){
          std::vector<int> backends;std::map<int,int> references;bool valid=true;
          for(size_t i=0;i<values->GetSize();++i){
            auto value=values->GetDictionary(i);if(!value||value->GetString("type")!="node"){valid=false;break;}
            if(value->HasKey("value")&&value->GetType("value")!=VTYPE_DICTIONARY){valid=false;break;}
            auto node=value->GetDictionary("value");int backend=0;
            const bool has_reference=value->HasKey("weakLocalObjectReference");
            const int reference=value->GetInt("weakLocalObjectReference");
            if(has_reference&&(value->GetType("weakLocalObjectReference")!=VTYPE_INT||reference<0)){valid=false;break;}
            if(node&&node->GetType("nodeType")==VTYPE_INT&&node->GetInt("nodeType")>0&&node->GetInt("nodeType")<=12&&node->GetType("backendNodeId")==VTYPE_INT&&node->GetInt("backendNodeId")>0){backend=node->GetInt("backendNodeId");if(has_reference){const auto [found,added]=references.emplace(reference,backend);if(!added&&found->second!=backend){valid=false;break;}}}
            else if(!node&&has_reference){const auto found=references.find(reference);if(found!=references.end())backend=found->second;}
            if(!backend){valid=false;break;}backends.push_back(backend);
          }
          if(valid)work->backends=std::move(backends);
        }
        CreateArray(work);
      });
    });
  }
 private:
  struct Work { Send send;Current current;Fail fail;Done done;int destination;std::string group,array,source;size_t index=0;std::vector<std::string> nodes,resolved;std::vector<int>backends; };
  static Dict Parse(const std::string& json){auto value=CefParseJSON(json,JSON_PARSER_RFC);return value?value->GetDictionary():nullptr;}
  static bool Valid(const std::shared_ptr<Work>& work){if(work->current())return true;work->fail("Native query document changed during node transfer");return false;}
  static void CreateArray(const std::shared_ptr<Work>& work){
    if(!Valid(work))return;
    // Literal slots are own writable data properties even when the author
    // installed numeric setters on Array.prototype. No author intrinsic is
    // called to create them; the real DOM wrappers replace only these slots.
    std::string slots="[";for(size_t i=0;i<work->nodes.size();++i)slots+="void 0,";slots+="]";
    auto params=CefDictionaryValue::Create();params->SetString("expression",slots);params->SetInt("contextId",work->destination);params->SetString("objectGroup",work->group);
    work->send("Runtime.evaluate",params,[work](bool ok,const std::string& json){
      if(!Valid(work))return;
      auto result=Parse(json);auto array=result?result->GetDictionary("result"):nullptr;
      if(!ok||!array||result->HasKey("exceptionDetails")||array->GetType("objectId")!=VTYPE_STRING){work->fail("Native main-world query array creation failed");return;}
      work->array=array->GetString("objectId").ToString();Next(work);
    });
  }
  static void Resolve(const std::shared_ptr<Work>& work,int backend){
    if(!Valid(work))return;
    auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",backend);
    params->SetInt("executionContextId",work->destination);params->SetString("objectGroup",work->group);
    work->send("DOM.resolveNode",params,[work](bool ok,const std::string& json){
      if(!Valid(work))return;
      auto result=Parse(json);auto node=result?result->GetDictionary("object"):nullptr;
      if(!ok||!node||node->GetType("objectId")!=VTYPE_STRING||node->GetString("subtype")!="node"){work->fail("Native isolated node adoption into the main world failed");return;}
      work->resolved.push_back(node->GetString("objectId").ToString());++work->index;Next(work);
    });
  }
  static void Append(const std::shared_ptr<Work>& work){
    if(!Valid(work))return;
    auto params=CefDictionaryValue::Create();params->SetString("objectId",work->array);
    params->SetString("functionDeclaration","function(offset,...nodes){for(let i=0;i<nodes.length;i++)this[offset+i]=nodes[i];}");
    auto args=CefListValue::Create();auto offset=CefDictionaryValue::Create();offset->SetInt("value",static_cast<int>(work->index-work->resolved.size()));args->SetDictionary(0,offset);
    for(size_t i=0;i<work->resolved.size();++i){auto arg=CefDictionaryValue::Create();arg->SetString("objectId",work->resolved[i]);args->SetDictionary(i+1,arg);}
    work->resolved.clear();params->SetList("arguments",args);params->SetBool("returnByValue",true);params->SetString("objectGroup",work->group);
    work->send("Runtime.callFunctionOn",params,[work](bool ok,const std::string& json){
      if(!Valid(work))return;
      auto result=Parse(json);
      if(!ok||!result||result->HasKey("exceptionDetails")){work->fail("Native transferred query nodes could not be appended");return;}
      if(work->index==work->nodes.size())work->done(work->array);else Next(work);
    });
  }
  static void Next(const std::shared_ptr<Work>& work){
    if(!Valid(work))return;
    if(work->index==work->nodes.size()){if(work->resolved.empty())work->done(work->array);else Append(work);return;}
    if(work->resolved.size()>=128){Append(work);return;}
    if(!work->backends.empty()){Resolve(work,work->backends[work->index]);return;}
    auto params=CefDictionaryValue::Create();params->SetString("objectId",work->nodes[work->index]);
    work->send("DOM.describeNode",params,[work](bool ok,const std::string& json){
      if(!Valid(work))return;
      auto result=Parse(json);auto node=result?result->GetDictionary("node"):nullptr;
      if(!ok||!node||node->GetType("backendNodeId")!=VTYPE_INT){work->fail("Native isolated query backend node unavailable");return;}
      Resolve(work,node->GetInt("backendNodeId"));
    });
  }
};
