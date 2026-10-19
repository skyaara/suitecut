// SPDX-License-Identifier: MIT
#undef NDEBUG
#include <cassert>
#include "query_realms.h"
int main(){
  using R=SuiteCutQueryRealms;
  assert((R::Engines("main=#x >> iso=#y")==std::vector<std::string>{"main","iso"}));
  assert((R::Engines("[data-engine=\"main=#x >> iso=#y\"]").empty()));
  assert((R::Engines("text=\"main=#x >> iso=#y\" >> *iso=#y")==std::vector<std::string>{"text","iso"}));
  assert((R::Engines("text=/main=x>>iso=y/ >> main=#x")==std::vector<std::string>{"text","main"}));
  assert((R::Engines("css=:is([x='iso=#x'],button) >> iso=#y")==std::vector<std::string>{"css","iso"}));
  assert((R::Engines(R"(div >> internal:has="iso=#inside" >> main=#outside)")==std::vector<std::string>{"iso","main"}));
  assert((R::Engines(R"(text = lo" >> iso=#inside)")==std::vector<std::string>{"text","iso"}));
  assert((R::Engines(R"(text="iso=#inside" >> main=#outside)")==std::vector<std::string>{"text","main"}));
  assert((R::Engines(R"(div >> internal:or="\u0069so=#inside")")==std::vector<std::string>{"iso"}));
  assert((R::Engines(R"(div >> internal:has="text=iso=#inside")")==std::vector<std::string>{"text"}));
  assert((R::Engines(R"(div >> internal:has=123)").empty()));
  assert((R::Engines("text="+std::string(65000,' ')+"hello >> iso=#inside")==std::vector<std::string>{"text","iso"}));
  assert((R::Engines(R"(div >> internal:has={"engine":"iso=#inside"})").empty()));
  assert((R::Engines(R"(text=/[/]iso=x/ >> main=#outside)")==std::vector<std::string>{"text","main"}));
  assert((R::Engines(R"(div >> internal:has="text=\ud800 main=fake >> iso=#actual")")==std::vector<std::string>{"text","iso"}));
  assert((R::Engines(R"(div >> internal:has="text=\\u0069so=fake")")==std::vector<std::string>{"text"}));
  assert((R::Engines(R"(text=` >> iso=#fake)")==std::vector<std::string>{"text"}));
  assert((R::Engines(R"(text=hello`world >> iso=#actual)")==std::vector<std::string>{"text","iso"}));
}
