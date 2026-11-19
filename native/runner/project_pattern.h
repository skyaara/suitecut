// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <bitset>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace suitecut::runner {
// '*' glob, anchored to the complete project name. ASCII case folding
// is explicit; other UTF8 bytes match literally. No regex parser/backtracking.
class ProjectPattern {
 public:
  explicit ProjectPattern(std::string_view pattern){
    if(pattern.empty()||pattern.find('\0')!=std::string_view::npos)throw std::invalid_argument("Native project patterns must be nonempty and contain no NUL");
    bool previous_star=false;
    for(const auto c:pattern){if(c=='*'&&previous_star)continue;previous_star=c=='*';
      if(previous_star)wildcard_=true;
      tokens_.push_back({previous_star,previous_star?static_cast<unsigned char>(0):Fold(static_cast<unsigned char>(c))});
    }
  }
  bool Matches(std::string_view name)const {
    // Consecutive stars are collapsed, so one ascending pass is the epsilon closure.
    const size_t length=tokens_.size();std::vector<char> states(length+1,0),next(length+1,0);
    const auto close=[&](std::vector<char>& value){for(size_t i=0;i<length;++i)if(value[i]&&tokens_[i].star)value[i+1]=1;};
    states[0]=1;close(states);
    for(const auto raw:name){const auto c=Fold(static_cast<unsigned char>(raw));std::fill(next.begin(),next.end(),static_cast<char>(0));
      for(size_t i=0;i<length;++i)if(states[i]){if(tokens_[i].star)next[i]=1;else if(tokens_[i].literal==c)next[i+1]=1;}
      close(next);states.swap(next);
    }
    return states[length]!=0;
  }
  bool Wildcard()const{return wildcard_;}
 private:
  static unsigned char Fold(unsigned char c){return c>='A'&&c<='Z'?static_cast<unsigned char>(c+('a'-'A')):c;}
  struct Token {bool star;unsigned char literal;};
  std::vector<Token> tokens_;bool wildcard_=false;
};
}
