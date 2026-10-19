// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <bitset>
#include <stdexcept>
#include <string_view>
namespace suitecut::runner {
// Bounded '*' glob, anchored to the complete project name. ASCII case folding
// is explicit; other UTF8 bytes match literally. No regex parser/backtracking.
class ProjectPattern {
 public:
  explicit ProjectPattern(std::string_view pattern){
    if(pattern.empty()||pattern.size()>256||pattern.find('\0')!=std::string_view::npos)throw std::invalid_argument("Native project patterns require1..256 bytes");
    bool previous_star=false;
    for(const auto c:pattern){if(c=='*'&&previous_star)continue;previous_star=c=='*';
      if(previous_star){stars_.set(length_);wildcard_=true;}
      else literals_[Fold(static_cast<unsigned char>(c))].set(length_);
      ++length_;
    }
  }
  bool Matches(std::string_view name)const {
    if(name.size()>256)return false;
    std::bitset<257> states;states.set(0);states|=(states&stars_)<<1;
    for(const auto c:name){states=(states&stars_)|((states&literals_[Fold(static_cast<unsigned char>(c))])<<1);states|=(states&stars_)<<1;}
    return states.test(length_);
  }
  bool Wildcard()const{return wildcard_;}
 private:
  static unsigned char Fold(unsigned char c){return c>='A'&&c<='Z'?static_cast<unsigned char>(c+('a'-'A')):c;}
  std::array<std::bitset<257>,256> literals_{};std::bitset<257> stars_{};size_t length_=0;bool wildcard_=false;
};
}
