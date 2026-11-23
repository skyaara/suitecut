// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace suitecut {
// Authored URL glob matching. ? is a literal URL query separator; \ quotes
// the following character, * stays in a component, ** may cross separators.
inline bool NativeURLGlobSimple(const std::string& pattern, const std::string& url) {
  if (pattern.find_first_of("*\\") == std::string::npos) return pattern == url;
  std::vector<unsigned char> previous(url.size() + 1), next(url.size() + 1);
  previous[0] = 1;
  for (size_t p = 0; p < pattern.size(); ++p) {
    std::fill(next.begin(), next.end(), 0);
    if (pattern[p] == '*') {
      const bool deep = p + 1 < pattern.size() && pattern[p + 1] == '*';
      if (deep) while (p + 1 < pattern.size() && pattern[p + 1] == '*') ++p;
      next[0] = previous[0];
      for (size_t i = 1; i <= url.size(); ++i)
        next[i] = previous[i] || (next[i - 1] && (deep || url[i - 1] != '/'));
    } else if (pattern[p] == '\\' && p + 1 < pattern.size()) {
      const char literal = pattern[++p];
      for (size_t i = 1; i <= url.size(); ++i) next[i] = previous[i - 1] && literal == url[i - 1];
    } else {
      for (size_t i = 1; i <= url.size(); ++i) next[i] = previous[i - 1] && pattern[p] == url[i - 1];
    }
    previous.swap(next);
  }
  return previous.back();
}
// Brace alternatives allow common **/*.{png,jpg} route patterns. This is the
// reference expansion: it is exponential in the number of groups, so request
// paths and registration use NativeURLGlob, which never expands.
inline bool NativeURLGlobExpand(const std::string& input, std::vector<std::string>& output) {
  std::vector<std::string> pending{input};
  while (!pending.empty()) {
    auto pattern = std::move(pending.back()); pending.pop_back();
    bool expanded = false;
    for (size_t begin = 0; begin < pattern.size(); ++begin) {
      if (pattern[begin] == '\\') { if (begin + 1 < pattern.size()) ++begin; continue; }
      if (pattern[begin] != '{') continue;
      size_t end = begin + 1, start = end;
      unsigned depth = 0;
      std::vector<std::string> choices;
      for (; end < pattern.size(); ++end) {
        if (pattern[end] == '\\') { if (end + 1 < pattern.size()) ++end; continue; }
        if (pattern[end] == '{') ++depth;
        else if (pattern[end] == '}' && depth) --depth;
        else if (pattern[end] == '}' && !depth) break;
        else if (pattern[end] == ',' && !depth) {
          choices.push_back(pattern.substr(start, end - start)); start = end + 1;
        }
      }
      if (end == pattern.size() || choices.empty()) continue;
      choices.push_back(pattern.substr(start, end - start));
      for (const auto& choice : choices)
        pending.push_back(pattern.substr(0, begin) + choice + pattern.substr(end + 1));
      expanded = true; break;
    }
    if (!expanded) output.push_back(std::move(pattern));
  }
  return true;
}
// Authored URL glob compiled once into a position NFA: one node per pattern
// character, brace groups become a split over their alternatives. Matching is
// O(pattern*url) and never materialises alternatives, yet accepts exactly the
// URLs some NativeURLGlobExpand alternative accepts under NativeURLGlobSimple,
// including star runs that only form ** across a group edge (*{*,a}).
class NativeURLGlob {
 public:
  NativeURLGlob()=default;
  explicit NativeURLGlob(std::string pattern):pattern_(std::move(pattern)){Compile();}
  const std::string& pattern()const{return pattern_;}
  bool Matches(const std::string& url)const{
    if(literal_)return pattern_==url;
    // A state is node*3+run: run counts the unescaped stars just passed (2 = **).
    // Stars consume only where their run ends, exactly as one Simple token.
    std::vector<size_t> seen(nodes_.size()*3,0),current,next,stack;size_t stamp=1;
    const auto closure=[&](size_t start,std::vector<size_t>&out){
      stack.push_back(start);
      while(!stack.empty()){
        const size_t state=stack.back();stack.pop_back();
        if(seen[state]==stamp)continue;
        seen[state]=stamp;const auto&node=nodes_[state/3];const size_t run=state%3;
        if(node.kind==kStar)stack.push_back(node.a*3+std::min<size_t>(run+1,2));
        else if(node.kind==kJump)stack.push_back(node.a*3+run);
        else if(node.kind==kSplit){for(size_t i=node.a+node.b;i-->node.a;)stack.push_back(targets_[i]*3+run);}
        else out.push_back(state);
      }
    };
    closure(0,current);
    for(const char c:url){
      ++stamp;next.clear();
      for(const size_t state:current){
        const auto&node=nodes_[state/3];const size_t run=state%3;
        if(run&&(run==2||c!='/')&&seen[state]!=stamp){seen[state]=stamp;next.push_back(state);}
        if(node.kind==kLiteral&&node.literal==c)closure(node.a*3,next);
      }
      current.swap(next);
      if(current.empty())return false;
    }
    return std::any_of(current.begin(),current.end(),[&](size_t state){return nodes_[state/3].kind==kEnd;});
  }
 private:
  enum Kind:uint8_t{kLiteral,kStar,kSplit,kJump,kEnd};
  // Literal/Star/Jump: a is the next node. Split: targets_[a,a+b) start each alternative.
  struct Node{Kind kind=kEnd;char literal=0;size_t a=0,b=0;};
  void Compile(){
    const size_t n=pattern_.size(),none=std::string::npos;
    literal_=pattern_.find_first_of("*\\{")==none;
    if(literal_)return;
    // Same tokenisation as the reference: \x pairs are opaque, a { opens a group
    // that expands only when it closes and owns a top-level comma.
    std::vector<size_t> close(n,none),owner(n,none),slot(n,0),open;
    for(size_t i=0;i<n;++i){
      const char c=pattern_[i];
      if(c=='\\'){if(i+1<n)++i;continue;}
      if(c=='{')open.push_back(i);
      else if(c=='}'&&!open.empty()){close[open.back()]=i;open.pop_back();}
      else if(c==','&&!open.empty()){owner[i]=open.back();++slot[open.back()];}
    }
    std::vector<uint8_t> expands(n,0),ends(n,0);
    for(size_t i=0;i<n;++i)if(close[i]!=none&&slot[i]){expands[i]=1;ends[close[i]]=1;}
    nodes_.assign(n+1,Node{});
    for(size_t i=0;i<n;++i){
      const char c=pattern_[i];auto&node=nodes_[i];
      if(c=='\\'&&i+1<n){node=Node{kLiteral,pattern_[i+1],i+2,0};++i;}
      else if(c=='*')node=Node{kStar,0,i+1,0};
      else if(c=='{'&&expands[i]){node=Node{kSplit,0,targets_.size(),slot[i]+1};targets_.push_back(i+1);targets_.resize(targets_.size()+slot[i]);slot[i]=node.a+1;}
      else if(c==','&&owner[i]!=none&&expands[owner[i]]){node=Node{kJump,0,close[owner[i]]+1,0};targets_[slot[owner[i]]++]=i+1;}
      else if(c=='}'&&ends[i])node=Node{kJump,0,i+1,0};
      else node=Node{kLiteral,c,i+1,0};
    }
  }
  std::string pattern_;std::vector<Node> nodes_;std::vector<size_t> targets_;bool literal_=true;
};
inline bool NativeURLGlobMatches(const std::string& pattern, const std::string& url) {
  return NativeURLGlob(pattern).Matches(url);
}
}  // namespace suitecut
