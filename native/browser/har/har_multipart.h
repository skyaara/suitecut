// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
namespace suitecut {
namespace har_multipart_detail {
inline bool EqualASCII(std::string_view a,std::string_view b){
  if(a.size()!=b.size())return false;
  for(size_t i=0;i<a.size();++i){const auto lower=[](unsigned char c){return c>='A'&&c<='Z'?static_cast<unsigned char>(c+('a'-'A')):c;};if(lower(a[i])!=lower(b[i]))return false;}return true;
}
inline bool Token(unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c))!=std::string_view::npos;}
inline void Space(std::string_view& value){while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.remove_prefix(1);}
inline std::string_view TakeToken(std::string_view& value){size_t count=0;while(count<value.size()&&Token(value[count]))++count;const auto token=value.substr(0,count);value.remove_prefix(count);return token;}
struct Boundary {std::array<char,70> bytes{};size_t size=0;std::string_view View()const{return {bytes.data(),size};}};
inline bool BoundaryByte(unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view("'()+_,-./:=? ").find(static_cast<char>(c))!=std::string_view::npos;}
// Strict MIME parameter parsing. No header/body is modified, retained again or
// inferred from payload text. Duplicate boundary parameters are ambiguous.
inline std::optional<Boundary> ContentType(std::string_view type){
  Space(type);if(!EqualASCII(TakeToken(type),"multipart")||type.empty()||type.front()!='/')return std::nullopt;
  type.remove_prefix(1);if(!EqualASCII(TakeToken(type),"form-data"))return std::nullopt;
  Boundary boundary;bool found=false;
  for(;;){
    Space(type);if(type.empty())break;if(type.front()!=';')return std::nullopt;type.remove_prefix(1);Space(type);
    const auto name=TakeToken(type);if(name.empty())return std::nullopt;Space(type);if(type.empty()||type.front()!='=')return std::nullopt;type.remove_prefix(1);Space(type);
    const bool selected=EqualASCII(name,"boundary");if(selected&&found)return std::nullopt;
    size_t count=0;bool valid=true;
    const auto byte=[&](unsigned char c){if(selected){if(!BoundaryByte(c)||count>=boundary.bytes.size())valid=false;else boundary.bytes[count]=static_cast<char>(c);}++count;};
    if(!type.empty()&&type.front()=='"'){
      type.remove_prefix(1);bool closed=false;
      while(!type.empty()){
        auto c=static_cast<unsigned char>(type.front());type.remove_prefix(1);
        if(c=='"'){closed=true;break;}
        if(c=='\\'){if(type.empty())return std::nullopt;c=static_cast<unsigned char>(type.front());type.remove_prefix(1);}
        if((c<32&&c!='\t')||c==127||c>=128)return std::nullopt;
        byte(c);
      }
      if(!closed)return std::nullopt;
    }else{const auto token=TakeToken(type);if(token.empty())return std::nullopt;for(const unsigned char c:token)byte(c);}
    if(!valid)return std::nullopt;
    if(selected){if(!count||boundary.bytes[count-1]==' ')return std::nullopt;boundary.size=count;found=true;}
  }
  return found?std::optional<Boundary>(boundary):std::nullopt;
}
struct Parameter {std::string_view name,value;};
// Called only after strict ContentType validation. Keep every other parameter
// byte-exact and ordered; boundary matching cannot erase a charset difference.
inline std::optional<Parameter> OtherParameter(std::string_view& type){
  while(!type.empty()){
    Space(type);if(type.empty())break;type.remove_prefix(1);Space(type);const auto name=TakeToken(type);Space(type);type.remove_prefix(1);Space(type);
    const auto before=type;
    if(type.front()=='"'){
      type.remove_prefix(1);while(!type.empty()){const auto c=type.front();type.remove_prefix(1);if(c=='\\')type.remove_prefix(1);else if(c=='"')break;}
    }else TakeToken(type);
    const auto value=before.substr(0,before.size()-type.size());
    if(!EqualASCII(name,"boundary"))return Parameter{name,value};
  }
  return std::nullopt;
}
inline bool OtherParametersEqual(std::string_view a,std::string_view b){
  const auto start=[](std::string_view type){const auto at=type.find(';');return at==std::string_view::npos?std::string_view{}:type.substr(at);};
  a=start(a);b=start(b);
  for(;;){const auto left=OtherParameter(a),right=OtherParameter(b);if(!left||!right)return !left&&!right;if(!EqualASCII(left->name,right->name)||left->value!=right->value)return false;}
}
struct Delimiter {size_t after=0;bool closing=false;std::string_view suffix;};
inline std::optional<Delimiter> At(std::string_view body,size_t at,std::string_view boundary){
  if(at>body.size()||body.substr(at,2)!="--"||body.substr(at+2,boundary.size())!=boundary)return std::nullopt;
  size_t end=at+2+boundary.size();const auto suffix=end;bool closing=false;
  if(body.substr(end,2)=="--"){closing=true;end+=2;}
  while(end<body.size()&&(body[end]==' '||body[end]=='\t'))++end;
  if(body.substr(end,2)=="\r\n")return Delimiter{end+2,closing,body.substr(suffix,end+2-suffix)};
  if(closing&&end==body.size())return Delimiter{end,closing,body.substr(suffix,end-suffix)};
  return std::nullopt;
}
inline bool PartHeaders(std::string_view part){
  const auto end=part.find("\r\n\r\n");if(end==std::string_view::npos)return false;
  auto headers=part.substr(0,end);bool disposition=false;
  while(!headers.empty()){
    const auto next=headers.find("\r\n");auto line=headers.substr(0,next);const auto name=TakeToken(line);
    if(name.empty()||line.empty()||line.front()!=':')return false;
    line.remove_prefix(1);
    for(const unsigned char c:line)if((c<32&&c!='\t')||c==127)return false;
    if(EqualASCII(name,"content-disposition")){if(disposition)return false;Space(line);if(!EqualASCII(TakeToken(line),"form-data"))return false;Space(line);if(!line.empty()&&line.front()!=';')return false;disposition=true;}
    if(next==std::string_view::npos)break;
    headers.remove_prefix(next+2);
  }
  return disposition;
}
struct Part {std::string_view bytes;Delimiter next;};
inline std::optional<Part> Next(std::string_view body,size_t start,std::string_view boundary){
  size_t at=start;
  for(;;){
    at=body.find("\r\n--",at);if(at==std::string_view::npos)return std::nullopt;
    if(const auto next=At(body,at+2,boundary)){
      const auto part=body.substr(start,at-start);if(!PartHeaders(part))return std::nullopt;
      return Part{part,*next};
    }
    at+=4;
  }
}
} // namespace har_multipart_detail
// Compare the original byte spans, skipping ONLY their valid outer boundary
// delimiter tokens. Part headers, binary payloads, duplicate names and ordering
// remain byte-exact. Malformed or ambiguous framing never gains a match.
inline bool NativeHARMultipartEquivalent(std::string_view recorded,std::string_view recorded_type,std::string_view actual,std::string_view actual_type){
  using namespace har_multipart_detail;
  const auto left=ContentType(recorded_type),right=ContentType(actual_type);if(!left||!right||!OtherParametersEqual(recorded_type,actual_type))return false;
  auto a=At(recorded,0,left->View()),b=At(actual,0,right->View());if(!a||!b)return false;
  for(;;){
    if(a->closing!=b->closing||a->suffix!=b->suffix)return false;
    if(a->closing)return recorded.substr(a->after)==actual.substr(b->after);
    const auto part_a=Next(recorded,a->after,left->View()),part_b=Next(actual,b->after,right->View());
    if(!part_a||!part_b||part_a->bytes!=part_b->bytes)return false;
    a=part_a->next;b=part_b->next;
  }
}
} // namespace suitecut
