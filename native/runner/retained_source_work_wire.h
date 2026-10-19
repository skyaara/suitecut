// SPDX-License-Identifier: MIT
#pragma once
#include "../browser/accepted_stream_work.h"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
namespace suitecut::runner {
// Exact native process-channel identities. These are not SDK-supplied counters;
// Decode is used only after actual SCM credentials + process start are checked.
class RetainedSourceWorkWire {
 public:
  struct Challenge {
    uint64_t ticket=0,nonce=0,borrow_lease=0,borrow_generation=0;
    std::string deadline_text;double deadline=0;
    bool operator==(const Challenge&)const=default;
  };
  struct Borrow {uint64_t lease=0,generation=0;unsigned worker=0;int64_t pid=0;uint64_t start=0;};
  struct Cuts {
    uint64_t ticket=0,nonce=0;NativeStreamCut reader,writer;
    bool operator==(const Cuts&)const=default;
  };
  struct Capability {uint64_t ticket=0;int64_t pid=0;uint64_t start=0;};
  struct Resume {uint64_t ticket=0,nonce=0,generation=0;};
  enum class Kind {Install,Capability,Begin,Cut,Observed,Acknowledged,Resume,Resumed,Unsupported};
  struct Message {Kind kind;Capability capability;Challenge challenge;Cuts cuts;Resume resume;uint64_t ticket=0,nonce=0,reason=0;};
  static Challenge Issue(uint64_t ticket,uint64_t nonce,uint64_t lease,uint64_t generation,double deadline){
    if(!ticket||!nonce||!lease||!generation||!std::isfinite(deadline)||deadline<=0)throw std::invalid_argument("Native Source challenge identity/deadline invalid");
    std::array<char,64> text{};const auto encoded=std::to_chars(text.data(),text.data()+text.size(),deadline,std::chars_format::general,std::numeric_limits<double>::max_digits10);
    if(encoded.ec!=std::errc{})throw std::runtime_error("Native Source original deadline serialization failed");
    return {ticket,nonce,lease,generation,std::string(text.data(),encoded.ptr),deadline};
  }
  static std::string Encode(const Challenge& value){
    const auto actual=Issue(value.ticket,value.nonce,value.borrow_lease,value.borrow_generation,value.deadline);
    if(actual.deadline_text!=value.deadline_text)throw std::invalid_argument("Native Source original challenge deadline changed");
    return "SOURCE_WORK_BEGIN 1 "+N(value.ticket)+" "+N(value.nonce)+" "+N(value.borrow_lease)+" "+N(value.borrow_generation)+" "+value.deadline_text+"\n";
  }
  static std::string Encode(Kind kind,const Cuts& value){
    if(kind!=Kind::Cut&&kind!=Kind::Observed&&kind!=Kind::Acknowledged)throw std::invalid_argument("Native Source cut packet kind invalid");
    if(!value.ticket||!value.nonce||!value.reader.instance||!value.reader.nonce||!value.writer.instance||!value.writer.nonce)throw std::invalid_argument("Native Source issued cut identity invalid");
    const char* name=kind==Kind::Cut?"SOURCE_WORK_CUT":kind==Kind::Observed?"SOURCE_WORK_OBSERVED":"SOURCE_WORK_ACK";
    return std::string(name)+" 1 "+N(value.ticket)+" "+N(value.nonce)+" "+N(value.reader.instance)+" "+N(value.reader.sequence)+" "+N(value.reader.nonce)+" "+N(value.writer.instance)+" "+N(value.writer.sequence)+" "+N(value.writer.nonce)+"\n";
  }
  static std::string Install(uint64_t ticket){if(!ticket)throw std::invalid_argument("Native Source install ticket invalid");return "SOURCE_WORK_INSTALL 1 "+N(ticket)+"\n";}
  static std::string Encode(const Capability& value){if(!value.ticket||value.pid<=0||value.pid>INT32_MAX||!value.start)throw std::invalid_argument("Native Source capability identity invalid");return "SOURCE_WORK_CAP 1 "+N(value.ticket)+" "+N(static_cast<uint64_t>(value.pid))+" "+N(value.start)+"\n";}
  static std::string Encode(const Resume& value){if(!value.ticket||!value.nonce||!value.generation)throw std::invalid_argument("Native Source resume identity invalid");return "SOURCE_WORK_RESUME 1 "+N(value.ticket)+" "+N(value.nonce)+" "+N(value.generation)+"\n";}
  static std::string Resumed(const Resume& value){auto packet=Encode(value);packet.replace(0,18,"SOURCE_WORK_RESUMED");return packet;}
  static std::string Unsupported(uint64_t ticket,uint64_t nonce,uint64_t reason){if(!ticket||!nonce||!reason)throw std::invalid_argument("Native Source unsupported identity invalid");return "SOURCE_WORK_UNSUPPORTED 1 "+N(ticket)+" "+N(nonce)+" "+N(reason)+"\n";}
  static Message Decode(std::string_view packet){
    if(packet.empty()||packet.size()>1024||packet.back()!='\n')throw std::invalid_argument("Native Source work packet framing invalid");
    packet.remove_suffix(1);std::array<std::string_view,12> words{};size_t count=0;
    while(!packet.empty()){
      if(count==words.size())throw std::invalid_argument("Native Source work packet arity exceeded");
      const auto end=packet.find(' ');const auto word=packet.substr(0,end);if(word.empty())throw std::invalid_argument("Native Source work packet empty field");
      words[count++]=word;if(end==packet.npos)break;packet.remove_prefix(end+1);if(packet.empty())throw std::invalid_argument("Native Source work packet trailing field");
    }
    if(count<2||words[1]!="1")throw std::invalid_argument("Native Source work version invalid");
    if(words[0]=="SOURCE_WORK_INSTALL"){Size(count,3);Message result{};result.kind=Kind::Install;result.ticket=U(words[2]);return result;}
    if(words[0]=="SOURCE_WORK_BEGIN"){
      Size(count,7);double deadline=0;const auto parsed=std::from_chars(words[6].data(),words[6].data()+words[6].size(),deadline,std::chars_format::general);
      if(parsed.ec!=std::errc{}||parsed.ptr!=words[6].data()+words[6].size())throw std::invalid_argument("Native Source work original deadline malformed");
      auto challenge=Issue(U(words[2]),U(words[3]),U(words[4]),U(words[5]),deadline);
      if(challenge.deadline_text!=words[6])throw std::invalid_argument("Native Source work original deadline noncanonical");
      Message result{};result.kind=Kind::Begin;result.challenge=std::move(challenge);return result;
    }
    if(words[0]=="SOURCE_WORK_CUT"||words[0]=="SOURCE_WORK_OBSERVED"||words[0]=="SOURCE_WORK_ACK"){
      Size(count,10);Cuts cuts{U(words[2]),U(words[3]),{U(words[4]),U(words[5],true),U(words[6])},{U(words[7]),U(words[8],true),U(words[9])}};
      Message result{};result.kind=words[0]=="SOURCE_WORK_CUT"?Kind::Cut:words[0]=="SOURCE_WORK_OBSERVED"?Kind::Observed:Kind::Acknowledged;result.cuts=cuts;return result;
    }
    if(words[0]=="SOURCE_WORK_CAP"){
      Size(count,5);const auto pid=U(words[3]);if(pid>INT32_MAX)throw std::invalid_argument("Native Source work capability PID invalid");
      Message result{};result.kind=Kind::Capability;result.capability={U(words[2]),static_cast<int64_t>(pid),U(words[4])};return result;
    }
    if(words[0]=="SOURCE_WORK_RESUME"||words[0]=="SOURCE_WORK_RESUMED"){
      Size(count,5);Message result{};result.kind=words[0]=="SOURCE_WORK_RESUME"?Kind::Resume:Kind::Resumed;result.resume={U(words[2]),U(words[3]),U(words[4])};return result;
    }
    if(words[0]=="SOURCE_WORK_UNSUPPORTED"){
      Size(count,5);Message result{};result.kind=Kind::Unsupported;result.ticket=U(words[2]);result.nonce=U(words[3]);result.reason=U(words[4]);return result;
    }
    throw std::invalid_argument("Unknown native Source work packet");
  }
 private:
  static std::string N(uint64_t value){return std::to_string(value);}
  static void Size(size_t actual,size_t expected){if(actual!=expected)throw std::invalid_argument("Native Source work packet arity invalid");}
  static uint64_t U(std::string_view value,bool zero=false){
    uint64_t result=0;if(value.empty()||(value.size()>1&&value.front()=='0'))throw std::invalid_argument("Native Source work integer noncanonical");
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||(!zero&&!result))throw std::invalid_argument("Native Source work integer invalid");
    return result;
  }
};
} // namespace suitecut::runner
