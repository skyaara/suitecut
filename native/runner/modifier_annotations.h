// SPDX-License-Identifier: MIT
#pragma once
#include "runtime_skip.h"
#include <optional>
#include <vector>

namespace suitecut::runner {
struct ModifierLocation {
  std::string file_json="\"\"",file_wire="-";uint32_t line=0,column=0;
  bool operator==(const ModifierLocation&) const = default;
};
struct ModifierAnnotation {
  std::string type,reason_json="\"\"",reason_wire="-";bool description_present=false;
  std::optional<ModifierLocation> location=std::nullopt;
  bool operator==(const ModifierAnnotation&) const = default;
};
inline void ValidateModifierAnnotation(const ModifierAnnotation& value){
  if((value.type!="skip"&&value.type!="fail")||!JsonString(value.reason_json,8192)||RuntimeSkipReason(value.reason_wire)!=value.reason_json||(!value.description_present&&value.reason_wire!="-"))throw std::invalid_argument("Invalid native modifier annotation");
  if(value.location){const auto& location=*value.location;if(!location.line||!location.column||location.line>2147483647||location.column>2147483647||location.file_wire=="-"||!JsonString(location.file_json,4096)||RuntimeSkipReason(location.file_wire)!=location.file_json)throw std::invalid_argument("Invalid native modifier location");}
}
inline std::string ModifierAnnotationWire(const ModifierAnnotation& value){
  return value.type+" "+(value.description_present?"1":"0")+" "+value.reason_wire+" "+(value.location?value.location->file_wire:"-")+" "+std::to_string(value.location?value.location->line:0)+" "+std::to_string(value.location?value.location->column:0);
}
inline std::string ModifierAnnotationJSON(const ModifierAnnotation& value){
  std::string out="{\"type\":\""+value.type+"\"";
  if(value.description_present)out+=",\"description\":"+value.reason_json;
  if(value.location)out+=",\"location\":{\"file\":"+value.location->file_json+",\"line\":"+std::to_string(value.location->line)+",\"column\":"+std::to_string(value.location->column)+"}";
  return out+"}";
}
class ModifierAnnotations {
 public:
  static constexpr size_t kCount=256,kWireBytes=48*1024;
  static ModifierAnnotation Prepare(std::string type,const std::string& reason_wire,bool description_present,const std::string& file_wire,uint64_t line,uint64_t column){
    ModifierAnnotation value{std::move(type),RuntimeSkipReason(reason_wire),reason_wire,description_present};
    if(file_wire!="-"||line||column){if(line>2147483647||column>2147483647)throw std::invalid_argument("Native modifier source location exceeds integer bound");value.location=ModifierLocation{RuntimeSkipReason(file_wire),file_wire,static_cast<uint32_t>(line),static_cast<uint32_t>(column)};}
    ValidateModifierAnnotation(value);return value;
  }
  bool CanAppend(const ModifierAnnotation& value)const{
    ValidateModifierAnnotation(value);return values_.size()<kCount&&ModifierAnnotationWire(value).size()+1<=kWireBytes-wire_bytes_;
  }
  void Append(ModifierAnnotation value){
    if(!CanAppend(value))throw std::length_error("Native modifier annotation capacity exceeded");
    wire_bytes_+=ModifierAnnotationWire(value).size()+1;values_.push_back(std::move(value));
  }
  void Clear(){values_.clear();wire_bytes_=0;}
  const std::vector<ModifierAnnotation>& Values()const{return values_;}
  std::string Snapshot()const{std::string out=std::to_string(values_.size());for(const auto& value:values_)out+=" "+ModifierAnnotationWire(value);return out;}
 private:std::vector<ModifierAnnotation> values_;size_t wire_bytes_=0;
};
} // namespace suitecut::runner
