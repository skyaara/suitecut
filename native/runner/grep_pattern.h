// SPDX-License-Identifier: MIT
#pragma once
#include "grep_unicode_data.h"
#include "grep_work.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace suitecut::runner {
// Boolean ECMAScript non-Unicode matching over UTF16 code units. A Thompson
// automaton bounds work even for nested ambiguous repetitions. Unsupported
// syntax fails during compilation; it never falls through to another engine.
class GrepPattern {
 public:
  static constexpr size_t kPatternUnits=4096,kStates=2048,kTitleUnits=32768;
  struct Options {bool insensitive=false,multiline=false,dot_all=false,sticky=false;};
  using Work=GrepWork;
  explicit GrepPattern(std::u16string source,bool insensitive=false):GrepPattern(std::move(source),Options{insensitive,false,false,false}){}
  GrepPattern(std::u16string source,Options options):source_(std::move(source)),insensitive_(options.insensitive),options_(options){
    if(source_.size()>kPatternUnits)Fail("pattern capacity4096 exceeded");
    const auto root=Disjunction();if(position_!=source_.size())Fail("unexpected closing delimiter");
    auto expression=Compile(root);const auto accept=Emit({Kind::Accept});Patch(expression.ends,accept);start_=expression.start;
    nodes_.clear();nodes_.shrink_to_fit();
  }
  bool Matches(std::u16string_view title,Work& work)const{
    if(title.size()>kTitleUnits)Fail("title capacity32768 UTF16 units exceeded");
    // A stop callback may synchronously match again using the same budget.
    // Lease owner-local storage while keeping nested matching independent.
    GrepNFAScratch nested;
    auto& scratch=work.nfa_scratch.active?nested:work.nfa_scratch;
    struct Release {GrepNFAScratch& scratch;~Release(){scratch.active=false;}} release{scratch};
    scratch.active=true;
    auto& current=scratch.current;auto& next=scratch.next;auto& stack=scratch.stack;auto& seen=scratch.seen;
    current.clear();next.clear();stack.clear();
    current.reserve(states_.size());next.reserve(states_.size());stack.reserve(2*states_.size()+1);
    seen.assign(states_.size(),0);uint32_t generation=0;
    const auto closure=[&](std::vector<size_t>& output,size_t initial,size_t at){
      stack.push_back(initial);
      while(!stack.empty()){
        const auto id=stack.back();stack.pop_back();work.Charge();
        if(seen[id]==generation)continue;
        seen[id]=generation;
        const auto& state=states_[id];
        if(state.kind==Kind::Split){stack.push_back(state.first);stack.push_back(state.second);}
        else if(state.kind==Kind::Jump)stack.push_back(state.first);
        else if(state.kind==Kind::Begin){if(at==0||(options_.multiline&&Line(title[at-1])))stack.push_back(state.first);}
        else if(state.kind==Kind::End){if(at==title.size()||(options_.multiline&&Line(title[at])))stack.push_back(state.first);}
        else if(state.kind==Kind::Boundary||state.kind==Kind::NonBoundary){
          const bool boundary=(at&&Word(title[at-1]))!=(at<title.size()&&Word(title[at]));
          if(boundary==(state.kind==Kind::Boundary))stack.push_back(state.first);
        }else output.push_back(id);
      }
    };
    for(size_t at=0;;++at){
      // Existing active states already have this generation; add a fresh search
      // origin to the same closure so shared states remain deduplicated.
      if(!generation)++generation;
      if(!options_.sticky||at==0)closure(current,start_,at);
      for(const auto id:current)if(states_[id].kind==Kind::Accept)return true;
      if(at==title.size())return false;
      next.clear();++generation;
      auto unit=static_cast<uint16_t>(title[at]);if(insensitive_)unit=Fold(unit);
      for(const auto id:current){work.Charge();const auto& state=states_[id];
        if(state.kind==Kind::Character&&Contains(state.ranges,unit,work))closure(next,state.first,at+1);
      }
      current.swap(next);
    }
  }
  const std::u16string& Source()const{return source_;}
  bool Insensitive()const{return insensitive_;}
  size_t StateCount()const{return states_.size();}
  size_t RangeCount()const{return range_count_;}
 private:
  using Range=std::pair<uint16_t,uint16_t>;using Ranges=std::vector<Range>;
  enum class Kind{Empty,Character,Begin,End,Boundary,NonBoundary,Concat,Alternative,Repeat,Jump,Split,Accept};
  static constexpr size_t none=std::numeric_limits<size_t>::max();
  struct Node{Kind kind;size_t left=none,right=none;unsigned minimum=0,maximum=0;Ranges ranges{};};
  struct State{Kind kind;size_t first=none,second=none;Ranges ranges{};};
  struct Edge{size_t state;bool second=false;};
  struct Fragment{size_t start;std::vector<Edge> ends;};
  [[noreturn]] static void Fail(const char* detail){throw std::invalid_argument(std::string("Native grep: ")+detail);}
  static bool Line(uint16_t c){return c==10||c==13||c==0x2028||c==0x2029;}
  static bool Word(uint16_t c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}
  static uint16_t Fold(uint16_t c){return grep_unicode::Legacy(c);}
  static bool Contains(const Ranges& ranges,uint16_t unit,Work& work){
    size_t first=0,last=ranges.size();
    while(first<last){work.Charge();const auto middle=first+(last-first)/2;
      if(unit<ranges[middle].first)last=middle;
      else if(unit>ranges[middle].second)first=middle+1;
      else return true;
    }
    return false;
  }
  static Ranges Normalize(Ranges input){
    std::sort(input.begin(),input.end());Ranges result;
    for(const auto& range:input){if(range.first>range.second)Fail("reversed character range");
      if(!result.empty()&&static_cast<unsigned>(range.first)<=static_cast<unsigned>(result.back().second)+1)result.back().second=std::max(result.back().second,range.second);
      else result.push_back(range);
    }return result;
  }
  static Ranges Invert(const Ranges& ranges){Ranges result;unsigned next=0;
    for(const auto& [first,last]:ranges){if(next<first)result.emplace_back(next,first-1);next=static_cast<unsigned>(last)+1;}
    if(next<=65535)result.emplace_back(next,65535);
    return result;
  }
  Ranges CaseRanges(Ranges ranges)const{return FoldRanges(Normalize(std::move(ranges)));}
  Ranges FoldRanges(const Ranges& ranges)const{
    if(!insensitive_)return ranges;
    Ranges result;
    // Project each original interval through the exact Canonicalize mapping.
    // Identity spans stay intervals; nonidentity keys contribute their mapped
    // unit. Mapping endpoints alone would lose symbols/range holes.
    for(const auto& [first,last]:ranges){
      uint32_t cursor=first;
      auto found=std::lower_bound(grep_unicode::kLegacyMappings.begin(),grep_unicode::kLegacyMappings.end(),first,
        [](const auto& mapping,uint16_t unit){return mapping.from<unit;});
      for(;found!=grep_unicode::kLegacyMappings.end()&&found->from<=last;++found){
        if(cursor<found->from)result.emplace_back(cursor,found->from-1);
        result.emplace_back(found->to,found->to);cursor=found->from+1;
      }
      if(cursor<=last)result.emplace_back(cursor,last);
    }
    return Normalize(std::move(result));
  }
  size_t Add(Node node){if(nodes_.size()>=4096)Fail("syntax node capacity4096 exceeded");nodes_.push_back(std::move(node));return nodes_.size()-1;}
  size_t Binary(Kind kind,size_t left,size_t right){Node node{kind};node.left=left;node.right=right;return Add(std::move(node));}
  bool Take(char16_t c){if(position_<source_.size()&&source_[position_]==c){++position_;return true;}return false;}
  size_t Disjunction(){
    if(++depth_>64)Fail("group nesting capacity64 exceeded");
    auto result=Sequence();while(Take('|'))result=Binary(Kind::Alternative,result,Sequence());--depth_;return result;
  }
  size_t Sequence(){size_t result=none;
    while(position_<source_.size()&&source_[position_]!=')'&&source_[position_]!='|'){
      const auto atom=Term();result=result==none?atom:Binary(Kind::Concat,result,atom);
    }return result==none?Add({Kind::Empty}):result;
  }
  unsigned Number(){
    if(position_==source_.size()||source_[position_]<'0'||source_[position_]>'9')Fail("bounded quantifier requires a number");
    unsigned value=0;while(position_<source_.size()&&source_[position_]>='0'&&source_[position_]<='9'){
      value=value*10+(source_[position_++]-'0');if(value>256)Fail("repeat capacity256 exceeded");}return value;
  }
  size_t Term(){const auto atom=Atom();if(position_==source_.size())return atom;
    const auto c=source_[position_];unsigned minimum=0,maximum=0;bool repeat=true;
    if(c=='*'){++position_;maximum=std::numeric_limits<unsigned>::max();}
    else if(c=='+'){++position_;minimum=1;maximum=std::numeric_limits<unsigned>::max();}
    else if(c=='?'){++position_;maximum=1;}
    else if(c=='{'){++position_;minimum=Number();maximum=minimum;
      if(Take(','))maximum=position_<source_.size()&&source_[position_]=='}'?std::numeric_limits<unsigned>::max():Number();
      if(!Take('}')||maximum<minimum)Fail("invalid bounded quantifier");
    }else repeat=false;
    if(!repeat)return atom;
    const auto kind=nodes_[atom].kind;
    if(kind==Kind::Begin||kind==Kind::End||kind==Kind::Boundary||kind==Kind::NonBoundary)Fail("assertion quantification is unsupported");
    (void)Take('?');Node node{Kind::Repeat};node.left=atom;node.minimum=minimum;node.maximum=maximum;return Add(std::move(node));
  }
  uint16_t Hex(unsigned count){unsigned value=0;
    for(unsigned i=0;i<count;++i){if(position_==source_.size())Fail("truncated hexadecimal escape");const auto c=source_[position_++];unsigned digit;
      if(c>='0'&&c<='9')digit=c-'0';else if(c>='a'&&c<='f')digit=c-'a'+10;else if(c>='A'&&c<='F')digit=c-'A'+10;else Fail("invalid hexadecimal escape");value=value*16+digit;
    }return static_cast<uint16_t>(value);
  }
  Ranges Escape(bool in_class,bool raw=false){if(position_==source_.size())Fail("trailing escape");const auto c=source_[position_++];Ranges ranges;
    if(c=='d'||c=='D')ranges={{'0','9'}};
    else if(c=='w'||c=='W')ranges={{'0','9'},{'A','Z'},{'_','_'},{'a','z'}};
    else if(c=='s'||c=='S')ranges={{9,13},{32,32},{160,160},{0x1680,0x1680},{0x2000,0x200a},{0x2028,0x2029},{0x202f,0x202f},{0x205f,0x205f},{0x3000,0x3000},{0xfeff,0xfeff}};
    if(!ranges.empty()){
      // Builtin classes are invariant under legacy ASCII case folding.
      if(insensitive_&&(c=='w'||c=='W'))ranges={{'0','9'},{'A','Z'},{'_','_'},{'a','z'}};
      ranges=Normalize(std::move(ranges));return c=='D'||c=='W'||c=='S'?Invert(ranges):ranges;
    }
    uint16_t value=c;
    if(c=='x')value=Hex(2);else if(c=='u')value=Hex(4);
    else if(c=='n')value=10;else if(c=='r')value=13;else if(c=='t')value=9;else if(c=='v')value=11;else if(c=='f')value=12;
    else if(c=='b'&&in_class)value=8;
    else if(c=='0'){if(position_<source_.size()&&source_[position_]>='0'&&source_[position_]<='9')Fail("legacy octal escapes are unsupported");value=0;}
    else if(std::u16string_view(u"^$\\.*+?()[]{}|/-").find(c)==std::u16string_view::npos)Fail("unsupported escape or backreference");
    if(raw)return{{value,value}};
    return CaseRanges({{value,value}});
  }
  Ranges ClassItem(){if(position_==source_.size())Fail("unterminated character class");if(Take('\\'))return Escape(true,true);const auto c=source_[position_++];return{{c,c}};}
  size_t CharacterClass(){const bool negative=Take('^');Ranges ranges;
    while(position_<source_.size()&&source_[position_]!=']'){
      auto item=ClassItem();
      if(position_<source_.size()&&source_[position_]=='-'&&position_+1<source_.size()&&source_[position_+1]!=']'){
        ++position_;auto last=ClassItem();
        if(item.size()!=1||last.size()!=1||item[0].first!=item[0].second||last[0].first!=last[0].second)Fail("class escape range endpoints are unsupported");
        if(item[0].first>last[0].first)Fail("reversed character range");
        item={{item[0].first,last[0].first}};
      }
      ranges.insert(ranges.end(),item.begin(),item.end());if(ranges.size()>4096)Fail("character class range capacity4096 exceeded");
    }
    if(!Take(']'))Fail("unterminated character class");
    ranges=FoldRanges(Normalize(std::move(ranges)));if(negative)ranges=Invert(ranges);
    Node node{Kind::Character};node.ranges=std::move(ranges);return Add(std::move(node));
  }
  size_t Atom(){const auto c=source_[position_++];
    if(c=='('){if(Take('?')&&!Take(':'))Fail("lookaround, named groups and inline flags are unsupported");const auto result=Disjunction();if(!Take(')'))Fail("unterminated group");return result;}
    if(c=='[')return CharacterClass();
    if(c=='^')return Add({Kind::Begin});
    if(c=='$')return Add({Kind::End});
    if(c=='*'||c=='+'||c=='?'||c=='{'||c=='}'||c==']')Fail("unexpected quantifier or delimiter");
    if(c=='\\'&&position_<source_.size()&&(source_[position_]=='b'||source_[position_]=='B'))return Add({source_[position_++]=='b'?Kind::Boundary:Kind::NonBoundary});
    Node node{Kind::Character};
    if(c=='.')node.ranges=options_.dot_all?Ranges{{0,65535}}:Ranges{{0,9},{11,12},{14,0x2027},{0x202a,65535}};
    else if(c=='\\')node.ranges=Escape(false);
    else node.ranges=CaseRanges({{c,c}});
    return Add(std::move(node));
  }
  size_t Emit(State state){if(states_.size()>=kStates)Fail("automaton state capacity2048 exceeded");
    if(state.ranges.size()>16384-range_count_)Fail("automaton range capacity16384 exceeded");
    range_count_+=state.ranges.size();
    states_.push_back(std::move(state));return states_.size()-1;
  }
  void Patch(const std::vector<Edge>& edges,size_t target){for(const auto& edge:edges)(edge.second?states_[edge.state].second:states_[edge.state].first)=target;}
  Fragment Join(Fragment left,Fragment right){Patch(left.ends,right.start);return{left.start,std::move(right.ends)};}
  Fragment Empty(){const auto state=Emit({Kind::Jump});return{state,{{state}}};}
  Fragment Compile(size_t id){const auto& node=nodes_[id];
    if(node.kind==Kind::Empty)return Empty();
    if(node.kind==Kind::Concat)return Join(Compile(node.left),Compile(node.right));
    if(node.kind==Kind::Alternative){auto left=Compile(node.left),right=Compile(node.right);const auto split=Emit({Kind::Split,left.start,right.start});left.ends.insert(left.ends.end(),right.ends.begin(),right.ends.end());return{split,std::move(left.ends)};}
    if(node.kind==Kind::Repeat){auto result=Empty();for(unsigned i=0;i<node.minimum;++i)result=Join(std::move(result),Compile(node.left));
      if(node.maximum==std::numeric_limits<unsigned>::max()){
        auto child=Compile(node.left);const auto split=Emit({Kind::Split,child.start});Patch(child.ends,split);result=Join(std::move(result),{split,{{split,true}}});
      }else for(unsigned i=node.minimum;i<node.maximum;++i){auto child=Compile(node.left);const auto split=Emit({Kind::Split,child.start});child.ends.push_back({split,true});result=Join(std::move(result),{split,std::move(child.ends)});}
      return result;
    }
    State state{node.kind};state.ranges=node.ranges;const auto emitted=Emit(std::move(state));return{emitted,{{emitted}}};
  }
  std::u16string source_;bool insensitive_;Options options_;size_t position_=0,depth_=0,start_=0,range_count_=0;std::vector<Node> nodes_;std::vector<State> states_;
};

// Stock public CLI recognizes slash delimiters only with g/i suffixes. A bare
// argument uses gi. Matching is boolean and fresh per title, so g changes no
// selection state. Native does not claim public config RegExp/array support.
inline GrepPattern CompileCLIGrepNFA(std::u16string argument){
  if(argument.size()>GrepPattern::kPatternUnits)throw std::invalid_argument("Native grep CLI argument capacity4096 exceeded");
  bool insensitive=true;
  if(!argument.empty()&&argument.front()=='/'){
    const auto slash=argument.rfind('/');
    if(slash>0&&slash!=std::u16string::npos){
      const auto flags=argument.substr(slash+1);bool recognized=true;
      for(const auto flag:flags)if(flag!='g'&&flag!='i')recognized=false;
      if(recognized){bool global=false;insensitive=false;
        for(const auto flag:flags){if(flag=='i'){if(insensitive)throw std::invalid_argument("Native grep duplicate i flag");insensitive=true;}
          else{if(global)throw std::invalid_argument("Native grep duplicate g flag");global=true;}}
        argument=argument.substr(1,slash-1);
      }
    }
  }
  return GrepPattern(std::move(argument),insensitive);
}
}  // namespace suitecut::runner
