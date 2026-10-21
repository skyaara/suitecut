// SPDX-License-Identifier: MIT
#pragma once
#include "connection.hpp"
#include <map>
#include <memory>
#include <string>
namespace suitecut::detail {
void bidi_check_active(Deadline);
struct BidiCleanupScope {
  BidiCleanupScope();
  ~BidiCleanupScope();
  BidiCleanupScope(const BidiCleanupScope&)=delete;
};
struct BidiPage {
  int id=1,context=0;
  std::string browsing_context,sandbox;
  double mouse_x=0,mouse_y=0;
  int modifiers=0,mouse_buttons=0;
};
struct BidiRuntime {
  LaunchOptions options;
  std::unique_ptr<CdpConnection> wire;
  std::map<int,BidiPage> pages;
  std::map<int,std::string> contexts;
  int next_page=1,next_context=0;
  Json call(const std::string&,Json,Deadline);
  // Native script.evaluate normalized to the existing SuiteCut evaluate
  // result envelope; protocol calls themselves always use WebDriver BiDi.
  Json evaluate(BidiPage&,const std::string&,Deadline,bool isolated=false);
  Json value(BidiPage&,const std::string&,Deadline,bool isolated=false);
};
Json bidi_dom_command(BidiRuntime&,BidiPage&,const Json&,Deadline);
Json bidi_input_command(BidiRuntime&,BidiPage&,const Json&,Deadline);
}
