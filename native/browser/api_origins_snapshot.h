// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <memory_resource>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace suitecut {
// Independent API contexts cannot execute author documents. Their origins are
// the accepted constructor snapshot, while cookies come from their live jar.
// This resource charges actual PMR allocation requests, including simultaneous
// vector/string replacements. It is shared by every pending/committed snapshot.
class APIOriginsBudget final : public std::pmr::memory_resource {
 public:
  explicit APIOriginsBudget(size_t limit=32*1048576,std::pmr::memory_resource* upstream=std::pmr::get_default_resource()):limit_(limit),upstream_(upstream?upstream:std::pmr::get_default_resource()){}
  size_t Bytes()const{return bytes_.load();}
  size_t Peak()const{return peak_.load();}
 private:
  void* do_allocate(size_t bytes,size_t alignment)override{
    auto before=bytes_.load();
    do{if(bytes>limit_||before>limit_-bytes)throw std::bad_alloc();}while(!bytes_.compare_exchange_weak(before,before+bytes));
    try{auto result=upstream_->allocate(bytes,alignment);auto peak=peak_.load();while(peak<before+bytes&&!peak_.compare_exchange_weak(peak,before+bytes)){}return result;}
    catch(...){bytes_.fetch_sub(bytes);throw;}
  }
  void do_deallocate(void* pointer,size_t bytes,size_t alignment)override{upstream_->deallocate(pointer,bytes,alignment);bytes_.fetch_sub(bytes);}
  bool do_is_equal(const std::pmr::memory_resource&other)const noexcept override{return this==&other;}
  size_t limit_;std::pmr::memory_resource* upstream_;std::atomic_size_t bytes_{0},peak_{0};
};

// Bounded span parser for an already semantically validated origin JSON value.
// It never parses/re-encodes author values: isolated UTF16 escape sequences,
// structured-clone tags, property order and array order remain byte-exact.
// A complete lexical parse is still required before trusting member boundaries.
class APIOriginJSON {
 public:
  struct Member{std::string key;size_t begin,end;};
  static bool Members(std::string_view text,std::vector<Member>&members){
    APIOriginJSON parser(text);parser.Space();if(!parser.Take('{'))return false;parser.Space();
    if(parser.Take('}'))return parser.End();
    do{parser.Space();const auto begin=parser.at_;std::string key;if(!parser.String(&key))return false;parser.Space();if(!parser.Take(':')||!parser.Value(0))return false;members.push_back({std::move(key),begin,parser.at_});if(members.size()>128)return false;parser.Space();if(parser.Take('}'))return parser.End();if(!parser.Take(','))return false;}while(true);
  }
 private:
  explicit APIOriginJSON(std::string_view text):text_(text){}
  void Space(){while(at_<text_.size()&&(text_[at_]==' '||text_[at_]=='\n'||text_[at_]=='\r'||text_[at_]=='\t'))++at_;}
  bool Take(char value){if(at_==text_.size()||text_[at_]!=value)return false;++at_;return true;}
  bool End(){Space();return at_==text_.size();}
  static int Hex(char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
  bool String(std::string*key=nullptr){
    if(!Take('"'))return false;
    while(at_<text_.size()){
      const unsigned char c=text_[at_++];if(c=='"')return true;if(c<32)return false;
      if(c!='\\'){if(key){if(key->size()>=128)return false;key->push_back(static_cast<char>(c));}continue;}
      if(at_==text_.size())return false;
      const char escape=text_[at_++];unsigned unit=0;
      if(escape=='u'){for(int i=0;i<4;++i){if(at_==text_.size())return false;const int h=Hex(text_[at_++]);if(h<0)return false;unit=(unit<<4)|h;}}
      else{switch(escape){case '"':case '\\':case '/':unit=static_cast<unsigned char>(escape);break;case 'b':unit=8;break;case 'f':unit=12;break;case 'n':unit=10;break;case 'r':unit=13;break;case 't':unit=9;break;default:return false;}}
      // Only ASCII schema keys are meaningful; non-ASCII author keys stay in
      // the raw value and cannot alias an optional ASCII top-level member.
      if(key){if(key->size()>=128)return false;key->push_back(unit<128?static_cast<char>(unit):'\x7f');}
    }return false;
  }
  bool Value(size_t depth){
    Space();if(depth>200||++work_>100000||at_==text_.size())return false;
    if(text_[at_]=='"')return String();
    if(text_[at_]=='{'||text_[at_]=='['){const bool object=text_[at_++]=='{';const char end=object?'}':']';Space();if(Take(end))return true;do{Space();if(object&&(!String()||(Space(),!Take(':'))))return false;if(!Value(depth+1))return false;Space();if(Take(end))return true;if(!Take(','))return false;}while(true);}
    for(const auto token:{std::string_view("true"),std::string_view("false"),std::string_view("null")})if(text_.substr(at_,token.size())==token){at_+=token.size();return true;}
    Take('-');if(at_==text_.size())return false;
    if(!Take('0')){if(text_[at_]<'1'||text_[at_]>'9')return false;while(at_<text_.size()&&text_[at_]>='0'&&text_[at_]<='9')++at_;}
    if(Take('.')){const auto start=at_;while(at_<text_.size()&&text_[at_]>='0'&&text_[at_]<='9')++at_;if(start==at_)return false;}
    if(Take('e')||Take('E')){if(!Take('+'))Take('-');const auto start=at_;while(at_<text_.size()&&text_[at_]>='0'&&text_[at_]<='9')++at_;if(start==at_)return false;}
    return true;
  }
  std::string_view text_;size_t at_=0,work_=0;
};
class APIOriginsSnapshot {
 public:
  struct Clock{std::function<double()>now;double deadline=0;};
  struct Origin{std::pmr::string origin,json;Origin(std::string_view name,std::string_view value,std::pmr::memory_resource* resource):origin(name,resource),json(value,resource){}};
  explicit APIOriginsSnapshot(std::shared_ptr<APIOriginsBudget>budget):budget_(std::move(budget)),origins_(budget_.get()){}
  static bool Current(const Clock&clock,std::string&error){const double now=clock.now?clock.now():-1;if(!std::isfinite(now)||now<0||!std::isfinite(clock.deadline)||clock.deadline<0){error="Native API origins clock is unavailable";return false;}if(clock.deadline&&now>=clock.deadline){error="Native API origins original deadline expired";return false;}return true;}
  bool AddValidated(std::string_view origin,std::string_view json,const Clock&clock,std::string&error){
    if(!Current(clock,error))return false;
    if(origin.empty()||origin.size()>8192||json.size()>524288||origins_.size()>=1024||json.size()>8*1048576-bytes_){error="Native API constructor origins capacity exceeded";return false;}
    std::vector<APIOriginJSON::Member>members;
    if(!APIOriginJSON::Members(json,members)){error="Invalid native API constructor origin JSON";return false;}
    bool name=false,storage=false,idb=false,opfs=false;
    for(const auto&member:members){bool*seen=member.key=="origin"?&name:member.key=="localStorage"?&storage:member.key=="indexedDB"?&idb:member.key=="opfs"?&opfs:nullptr;if(seen){if(*seen){error="Duplicate native API constructor origin member";return false;}*seen=true;}}
    if(!name||!storage){error="Missing native API constructor origin members";return false;}
    for(const auto&entry:origins_)if(entry.origin==origin){error="Duplicate native API constructor origin";return false;}
    try{Origin entry(origin,json,budget_.get());if(!Current(clock,error))return false;origins_.push_back(std::move(entry));bytes_+=json.size();error.clear();return true;}
    catch(const std::bad_alloc&){error="Native API constructor origins allocation budget exceeded";return false;}
  }
  bool Project(size_t index,bool indexed_db,bool opfs,const Clock&clock,std::pmr::string&output,std::string&error)const{
    if(!Current(clock,error))return false;
    if(output.get_allocator().resource()!=budget_.get()){error="Native API origins publication allocator mismatch";return false;}
    if(index>=origins_.size()){error="Native API constructor origin index is unavailable";return false;}
    const auto&text=origins_[index].json;std::vector<APIOriginJSON::Member>members;
    if(!APIOriginJSON::Members(text,members)){error="Invalid retained native API constructor origin JSON";return false;}
    try{
      std::pmr::string next(budget_.get());next.reserve(text.size());next+='{' ;bool first=true;
      for(const auto&member:members){if(member.key!="origin"&&member.key!="localStorage"&&!(indexed_db&&member.key=="indexedDB")&&!(opfs&&member.key=="opfs"))continue;if(!first)next+=',';first=false;next.append(text.data()+member.begin,member.end-member.begin);}
      next+='}';if(!Current(clock,error))return false;output=std::move(next);error.clear();return true;
    }catch(const std::bad_alloc&){error="Native API constructor origins publication budget exceeded";return false;}
  }
  size_t Size()const{return origins_.size();}
  const Origin&At(size_t index)const{return origins_.at(index);}
  std::pmr::memory_resource*Resource()const{return budget_.get();}
 private:
  std::shared_ptr<APIOriginsBudget>budget_;std::pmr::vector<Origin>origins_;size_t bytes_=0;
};
}
