// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <string>
#include "devtools_utf16.h"
namespace suitecut {
// Selected, genuinely typed Inspector scalars only. Author by-value objects are
// never traversed or interpreted as private UTF16 transport tags.
class DeveloperDebuggerEvaluationProjection {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  static bool Build(Dict packet,std::string&output,std::string&error){
    DeveloperDebuggerEvaluationProjection writer;
    if(!packet||packet->GetType("result")!=VTYPE_DICTIONARY){error="Actual paused evaluation result is missing";return false;}
    if(packet->HasKey("exceptionDetails")){
      if(packet->GetType("exceptionDetails")!=VTYPE_DICTIONARY){error="Actual paused evaluation exception metadata is malformed";return false;}
      const auto exception=packet->GetDictionary("exceptionDetails");
      writer.out_="{\"kind\":\"exception\",\"exception\":{\"text\":";
      if(!writer.Text(exception->GetValue("text"),4096)){error="Actual paused evaluation exception text exceeds its bound";return false;}
      for(const auto*key:{"lineNumber","columnNumber"}){
        if(exception->GetType(key)!=VTYPE_INT||exception->GetInt(key)<0){error="Actual paused evaluation exception location is malformed";return false;}
        writer.out_+=std::string(",\"")+key+"\":"+std::to_string(exception->GetInt(key));
      }
      if(exception->HasKey("scriptId")){
        writer.out_+=",\"scriptId\":";
        if(!writer.Text(exception->GetValue("scriptId"),128)){error="Actual paused evaluation exception script identity exceeds its bound";return false;}
      }
      if(exception->HasKey("exception")){
        if(exception->GetType("exception")!=VTYPE_DICTIONARY){error="Actual thrown value metadata is malformed";return false;}
        const auto thrown=exception->GetDictionary("exception");
        if(thrown->HasKey("description")){
          writer.out_+=",\"description\":";
          if(!writer.Text(thrown->GetValue("description"),4096)){error="Actual paused evaluation exception description exceeds its bound";return false;}
        }
        const auto type=thrown->GetType("type")==VTYPE_STRING?thrown->GetString("type").ToString():"";
        if(type=="undefined"||type=="string"||type=="number"||type=="boolean"||type=="bigint"||
           (type=="object"&&thrown->GetType("subtype")==VTYPE_STRING&&thrown->GetString("subtype")=="null")){
          writer.out_+=",\"thrown\":";
          if(!writer.Primitive(thrown)){error="Actual thrown primitive exceeds its supported serialization bound";return false;}
        }
      }
      writer.out_+="}}";
    }else{
      writer.out_="{\"kind\":\"value\",\"value\":";
      if(!writer.Primitive(packet->GetDictionary("result"))){error="Paused evaluation supports bounded primitive by-value results only; remote objects are not adopted";return false;}
      writer.out_+='}';
    }
    if(writer.out_.size()>65536){error="Actual paused evaluation serialized result exceeds65536bytes";return false;}
    output=std::move(writer.out_);return true;
  }
 private:
  std::string out_;
  bool Text(CefRefPtr<CefValue>value,size_t maximum){
    if(!value)return false;
    std::u16string units;
    if(value->GetType()==VTYPE_STRING)units=value->GetString().ToString16();
    else if(!DevToolsReadTag(value,units))return false;
    if(units.size()>maximum)return false;
    const auto quoted=DevToolsQuoteUnits(units);if(out_.size()>65536||quoted.size()>65536-out_.size())return false;
    out_+=quoted;return true;
  }
  bool Primitive(Dict object){
    if(!object||object->GetType("type")!=VTYPE_STRING)return false;
    const auto type=object->GetString("type").ToString();
    if(type=="undefined"){out_+="{\"type\":\"undefined\"}";return true;}
    if(type=="object"&&object->GetType("subtype")==VTYPE_STRING&&object->GetString("subtype")=="null"&&object->GetType("value")==VTYPE_NULL){out_+="{\"type\":\"null\"}";return true;}
    if(type=="string"){out_+="{\"type\":\"string\",\"value\":";if(!Text(object->GetValue("value"),8192))return false;out_+='}';return true;}
    if(type=="boolean"&&object->GetType("value")==VTYPE_BOOL){out_+=object->GetBool("value")?"{\"type\":\"boolean\",\"value\":true}":"{\"type\":\"boolean\",\"value\":false}";return true;}
    if(type=="number"){
      if(object->HasKey("unserializableValue")){
        if(object->GetType("unserializableValue")!=VTYPE_STRING)return false;
        const auto text=object->GetString("unserializableValue").ToString();
        if(text!="NaN"&&text!="-0"&&text!="Infinity"&&text!="-Infinity")return false;
        out_+="{\"type\":\"specialNumber\",\"value\":\""+text+"\"}";return true;
      }
      if(object->GetType("value")!=VTYPE_INT&&object->GetType("value")!=VTYPE_DOUBLE)return false;
      if(object->GetType("value")==VTYPE_DOUBLE&&!std::isfinite(object->GetDouble("value")))return false;
      out_+="{\"type\":\"number\",\"value\":"+CefWriteJSON(object->GetValue("value"),JSON_WRITER_DEFAULT).ToString()+"}";return true;
    }
    if(type=="bigint"&&object->GetType("unserializableValue")==VTYPE_STRING){
      auto text=object->GetString("unserializableValue").ToString();if(text.size()<2||text.size()>1026||text.back()!='n')return false;
      text.pop_back();const size_t start=text.front()=='-'?1:0;
      if(start==text.size()||(text.size()-start>1&&text[start]=='0'))return false;
      for(size_t i=start;i<text.size();++i)if(text[i]<'0'||text[i]>'9')return false;
      out_+="{\"type\":\"bigint\",\"value\":\""+text+"\"}";return true;
    }
    return false;
  }
};
}
