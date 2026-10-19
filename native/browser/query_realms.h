// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <string_view>
#include <vector>

// Bounded authored scanner for selector syntax only. JSON nesting is decoded
// solely to choose genuine native execution worlds, never to rewrite values.
class SuiteCutQueryRealms {
 public:
  static std::vector<std::string> Engines(std::string_view selector) {
    std::vector<std::string> result;size_t work=0;Scan(selector,0,work,result);return result;
  }
 private:
  static std::string_view Trim(std::string_view value){
    const auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f';};
    while(!value.empty()&&space(value.front()))value.remove_prefix(1);
    while(!value.empty()&&space(value.back()))value.remove_suffix(1);
    return value;
  }
  static bool Name(std::string_view name){if(name.empty())return false;for(const unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;return true;}
  static bool JSONString(std::string_view value,std::string& output,size_t& work){
    if(value.size()<2||value.front()!='"'||value.back()!='"')return false;
    for(size_t i=1;i+1<value.size();++i){
      if(++work>1000000)return false;
      const unsigned char c=value[i];if(c=='"'||c<32)return false;
      if(c!='\\'){output+=static_cast<char>(c);continue;}if(++i+1>=value.size())return false;
      const char escaped=value[i];
      if(escaped=='"'||escaped=='\\'||escaped=='/')output+=escaped;
      else if(escaped=='b')output+='\b';else if(escaped=='f')output+='\f';else if(escaped=='n')output+='\n';else if(escaped=='r')output+='\r';else if(escaped=='t')output+='\t';
      else if(escaped=='u'){
        if(i+4>=value.size()-1)return false;
        unsigned unit=0;
        for(int index=0;index<4;++index){const unsigned char digit=value[++i];unsigned number;if(digit>='0'&&digit<='9')number=digit-'0';else if(digit>='a'&&digit<='f')number=digit-'a'+10;else if(digit>='A'&&digit<='F')number=digit-'A'+10;else return false;unit=(unit<<4)|number;}
        // Realm names are ASCII. Keep every non-ASCII UTF16 code unit opaque;
        // its WTF8 representation cannot introduce an ASCII syntax delimiter.
        if(unit<128)output+=static_cast<char>(unit);else if(unit<2048){output+=static_cast<char>(0xc0|(unit>>6));output+=static_cast<char>(0x80|(unit&63));}else{output+=static_cast<char>(0xe0|(unit>>12));output+=static_cast<char>(0x80|((unit>>6)&63));output+=static_cast<char>(0x80|(unit&63));}
      }else return false;
    }
    return true;
  }
  static void Scan(std::string_view selector,int nesting,size_t& work,std::vector<std::string>& result){
    if(nesting>64||selector.size()>65536||work>1000000)return;
    size_t start=0;int depth=0;char quote=0;bool escaped=false,regex=false,character_class=false;
    const auto body_start=[&](){const auto part=selector.substr(start);const auto equal=part.find('=');if(equal==std::string_view::npos)return std::string_view::npos;auto name=Trim(part.substr(0,equal));if(!name.empty()&&name.front()=='*')name.remove_prefix(1);return name=="text"||name=="text:light"?start+equal+1:std::string_view::npos;};
    auto text_start=body_start();
    const auto append=[&](size_t end){
      auto part=Trim(selector.substr(start,end-start));if(!part.empty()&&part.front()=='*')part.remove_prefix(1);
      const auto equal=part.find('=');if(equal==std::string_view::npos||!equal)return;const auto name=Trim(part.substr(0,equal));
      if(name=="internal:has"||name=="internal:has-not"||name=="internal:and"||name=="internal:or"||name=="internal:chain"){
        std::string nested;if(JSONString(Trim(part.substr(equal+1)),nested,work))Scan(nested,nesting+1,work,result);return;
      }
      if(Name(name))result.emplace_back(name);
    };
    for(size_t i=0;i<selector.size();++i){
      if(++work>1000000)return;
      const char c=selector[i];
      if(escaped){escaped=false;continue;}if(c=='\\'){escaped=true;continue;}
      if(quote){if(c==quote)quote=0;continue;}
      if(regex){if(c=='[')character_class=true;if(c==']')character_class=false;if(c=='/'&&!character_class)regex=false;continue;}
      if(((c=='\''||c=='"')&&(text_start==std::string_view::npos||i==text_start))||(c=='`'&&i==text_start)){quote=c;continue;}
      if(c=='/'){
        bool begins_regex=text_start!=std::string_view::npos&&i==text_start;
        if(text_start==std::string_view::npos){const auto prefix=Trim(selector.substr(start,i-start));begins_regex=!prefix.empty()&&prefix.back()=='=';}
        if(begins_regex){regex=true;continue;}
      }
      if(text_start==std::string_view::npos){if(c=='['||c=='(')++depth;if(c==']'||c==')')--depth;}
      if(!depth&&c=='>'&&i+1<selector.size()&&selector[i+1]=='>'){append(i);start=i+2;++i;text_start=body_start();}
    }
    append(selector.size());
  }
};
