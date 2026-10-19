// SPDX-License-Identifier: MIT
#pragma once
#include "grep_unicode_data.h"
#include "grep_unicode_properties.h"
#include "grep_work.h"
#include <algorithm>
#include <limits>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace suitecut::runner {
  // Authored RegExp VM. Ordered alternatives retain captures; lookarounds are
  // atomic subprograms and lookbehind executes its sequence backwards. Every VM
  // instruction, character/range comparison and capture snapshot is budgeted.
  // Explicit retained-state payload and instruction bounds cover backreferences/catastrophic
  // patterns without threads, elapsed-time guesses or unowned matcher processes.
  class GrepVM {
    public: struct Flags {
      bool insensitive=false,multiline=false,dot_all=false,sticky=false,unicode=false,unicode_sets=false;
    }
    ;
    explicit GrepVM(std::u16string source,Flags flags):source_(std::move(source)),flags_(flags){
      if(source_.size()>4096)Fail("pattern capacity4096 exceeded");
      total_captures_=CountCaptures();
      const auto root=Disjunction();
      root_=root;
      if(at_!=source_.size())Fail("unexpected closing delimiter");
      ResolveReferences();
      start_=Compile(root,1,Emit({
        Op::Accept
      }
      ));
    }
    bool Matches(std::u16string_view title,GrepWork& work)const{
      if(title.size()>32768)Fail("title capacity32768 exceeded");
      std::vector<uint32_t> input;
      input.reserve(title.size());
      for(size_t i=0;
      i<title.size();
      ++i){
        work.Charge();
        uint32_t point=title[i];
        if(Unicode()&&point>=0xd800&&point<=0xdbff&&i+1<title.size()&&title[i+1]>=0xdc00&&title[i+1]<=0xdfff)point=0x10000+((point-0xd800)<<10)+(title[++i]-0xdc00);
        input.push_back(point);
      }
      Memory memory;
      for(size_t origin=0;
      origin<=input.size();
      ++origin){
        State state;
        state.pc=start_;
        state.position=origin;
        state.captures.resize(total_captures_+1);
        state.loops.resize(loop_count_);
        work.Charge(state.captures.size()+state.loops.size());
        if(Run(std::move(state),input,work,memory,0))return true;
        if(flags_.sticky)return false;
      }
      return false;
    }
    size_t StateCount()const{
      return code_.size();
    }
    size_t RangeCount()const{
      return range_count_;
    }
    bool RequiresVM()const{
      return advanced_||Unicode();
    }
    bool CanUseNFA()const{
      const auto size=Estimate(root_);
      return !RequiresVM()&&size.first<2048&&size.second<16384;
    }
    private: using Range=std::pair<uint32_t,uint32_t>;
    using Ranges=std::vector<Range>;
    using String=std::vector<uint32_t>;
    struct Set {
      Ranges ranges;
      std::vector<String> strings;
      bool may_strings=false,case_ready=false;
    }
    ;
    static constexpr size_t none=std::numeric_limits<size_t>::max();
    static constexpr uint64_t infinity=std::numeric_limits<uint64_t>::max();
    enum class Kind{
      Empty,Character,Sequence,Alternative,Capture,Assert,Repeat,Begin,End,Boundary,NonBoundary,Reference
    }
    ;
    struct Node {
      Kind kind;
      Flags flags{
      }
      ;
      std::vector<size_t> children{
      }
      ;
      Ranges ranges{
      }
      ;
      std::vector<String> strings{
      }
      ;
      size_t capture=0,loop=0;
      uint64_t minimum=0,maximum=0;
      bool greedy=true,positive=true,behind=false,raw_ranges=false;
      std::u16string reference{
      }
      ;
      std::vector<size_t> captures{
      }
      ;
    }
    ;
    enum class Op{
      Accept,Character,Split,SaveStart,SaveEnd,Assert,Reference,Begin,End,Boundary,NonBoundary,LoopInit,LoopDecide,LoopStart,LoopStep,Clear
    }
    ;
    struct Instruction {
      Op op;
      size_t next=none,other=none,node=none,group=0;
      int direction=1;
    }
    ;
    struct Capture {
      size_t first=none,last=none;
    }
    ;
    struct Loop {
      uint64_t count=0;
      size_t position=0;
    }
    ;
    struct State {
      size_t pc=0,position=0;
      std::vector<Capture> captures;
      std::vector<Loop> loops;
    }
    ;
    // Account live State/capture/loop snapshot payload. Allocator overhead,
    // vector spare capacity and bounded input/AST buffers are additional; this
    // counter is not an RSS/total-heap measurement.
    struct Memory {
      size_t bytes=0;
    }
    ;
    class Retained {
      public:Retained(Memory& owner,size_t bytes):owner_(owner),bytes_(bytes){
        if(bytes>32*1024*1024-owner_.bytes)Fail("retained matcher memory capacity32MiB payload exceeded");
        owner_.bytes+=bytes;
      }
      ~Retained(){
        owner_.bytes-=bytes_;
      }
      private:Memory& owner_;
      size_t bytes_;
    }
    ;
    [[noreturn]]static void Fail(const char* message){
      throw std::invalid_argument(std::string("Native grep VM: ")+message);
    }
    bool Unicode()const{
      return flags_.unicode||flags_.unicode_sets;
    }
    static bool Line(uint32_t point){
      return point==10||point==13||point==0x2028||point==0x2029;
    }
    static uint32_t Canonical(uint32_t point,const Flags& flags){
      if(!flags.insensitive)return point;
      return flags.unicode||flags.unicode_sets?grep_unicode::Simple(point):grep_unicode::Legacy(static_cast<uint16_t>(point));
    }
    static bool Word(uint32_t point,const Flags& flags){
      point=Canonical(point,flags);
      return(point>='A'&&point<='Z')||(point>='a'&&point<='z')||(point>='0'&&point<='9')||point=='_';
    }
    static Ranges Normalize(Ranges ranges){
      std::sort(ranges.begin(),ranges.end());
      Ranges result;
      for(const auto& range:ranges){
        if(range.first>range.second)Fail("reversed character range");
        if(!result.empty()&&range.first<=result.back().second+1)result.back().second=std::max(range.second,result.back().second);
        else result.push_back(range);
      }
      return result;
    }
    Ranges Complement(const Ranges& ranges)const{
      Ranges result;
      uint32_t next=0;
      const uint32_t last=Unicode()?0x10ffff:65535;
      for(const auto& range:ranges){
        if(next<range.first)result.emplace_back(next,range.first-1);
        next=range.second+1;
      }
      if(next<=last)result.emplace_back(next,last);
      return result;
    }
    Ranges CanonicalRanges(const Ranges& ranges)const{
      if(!flags_.insensitive)return ranges;
      Ranges result;
      const auto project=[&](const auto& mappings){
        for(const auto& [first,last]:ranges){
          uint32_t cursor=first;
          auto found=std::lower_bound(mappings.begin(),mappings.end(),first,[](const auto& mapping,uint32_t point){
            return mapping.from<point;
          }
          );
          for(;
          found!=mappings.end()&&found->from<=last;
          ++found){
            if(cursor<found->from)result.emplace_back(cursor,found->from-1);
            result.emplace_back(found->to,found->to);
            cursor=found->from+1;
          }
          if(cursor<=last)result.emplace_back(cursor,last);
        }
      }
      ;
      if(Unicode())project(grep_unicode::kSimpleMappings);
      else project(grep_unicode::kLegacyMappings);
      return Normalize(std::move(result));
    }
    static bool HasPoint(const Ranges& ranges,uint32_t point){
      size_t first=0,last=ranges.size();
      while(first<last){
        const auto middle=first+(last-first)/2;
        if(point<ranges[middle].first)last=middle;
        else if(point>ranges[middle].second)first=middle+1;
        else return true;
      }
      return false;
    }
    // Keep scalar set membership separate from string character comparisons.
    // The pinned public Node24 v+i observations retain raw ASCII property
    // membership and canonical singleton q-elements, while ordinary character
    // operands have full simple-fold closure and multi-point strings compare
    // canonically. The two shipped genuine Node matrices exercise these origins.
    Ranges CaseClosure(Ranges ranges)const{
      ranges=CanonicalRanges(Normalize(std::move(ranges)));
      if(!flags_.insensitive)return ranges;
      Ranges result=ranges;
      for(const auto& mapping:grep_unicode::kSimpleMappings)if(HasPoint(ranges,mapping.to))result.emplace_back(mapping.from,mapping.from);
      return Normalize(std::move(result));
    }
    size_t Add(Node node){
      if(nodes_.size()>=4096)Fail("AST capacity4096 exceeded");
      ast_payload_+=node.ranges.size();
      for(const auto& string:node.strings)ast_payload_+=string.size()+1;
      if(ast_payload_>262144)Fail("AST range/string payload capacity262144 exceeded");
      node.flags=flags_;
      nodes_.push_back(std::move(node));
      return nodes_.size()-1;
    }
    size_t Character(Ranges ranges){
      Node node{
        Kind::Character
      }
      ;
      node.raw_ranges=flags_.unicode_sets;
      node.ranges=flags_.unicode_sets?CaseClosure(std::move(ranges)):CanonicalRanges(Normalize(std::move(ranges)));
      return Add(std::move(node));
    }
    bool Take(char16_t value){
      if(at_<source_.size()&&source_[at_]==value){
        ++at_;
        return true;
      }
      return false;
    }
    size_t CountCaptures(){
      size_t count=0,class_depth=0;
      bool escaped=false;
      for(size_t i=0;
      i<source_.size();
      ++i){
        const auto c=source_[i];
        if(escaped){
          escaped=false;
          continue;
        }
        if(c=='\\'){
          escaped=true;
          continue;
        }
        if(c=='['){
          if(flags_.unicode_sets||!class_depth)++class_depth;
        }
        else if(c==']'&&class_depth)--class_depth;
        else if(c=='('&&!class_depth&&(i+1==source_.size()||source_[i+1]!='?'||(i+2<source_.size()&&source_[i+2]=='<'&&i+3<source_.size()&&source_[i+3]!='='&&source_[i+3]!='!'))){
          if(i+2<source_.size()&&source_[i+1]=='?'&&source_[i+2]=='<')has_named_=true;
          if(++count>128)Fail("capture capacity128 exceeded");
        }
      }
      return count;
    }
    size_t Disjunction(){
      if(++depth_>64)Fail("group depth64 exceeded");
      const auto branch=disjunction_count_++;
      branch_path_.emplace_back(branch,0);
      Node node{
        Kind::Alternative
      }
      ;
      node.children.push_back(Sequence());
      while(Take('|')){
        ++branch_path_.back().second;
        node.children.push_back(Sequence());
      }
      branch_path_.pop_back();
      --depth_;
      return node.children.size()==1?node.children.front():Add(std::move(node));
    }
    size_t Sequence(){
      Node node{
        Kind::Sequence
      }
      ;
      while(at_<source_.size()&&source_[at_]!=')'&&source_[at_]!='|')node.children.push_back(Term());
      if(node.children.empty())return Add({
        Kind::Empty
      }
      );
      return node.children.size()==1?node.children.front():Add(std::move(node));
    }
    uint64_t Number(){
      uint64_t result=0;
      const auto begin=at_;
      while(at_<source_.size()&&source_[at_]>='0'&&source_[at_]<='9'){
        const auto digit=source_[at_++]-'0';
        if(result>(infinity-digit)/10)result=infinity;
        else result=result*10+digit;
      }
      if(at_==begin)Fail("expected quantifier number");
      return result;
    }
    size_t Term(){
      const auto atom=Atom();
      if(at_==source_.size())return atom;
      uint64_t minimum=0,maximum=0;
      bool quantifier=true;
      if(Take('*'))maximum=infinity;
      else if(Take('+')){
        minimum=1;
        maximum=infinity;
      }
      else if(Take('?'))maximum=1;
      else if(source_[at_]=='{'){
        const auto saved=at_++;
        if(at_==source_.size()||source_[at_]<'0'||source_[at_]>'9'){
          at_=saved;
          return atom;
        }
        minimum=maximum=Number();
        if(Take(',')){
          if(at_<source_.size()&&source_[at_]=='}')maximum=infinity;
          else if(at_<source_.size()&&source_[at_]>='0'&&source_[at_]<='9')maximum=Number();
          else{
            if(Unicode())Fail("invalid Unicode quantifier");
            at_=saved;
            return atom;
          }
        }
        if(!Take('}')){
          if(Unicode())Fail("unterminated quantifier");
          at_=saved;
          return atom;
        }
        if(maximum<minimum)Fail("reversed quantifier bounds");
        if(minimum>256||(maximum!=infinity&&maximum>256))advanced_=true;
      }
      else quantifier=false;
      if(!quantifier)return atom;
      if(nodes_[atom].kind==Kind::Begin||nodes_[atom].kind==Kind::End||nodes_[atom].kind==Kind::Boundary||nodes_[atom].kind==Kind::NonBoundary||(nodes_[atom].kind==Kind::Assert&&(Unicode()||nodes_[atom].behind)))Fail("assertion cannot be quantified");
      Node repeat{
        Kind::Repeat
      }
      ;
      repeat.children={
        atom
      }
      ;
      repeat.minimum=minimum;
      repeat.maximum=maximum;
      repeat.greedy=!Take('?');
      repeat.loop=loop_count_++;
      CollectCaptures(atom,repeat.captures);
      return Add(std::move(repeat));
    }
    uint32_t Hex(unsigned length){
      uint32_t value=0;
      for(unsigned i=0;
      i<length;
      ++i){
        if(at_==source_.size())Fail("truncated hexadecimal escape");
        const auto c=source_[at_++];
        unsigned digit;
        if(c>='0'&&c<='9')digit=c-'0';
        else if(c>='a'&&c<='f')digit=c-'a'+10;
        else if(c>='A'&&c<='F')digit=c-'A'+10;
        else Fail("invalid hexadecimal escape");
        value=value*16+digit;
      }
      return value;
    }
    uint32_t Point(){
      uint32_t point=source_[at_++];
      if(Unicode()&&point>=0xd800&&point<=0xdbff&&at_<source_.size()&&source_[at_]>=0xdc00&&source_[at_]<=0xdfff)point=0x10000+((point-0xd800)<<10)+(source_[at_++]-0xdc00);
      return point;
    }
    static bool IdentifierPoint(uint32_t point,bool first){
      if(point=='$'||point=='_'||(!first&&(point==0x200c||point==0x200d)))return true;
      const auto* property=grep_properties::Find(first?"ID_Start":"ID_Continue");
      size_t begin=0,end=property->range_count;
      while(begin<end){
        const auto middle=begin+(end-begin)/2;
        const auto& range=grep_properties::kRanges[property->first_range+middle];
        if(point<range.first)end=middle;
        else if(point>range.last)begin=middle+1;
        else return true;
      }
      return false;
    }
    uint32_t NamePoint(){
      if(Take('\\')){
        if(!Take('u'))Fail("capture name escape must be Unicode");
        uint32_t point=0;
        if(Take('{')){
          const auto begin=at_;
          while(at_<source_.size()&&source_[at_]!='}'){
            if(point>0x10ffff/16)Fail("capture name point exceeds maximum");
            point=point*16+Hex(1);
          }
          if(at_==begin||!Take('}')||point>0x10ffff)Fail("invalid capture name point");
          return point;
        }
        point=Hex(4);
        if(point>=0xd800&&point<=0xdbff&&at_+5<source_.size()&&source_[at_]=='\\'&&source_[at_+1]=='u'){
          const auto saved=at_;
          bool valid=true;
          for(size_t i=at_+2;
          i<at_+6;
          ++i)valid=valid&&((source_[i]>='0'&&source_[i]<='9')||(source_[i]>='a'&&source_[i]<='f')||(source_[i]>='A'&&source_[i]<='F'));
          if(valid){
            at_+=2;
            const auto low=Hex(4);
            if(low>=0xdc00&&low<=0xdfff)point=0x10000+((point-0xd800)<<10)+(low-0xdc00);
            else at_=saved;
          }
        }
        return point;
      }
      uint32_t point=source_[at_++];
      if(point>=0xd800&&point<=0xdbff&&at_<source_.size()&&source_[at_]>=0xdc00&&source_[at_]<=0xdfff)point=0x10000+((point-0xd800)<<10)+(source_[at_++]-0xdc00);
      return point;
    }
    std::u16string Name(){
      std::u16string name;
      while(at_<source_.size()&&source_[at_]!='>'){
        const auto point=NamePoint();
        if(!IdentifierPoint(point,name.empty()))Fail("invalid Unicode capture identifier");
        if(point<=65535)name+=static_cast<char16_t>(point);
        else{
          name+=static_cast<char16_t>(0xd800+((point-0x10000)>>10));
          name+=static_cast<char16_t>(0xdc00+((point-0x10000)&1023));
        }
      }
      if(name.empty()||!Take('>'))Fail("invalid capture name");
      return name;
    }
    Ranges Escape(bool in_class){
      if(at_==source_.size())Fail("trailing escape");
      const auto c=source_[at_++];
      Ranges ranges;
      if(c=='d'||c=='D')ranges={
        {
          '0','9'
        }
      }
      ;
      else if(c=='w'||c=='W'){
        ranges={
          {
            '0','9'
          }
          ,{
            'A','Z'
          }
          ,{
            '_','_'
          }
          ,{
            'a','z'
          }
        }
        ;
        if(Unicode()&&flags_.insensitive){
          ranges.emplace_back(0x17f,0x17f);
          ranges.emplace_back(0x212a,0x212a);
        }
      }
      else if(c=='s'||c=='S')ranges={
        {
          9,13
        }
        ,{
          32,32
        }
        ,{
          160,160
        }
        ,{
          0x1680,0x1680
        }
        ,{
          0x2000,0x200a
        }
        ,{
          0x2028,0x2029
        }
        ,{
          0x202f,0x202f
        }
        ,{
          0x205f,0x205f
        }
        ,{
          0x3000,0x3000
        }
        ,{
          0xfeff,0xfeff
        }
      }
      ;
      if(!ranges.empty())return c=='D'||c=='W'||c=='S'?Complement(Normalize(std::move(ranges))):ranges;
      uint32_t point=c;
      if(c=='n')point=10;
      else if(c=='r')point=13;
      else if(c=='t')point=9;
      else if(c=='v')point=11;
      else if(c=='f')point=12;
      else if(c=='b'&&in_class)point=8;
      else if(c=='c'){
        advanced_=true;
        if(at_<source_.size()&&((source_[at_]>='A'&&source_[at_]<='Z')||(source_[at_]>='a'&&source_[at_]<='z')||(in_class&&!Unicode()&&(source_[at_]=='_'||(source_[at_]>='0'&&source_[at_]<='9')))))point=source_[at_++]%32;
        else{
          if(Unicode())Fail("invalid Unicode control escape");
          return{
            {
              '\\','\\'
            }
            ,{
              'c','c'
            }
          }
          ;
        }
      }
      else if(c=='x'){
        const auto saved=at_;
        bool valid=at_+2<=source_.size();
        for(size_t i=at_;
        valid&&i<at_+2;
        ++i)valid=(source_[i]>='0'&&source_[i]<='9')||(source_[i]>='A'&&source_[i]<='F')||(source_[i]>='a'&&source_[i]<='f');
        if(valid)point=Hex(2);
        else{
          if(Unicode())Fail("invalid Unicode hex escape");
          at_=saved;
          advanced_=true;
          point='x';
        }
      }
      else if(c=='u'){
        if(Unicode()&&Take('{')){
          const auto begin=at_;
          point=0;
          while(at_<source_.size()&&source_[at_]!='}'){
            const auto digit=Hex(1);
            if(point>0x10ffff/16)Fail("Unicode escape exceeds maximum point");
            point=point*16+digit;
          }
          if(at_==begin||!Take('}')||point>0x10ffff)Fail("invalid Unicode point escape");
        }
        else{
          const auto saved=at_;
          bool valid=at_+4<=source_.size();
          for(size_t i=at_;
          valid&&i<at_+4;
          ++i)valid=(source_[i]>='0'&&source_[i]<='9')||(source_[i]>='A'&&source_[i]<='F')||(source_[i]>='a'&&source_[i]<='f');
          if(valid)point=Hex(4);
          else{
            if(Unicode())Fail("invalid Unicode hex escape");
            at_=saved;
            advanced_=true;
            point='u';
          }
        }
        if(Unicode()&&point>=0xd800&&point<=0xdbff&&at_+5<source_.size()&&source_[at_]=='\\'&&source_[at_+1]=='u'){
          const auto saved=at_;
          bool valid=true;
          for(size_t i=at_+2;
          i<at_+6;
          ++i)valid=valid&&((source_[i]>='0'&&source_[i]<='9')||(source_[i]>='a'&&source_[i]<='f')||(source_[i]>='A'&&source_[i]<='F'));
          if(valid){
            at_+=2;
            const auto low=Hex(4);
            if(low>=0xdc00&&low<=0xdfff)point=0x10000+((point-0xd800)<<10)+(low-0xdc00);
            else at_=saved;
          }
        }
      }
      else if(c>='0'&&c<='9'){
        advanced_=true;
        if(Unicode()){
          if(c!='0'||(at_<source_.size()&&source_[at_]>='0'&&source_[at_]<='9'))Fail("invalid Unicode decimal escape");
          point=0;
        }
        else if(c<='7'){
          point=c-'0';
          unsigned count=1;
          while(count<(c<='3'?3U:2U)&&at_<source_.size()&&source_[at_]>='0'&&source_[at_]<='7'){
            point=point*8+(source_[at_++]-'0');
            ++count;
          }
        }
      }
      else if(c=='p'||c=='P'){
        advanced_=true;
        if(Unicode()){
          auto set=Property(c=='P');
          if(!set.strings.empty())Fail("string property requires a Unicode-sets atom");
          return std::move(set.ranges);
        }
      }
      else if(Unicode()){
        const bool syntax=std::u16string_view(u"^$\\.*+?()[]{}|/").find(c)!=std::u16string_view::npos;
        const bool punctuator=in_class&&(c=='-'||(flags_.unicode_sets&&std::u16string_view(u"!#%&,:;<=>@`~\"'").find(c)!=std::u16string_view::npos));
        if(!syntax&&!punctuator)Fail("invalid Unicode identity escape");
      }
      if(std::u16string_view(u"dDsSwWtnrvfbB0xu\\^$.*+?()[]{}|/-").find(c)==std::u16string_view::npos)advanced_=true;
      return{
        {
          point,point
        }
      }
      ;
    }
    static Ranges Intersect(const Ranges& left,const Ranges& right){
      Ranges result;
      size_t a=0,b=0;
      while(a<left.size()&&b<right.size()){
        const auto first=std::max(left[a].first,right[b].first),last=std::min(left[a].second,right[b].second);
        if(first<=last)result.emplace_back(first,last);
        if(left[a].second<right[b].second)++a;
        else ++b;
      }
      return result;
    }
    static Ranges Subtract(const Ranges& left,const Ranges& right){
      Ranges result;
      size_t begin=0;
      for(const auto& [first,last]:left){
        uint32_t cursor=first;
        while(begin<right.size()&&right[begin].second<cursor)++begin;
        for(size_t index=begin;
        index<right.size()&&right[index].first<=last;
        ++index){
          if(cursor<right[index].first)result.emplace_back(cursor,right[index].first-1);
          cursor=std::max(cursor,right[index].second+1);
          if(cursor>last)break;
        }
        if(cursor<=last)result.emplace_back(cursor,last);
      }
      return result;
    }
    Set NormalizeSet(Set value){
      for(auto& string:value.strings){
        if(flags_.insensitive)for(auto& point:string)point=Canonical(point,flags_);
        if(string.size()==1)value.ranges.emplace_back(string.front(),string.front());
      }
      std::erase_if(value.strings,[](const auto& string){
        return string.size()==1;
      }
      );
      value.ranges=Normalize(std::move(value.ranges));
      if(!value.case_ready)value.ranges=flags_.unicode_sets?CaseClosure(std::move(value.ranges)):CanonicalRanges(value.ranges);
      value.case_ready=true;
      std::sort(value.strings.begin(),value.strings.end());
      value.strings.erase(std::unique(value.strings.begin(),value.strings.end()),value.strings.end());
      size_t points=0;
      for(const auto& string:value.strings){
        points+=string.size();
        if(points>262144)Fail("class string point capacity262144 exceeded");
      }
      if(value.ranges.size()>262144||value.strings.size()>8192)Fail("class set capacity exceeded");
      return value;
    }
    Set Union(Set left,Set right){
      left=NormalizeSet(std::move(left));
      right=NormalizeSet(std::move(right));
      left.ranges.insert(left.ranges.end(),right.ranges.begin(),right.ranges.end());
      left.strings.insert(left.strings.end(),right.strings.begin(),right.strings.end());
      left.may_strings=left.may_strings||right.may_strings;
      return NormalizeSet(std::move(left));
    }
    Set Combine(Set left,Set right,bool subtraction){
      left=NormalizeSet(std::move(left));
      right=NormalizeSet(std::move(right));
      Set result;
      result.case_ready=true;
      result.ranges=subtraction?Subtract(left.ranges,right.ranges):Intersect(left.ranges,right.ranges);
      if(subtraction)std::set_difference(left.strings.begin(),left.strings.end(),right.strings.begin(),right.strings.end(),std::back_inserter(result.strings));
      else std::set_intersection(left.strings.begin(),left.strings.end(),right.strings.begin(),right.strings.end(),std::back_inserter(result.strings));
      result.may_strings=subtraction?left.may_strings:(left.may_strings&&right.may_strings);
      return result;
    }
    Ranges ComplementCanonical(const Ranges& ranges)const{
      if(flags_.unicode_sets)return Complement(ranges);
      return Subtract(CanonicalRanges({
        {
          0,Unicode()?0x10ffffU:65535U
        }
      }
      ),ranges);
    }
    Set Property(bool negative){
      if(!Take('{'))Fail("missing Unicode property brace");
      std::string name;
      while(at_<source_.size()&&source_[at_]!='}'){
        const auto c=source_[at_++];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='='))Fail("invalid Unicode property alias");
        name+=static_cast<char>(c);
      }
      if(!Take('}'))Fail("unterminated Unicode property");
      const auto* property=grep_properties::Find(name);
      if(!property)Fail("unknown Unicode property alias");
      if(property->strings&&(!flags_.unicode_sets||negative))Fail("invalid Unicode string property mode or complement");
      Set result;
      result.may_strings=property->strings;
      for(size_t i=0;
      i<property->range_count;
      ++i){
        const auto& range=grep_properties::kRanges[property->first_range+i];
        result.ranges.emplace_back(range.first,range.last);
      }
      for(size_t i=0;
      i<property->string_count;
      ++i){
        const auto& string=grep_properties::kStrings[property->first_string+i];
        result.strings.emplace_back(grep_properties::kPoints.begin()+string.first,grep_properties::kPoints.begin()+string.first+string.count);
      }
      if(flags_.unicode_sets){
        result.case_ready=name=="ASCII";  // Public v+i property behavior, including through set operators.
        result=NormalizeSet(std::move(result));
        if(negative)result.ranges=ComplementCanonical(result.ranges);
      }
      else if(negative)result.ranges=Complement(result.ranges);
      return result;
    }
    Set ClassString(){
      if(!Take('{'))Fail("missing class string brace");
      Set result;
      result.case_ready=true;  // Singleton q-elements keep canonical scalar membership.
      String string;
      for(;
      ;
      ){
        if(at_==source_.size())Fail("unterminated class string");
        if(Take('}')){
          result.strings.push_back(std::move(string));
          break;
        }
        if(Take('|')){
          result.strings.push_back(std::move(string));
          string.clear();
          continue;
        }
        if(Take('\\')){
          if(at_==source_.size()||std::u16string_view(u"dDsSwWpP").find(source_[at_])!=std::u16string_view::npos)Fail("class string requires character escapes");
          const auto ranges=Escape(true);
          if(ranges.size()!=1||ranges[0].first!=ranges[0].second)Fail("class string requires character escapes");
          string.push_back(ranges[0].first);
        }
        else{
          if(std::u16string_view(u"()[]{} /-").find(source_[at_])!=std::u16string_view::npos&&source_[at_]!=' ')Fail("unescaped class string syntax");
          string.push_back(Point());
        }
      }
      for(const auto& value:result.strings)if(value.size()!=1)result.may_strings=true;
      return NormalizeSet(std::move(result));
    }
    Set UnicodeOperand(bool& character){
      character=false;
      if(at_==source_.size())Fail("unterminated Unicode class");
      if(Take('['))return UnicodeClass();
      if(Take('\\')){
        if(at_==source_.size())Fail("trailing Unicode class escape");
        if(Take('q'))return ClassString();
        if(source_[at_]=='p'||source_[at_]=='P')return Property(source_[at_++]=='P');
        const auto escape=source_[at_];
        auto ranges=Escape(true);
        character=std::u16string_view(u"dDsSwW").find(escape)==std::u16string_view::npos;
        return{
          std::move(ranges),{
          }
          ,false
        }
        ;
      }
      const auto c=source_[at_];
      if(std::u16string_view(u"(){}|/-]").find(c)!=std::u16string_view::npos)Fail("unescaped Unicode class syntax");
      if(at_+1<source_.size()&&source_[at_+1]==c&&std::u16string_view(u"!#$%&*+,.:;<=>?@^`~").find(c)!=std::u16string_view::npos)Fail("reserved double Unicode class punctuator");
      character=true;
      const auto point=Point();
      return{
        {
          {
            point,point
          }
        }
        ,{
        }
        ,false
      }
      ;
    }
    Set UnicodeClass(){
      advanced_=true;
      if(++class_depth_>64)Fail("Unicode class depth64 exceeded");
      const bool negative=Take('^');
      Set result;
      bool have=false,union_only=false,range_operand=false;
      char operation=0;
      while(at_<source_.size()&&source_[at_]!=']'){
        if(have&&at_+1<source_.size()&&((source_[at_]=='&'&&source_[at_+1]=='&')||(source_[at_]=='-'&&source_[at_+1]=='-'))){
          if(union_only||range_operand)Fail("Unicode set operator requires a single operand");
          const char next=static_cast<char>(source_[at_]);
          if(operation&&operation!=next)Fail("mixed Unicode class set operators");
          operation=next;
          at_+=2;
          if(at_==source_.size()||source_[at_]==']')Fail("missing Unicode class operand");
        }
        else if(have&&operation)Fail("Unicode set operator required");
        bool single=false;
        auto item=UnicodeOperand(single);
        if(at_<source_.size()&&source_[at_]=='-'&&(at_+1==source_.size()||source_[at_+1]!='-')){
          if(operation)Fail("range cannot be a Unicode set operator operand");
          range_operand=true;
          ++at_;
          bool last_single=false;
          auto last=UnicodeOperand(last_single);
          if(!single||!last_single||item.ranges.size()!=1||last.ranges.size()!=1||item.ranges[0].first!=item.ranges[0].second||last.ranges[0].first!=last.ranges[0].second)Fail("invalid Unicode class range");
          if(item.ranges[0].first>last.ranges[0].first)Fail("reversed Unicode class range");
          item=NormalizeSet({
            {
              {
                item.ranges[0].first,last.ranges[0].first
              }
            }
            ,{
            }
            ,false
          }
          );
        }
        if(!have){
          result=NormalizeSet(std::move(item));
          have=true;
        }
        else if(operation)result=Combine(std::move(result),std::move(item),operation=='-');
        else{
          union_only=true;
          result=Union(std::move(result),std::move(item));
        }
      }
      if(!Take(']'))Fail("unterminated Unicode class");
      --class_depth_;
      if(negative){
        if(result.may_strings)Fail("Unicode class strings cannot be complemented");
        result.ranges=ComplementCanonical(result.ranges);
      }
      return result;
    }
    size_t SetNode(Set value){
      value=NormalizeSet(std::move(value));
      Node node{
        Kind::Character
      }
      ;
      node.raw_ranges=flags_.unicode_sets;
      node.ranges=std::move(value.ranges);
      node.strings=std::move(value.strings);
      std::sort(node.strings.begin(),node.strings.end(),[](const auto& a,const auto& b){
        return a.size()!=b.size()?a.size()>b.size():a<b;
      }
      );
      return Add(std::move(node));
    }
    Ranges ClassItem(){
      if(at_==source_.size())Fail("unterminated character class");
      if(Take('\\'))return Escape(true);
      const auto point=Point();
      return{
        {
          point,point
        }
      }
      ;
    }
    size_t Class(){
      const bool negative=Take('^');
      Ranges ranges;
      while(at_<source_.size()&&source_[at_]!=']'){
        auto item=ClassItem();
        if(at_<source_.size()&&source_[at_]=='-'&&at_+1<source_.size()&&source_[at_+1]!=']'){
          ++at_;
          auto last=ClassItem();
          if(item.size()!=1||last.size()!=1||item[0].first!=item[0].second||last[0].first!=last[0].second){
            if(Unicode())Fail("class escape cannot be a range endpoint");
            advanced_=true;
            item.emplace_back('-','-');
            item.insert(item.end(),last.begin(),last.end());
          }
          else{
            if(item[0].first>last[0].first)Fail("reversed character range");
            item={
              {
                item[0].first,last[0].first
              }
            }
            ;
          }
        }
        ranges.insert(ranges.end(),item.begin(),item.end());
      }
      if(!Take(']'))Fail("unterminated character class");
      ranges=CanonicalRanges(Normalize(std::move(ranges)));
      if(negative)ranges=Complement(ranges);
      Node node{
        Kind::Character
      }
      ;
      node.ranges=std::move(ranges);
      return Add(std::move(node));
    }
    size_t Atom(){
      if(at_==source_.size())Fail("expected atom");
      const auto c=source_[at_++];
      if(c=='('){
        if(Take('?')){
          if(Take(':')){
            const auto body=Disjunction();
            if(!Take(')'))Fail("unterminated group");
            return body;
          }
          advanced_=true;
          bool behind=false,positive=true;
          if(Take('='))positive=true;
          else if(Take('!'))positive=false;
          else if(Take('<')){
            if(Take('=')){
              behind=true;
              positive=true;
            }
            else if(Take('!')){
              behind=true;
              positive=false;
            }
            else{
              const auto name=Name();
              const auto group=++capture_count_;
              names_[name].push_back({
                group,branch_path_
              }
              );
              const auto body=Disjunction();
              if(!Take(')'))Fail("unterminated capture");
              Node node{
                Kind::Capture
              }
              ;
              node.capture=group;
              node.children={
                body
              }
              ;
              return Add(std::move(node));
            }
          }
          else{
            const auto saved=flags_;
            bool remove=false,seen_any=false;
            std::set<char16_t> added,removed;
            while(at_<source_.size()&&source_[at_]!=':'){
              const auto flag=source_[at_++];
              if(flag=='-'&&!remove){
                remove=true;
                continue;
              }
              if(flag!='i'&&flag!='m'&&flag!='s')Fail("invalid scoped modifier");
              auto& seen=remove?removed:added;
              if(!seen.insert(flag).second||(added.contains(flag)&&removed.contains(flag)))Fail("duplicate scoped modifier");
              seen_any=true;
              const bool value=!remove;
              if(flag=='i')flags_.insensitive=value;
              else if(flag=='m')flags_.multiline=value;
              else flags_.dot_all=value;
            }
            if(!seen_any||!Take(':')||(remove&&removed.empty()))Fail("invalid scoped modifier group");
            const auto body=Disjunction();
            if(!Take(')'))Fail("unterminated scoped modifier");
            flags_=saved;
            return body;
          }
          const auto body=Disjunction();
          if(!Take(')'))Fail("unterminated assertion");
          Node node{
            Kind::Assert
          }
          ;
          node.positive=positive;
          node.behind=behind;
          node.children={
            body
          }
          ;
          return Add(std::move(node));
        }
        const auto group=++capture_count_,body=Disjunction();
        if(!Take(')'))Fail("unterminated capture");
        Node node{
          Kind::Capture
        }
        ;
        node.capture=group;
        node.children={
          body
        }
        ;
        return Add(std::move(node));
      }
      if(c=='[')return flags_.unicode_sets?SetNode(UnicodeClass()):Class();
      if(c=='^')return Add({
        Kind::Begin
      }
      );
      if(c=='$')return Add({
        Kind::End
      }
      );
      if(c=='*'||c=='+'||c=='?')Fail("quantifier without atom");
      if(c=='{'||c=='}'||c==']'){
        if(Unicode())Fail("unescaped Unicode syntax character");
        advanced_=true;
      }
      if(c=='\\'){
        if(!Unicode()&&at_<source_.size()&&source_[at_]=='c'&&(at_+1==source_.size()||!((source_[at_+1]>='A'&&source_[at_+1]<='Z')||(source_[at_+1]>='a'&&source_[at_+1]<='z')))){
          advanced_=true;
          return Character({
            {
              '\\','\\'
            }
          }
          );
        }
        if(has_named_&&at_<source_.size()&&source_[at_]=='k'&&(at_+1==source_.size()||source_[at_+1]!='<'))Fail("invalid named reference");
        if(Unicode()&&at_<source_.size()&&(source_[at_]=='p'||source_[at_]=='P')){
          advanced_=true;
          return SetNode(Property(source_[at_++]=='P'));
        }
        if(at_<source_.size()&&(source_[at_]=='b'||source_[at_]=='B'))return Add({
          source_[at_++]=='b'?Kind::Boundary:Kind::NonBoundary
        }
        );
        if(at_<source_.size()&&source_[at_]>='1'&&source_[at_]<='9'){
          const auto saved=at_;
          const auto number=Number();
          if(number<=total_captures_){
            advanced_=true;
            Node node{
              Kind::Reference
            }
            ;
            node.capture=number;
            return Add(std::move(node));
          }
          at_=saved;
          if(Unicode())Fail("reference exceeds capture count");
        }
        if((Unicode()||has_named_)&&at_<source_.size()&&source_[at_]=='k'&&at_+1<source_.size()&&source_[at_+1]=='<'){
          advanced_=true;
          at_+=2;
          Node node{
            Kind::Reference
          }
          ;
          node.reference=Name();
          return Add(std::move(node));
        }
        return Character(Escape(false));
      }
      if(c=='.')return Character(flags_.dot_all?Ranges{
        {
          0,Unicode()?0x10ffffU:65535U
        }
      }
      :Ranges{
        {
          0,9
        }
        ,{
          11,12
        }
        ,{
          14,0x2027
        }
        ,{
          0x202a,Unicode()?0x10ffffU:65535U
        }
      }
      );
      --at_;
      const auto point=Point();
      return Character({
        {
          point,point
        }
      }
      );
    }
    std::pair<uint64_t,uint64_t> Estimate(size_t id)const{
      const auto& node=nodes_[id];
      uint64_t states=0,ranges=0;
      if(node.kind==Kind::Character)return{
        1,node.ranges.size()
      }
      ;
      if(node.children.empty())return{
        1,0
      }
      ;
      for(const auto child:node.children){
        const auto size=Estimate(child);
        states=std::min<uint64_t>(2048,states+size.first);
        ranges=std::min<uint64_t>(16384,ranges+size.second);
      }
      if(node.kind==Kind::Repeat){
        const auto copies=node.maximum==infinity?std::min<uint64_t>(2048,node.minimum)+1:std::min<uint64_t>(2048,node.maximum);
        states=std::min<uint64_t>(2048,(states+1)*copies+2);
        ranges=std::min<uint64_t>(16384,ranges*copies);
      }
      else if(node.kind==Kind::Alternative)states=std::min<uint64_t>(2048,states+node.children.size());
      return{
        states,ranges
      }
      ;
    }
    void ResolveReferences(){
      if(capture_count_!=total_captures_)Fail("capture count parser mismatch");
      for(const auto& [name,groups]:names_){
        (void)name;
        for(size_t a=0;
        a<groups.size();
        ++a)for(size_t b=a+1;
        b<groups.size();
        ++b){
          bool disjoint=false;
          for(const auto& left:groups[a].branches)for(const auto& right:groups[b].branches)if(left.first==right.first&&left.second!=right.second)disjoint=true;
          if(!disjoint)Fail("duplicate capture names can match in the same alternative");
        }
      }
      for(auto& node:nodes_)if(node.kind==Kind::Reference&&!node.reference.empty()){
        const auto found=names_.find(node.reference);
        if(found==names_.end())Fail("unknown named reference");
        for(const auto& group:found->second)node.captures.push_back(group.group);
      }
    }
    void CollectCaptures(size_t id,std::vector<size_t>& captures)const{
      const auto& node=nodes_[id];
      if(node.kind==Kind::Capture)captures.push_back(node.capture);
      for(const auto child:node.children)CollectCaptures(child,captures);
    }
    size_t Emit(Instruction instruction){
      if(code_.size()>=32768)Fail("instruction capacity32768 exceeded");
      code_.push_back(instruction);
      return code_.size()-1;
    }
    size_t Compile(size_t id,int direction,size_t next){
      const auto& node=nodes_[id];
      switch(node.kind){
        case Kind::Empty:return next;
        case Kind::Sequence:{
          if(direction>0){
            for(auto it=node.children.rbegin();
            it!=node.children.rend();
            ++it)next=Compile(*it,direction,next);
          }
          else for(const auto child:node.children)next=Compile(child,direction,next);
          return next;
        }
        case Kind::Alternative:{
          auto current=Compile(node.children.back(),direction,next);
          for(size_t i=node.children.size()-1;
          i>0;
          --i)current=Emit({
            Op::Split,Compile(node.children[i-1],direction,next),current
          }
          );
          return current;
        }
        case Kind::Capture:{
          const auto end=Emit({
            Op::SaveEnd,next,none,id,node.capture,direction
          }
          );
          const auto body=Compile(node.children.front(),direction,end);
          return Emit({
            Op::SaveStart,body,none,id,node.capture,direction
          }
          );
        }
        case Kind::Assert:{
          const auto child=Compile(node.children.front(),node.behind?-1:1,Emit({
            Op::Accept
          }
          ));
          return Emit({
            Op::Assert,next,child,id
          }
          );
        }
        case Kind::Repeat:{
          const auto decide=Emit({
            Op::LoopDecide,none,next,id,node.loop,direction
          }
          );
          const auto step=Emit({
            Op::LoopStep,decide,none,id,node.loop,direction
          }
          );
          const auto body=Compile(node.children.front(),direction,step);
          const auto clear=Emit({
            Op::Clear,body,none,id
          }
          );
          const auto begin=Emit({
            Op::LoopStart,clear,none,id,node.loop
          }
          );
          code_[decide].next=begin;
          return Emit({
            Op::LoopInit,decide,none,id,node.loop
          }
          );
        }
        case Kind::Character:range_count_+=node.ranges.size();
        for(const auto& string:node.strings)range_count_+=string.size()+1;
        if(range_count_>262144)Fail("range capacity262144 exceeded");
        return Emit({
          Op::Character,next,none,id,0,direction
        }
        );
        case Kind::Reference:return Emit({
          Op::Reference,next,none,id,node.capture,direction
        }
        );
        case Kind::Begin:return Emit({
          Op::Begin,next,none,id
        }
        );
        case Kind::End:return Emit({
          Op::End,next,none,id
        }
        );
        case Kind::Boundary:return Emit({
          Op::Boundary,next,none,id
        }
        );
        case Kind::NonBoundary:return Emit({
          Op::NonBoundary,next,none,id
        }
        );
      }
      Fail("unknown AST kind");
    }
    static bool Contains(const Ranges& ranges,uint32_t point,GrepWork& work){
      size_t first=0,last=ranges.size();
      while(first<last){
        work.Charge();
        const auto middle=first+(last-first)/2;
        if(point<ranges[middle].first)last=middle;
        else if(point>ranges[middle].second)first=middle+1;
        else return true;
      }
      return false;
    }
    std::optional<State> Run(State state,const std::vector<uint32_t>& input,GrepWork& work,Memory& memory,unsigned depth)const{
      if(depth>64)Fail("assertion depth64 exceeded");
      const size_t snapshot=sizeof(State)+state.captures.size()*sizeof(Capture)+state.loops.size()*sizeof(Loop);
      Retained active(memory,snapshot);
      std::vector<State> alternatives;
      size_t retained=0;
      struct Release {
        Memory& memory;
        size_t& bytes;
        ~Release(){
          memory.bytes-=bytes;
        }
      }
      release{
        memory,retained
      }
      ;
      const auto push=[&](size_t pc,size_t position=none){
        work.Charge(state.captures.size()+state.loops.size()+1);
        if(snapshot>32*1024*1024-memory.bytes)Fail("retained matcher memory capacity32MiB payload exceeded");
        memory.bytes+=snapshot;
        retained+=snapshot;
        alternatives.push_back(state);
        alternatives.back().pc=pc;
        if(position!=none)alternatives.back().position=position;
      }
      ;
      for(;
      ;
      ){
        work.Charge();
        const auto& instruction=code_.at(state.pc);
        const auto* node=instruction.node==none?nullptr:&nodes_[instruction.node];
        bool failed=false;
        switch(instruction.op){
          case Op::Accept:return state;
          case Op::Character:{
            std::vector<size_t> positions;
            for(const auto& string:node->strings){
              work.Charge();
              const bool available=instruction.direction>0?string.size()<=input.size()-state.position:string.size()<=state.position;
              if(!available)continue;
              const auto origin=instruction.direction>0?state.position:state.position-string.size();
              bool matches=true;
              for(size_t i=0;
              i<string.size();
              ++i){
                work.Charge();
                if(Canonical(input[origin+i],node->flags)!=string[i]){
                  matches=false;
                  break;
                }
              }
              if(matches)positions.push_back(instruction.direction>0?state.position+string.size():state.position-string.size());
            }
            const bool available=instruction.direction>0?state.position<input.size():state.position>0;
            if(available&&Contains(node->ranges,node->raw_ranges?input[instruction.direction>0?state.position:state.position-1]:Canonical(input[instruction.direction>0?state.position:state.position-1],node->flags),work))positions.push_back(instruction.direction>0?state.position+1:state.position-1);
            if(positions.empty())failed=true;
            else{
              for(size_t i=positions.size()-1;
              i>0;
              --i)push(instruction.next,positions[i]);
              state.position=positions.front();
              state.pc=instruction.next;
            }
            break;
          }
          case Op::Split:push(instruction.other);
          state.pc=instruction.next;
          break;
          case Op::SaveStart:if(instruction.direction>0)state.captures[instruction.group].first=state.position;
          else state.captures[instruction.group].last=state.position;
          state.pc=instruction.next;
          break;
          case Op::SaveEnd:if(instruction.direction>0)state.captures[instruction.group].last=state.position;
          else state.captures[instruction.group].first=state.position;
          state.pc=instruction.next;
          break;
          case Op::Clear:for(const auto group:node->captures){
            work.Charge();
            state.captures[group]={
            }
            ;
          }
          state.pc=instruction.next;
          break;
          case Op::Assert:{
            work.Charge(state.captures.size()+state.loops.size());
            State child=state;
            child.pc=instruction.other;
            auto found=Run(std::move(child),input,work,memory,depth+1);
            if(found.has_value()!=node->positive)failed=true;
            else{
              if(found)state.captures=std::move(found->captures);
              state.pc=instruction.next;
            }
            break;
          }
          case Op::Reference:{
            Capture capture=state.captures[instruction.group];
            for(const auto group:node->captures){
              work.Charge();
              if(state.captures[group].first!=none&&state.captures[group].last!=none){
                capture=state.captures[group];
                break;
              }
            }
            if(capture.first==none||capture.last==none){
              state.pc=instruction.next;
              break;
            }
            const auto length=capture.last-capture.first;
            const bool available=instruction.direction>0?length<=input.size()-state.position:length<=state.position;
            if(!available){
              failed=true;
              break;
            }
            const auto origin=instruction.direction>0?state.position:state.position-length;
            for(size_t i=0;
            i<length;
            ++i){
              work.Charge();
              if(Canonical(input[capture.first+i],node->flags)!=Canonical(input[origin+i],node->flags)){
                failed=true;
                break;
              }
            }
            if(!failed){
              state.position=instruction.direction>0?state.position+length:state.position-length;
              state.pc=instruction.next;
            }
            break;
          }
          case Op::Begin:if(state.position==0||(node->flags.multiline&&Line(input[state.position-1])))state.pc=instruction.next;
          else failed=true;
          break;
          case Op::End:if(state.position==input.size()||(node->flags.multiline&&Line(input[state.position])))state.pc=instruction.next;
          else failed=true;
          break;
          case Op::Boundary:case Op::NonBoundary:{
            const bool boundary=(state.position>0&&Word(input[state.position-1],node->flags))!=(state.position<input.size()&&Word(input[state.position],node->flags));
            if(boundary==(instruction.op==Op::Boundary))state.pc=instruction.next;
            else failed=true;
            break;
          }
          case Op::LoopInit:state.loops[instruction.group]={
          }
          ;
          state.pc=instruction.next;
          break;
          case Op::LoopStart:state.loops[instruction.group].position=state.position;
          state.pc=instruction.next;
          break;
          case Op::LoopDecide:{
            const auto count=state.loops[instruction.group].count;
            if(count<node->minimum)state.pc=instruction.next;
            else if(count>=node->maximum)state.pc=instruction.other;
            else if(node->greedy){
              push(instruction.other);
              state.pc=instruction.next;
            }
            else{
              push(instruction.next);
              state.pc=instruction.other;
            }
            break;
          }
          case Op::LoopStep:{
            auto& loop=state.loops[instruction.group];
            if(state.position==loop.position&&loop.count>=node->minimum)failed=true;
            else{
              ++loop.count;
              state.pc=instruction.next;
            }
            break;
          }
        }
        if(failed){
          if(alternatives.empty())return std::nullopt;
          state=std::move(alternatives.back());
          alternatives.pop_back();
          memory.bytes-=snapshot;
          retained-=snapshot;
        }
      }
    }
    std::u16string source_;
    Flags flags_;
    bool advanced_=false,has_named_=false;
    size_t at_=0,depth_=0,total_captures_=0,capture_count_=0,loop_count_=0,start_=0,root_=0,range_count_=0,class_depth_=0,ast_payload_=0;
    struct NamedCapture {
      size_t group;
      std::vector<std::pair<size_t,size_t>> branches;
    }
    ;
    size_t disjunction_count_=0;
    std::vector<std::pair<size_t,size_t>> branch_path_;
    std::map<std::u16string,std::vector<NamedCapture>> names_;
    std::vector<Node> nodes_;
    std::vector<Instruction> code_;
  }
  ;
}
