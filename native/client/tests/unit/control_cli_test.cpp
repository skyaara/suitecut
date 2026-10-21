// SPDX-License-Identifier: MIT
#include "control_cli.hpp"
#include "suitecut/agent_tools.hpp"
#include "suitecut/agent_driver.hpp"
#include <iostream>
#include <stdexcept>
using suitecut::Json;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F work){try{work();}catch(const std::exception&){return;}throw std::runtime_error("Expected invalid argument rejection");}
bool contains(const Json& catalog,const std::string& name){for(const auto& tool:catalog.as_array())if(tool.at("name")==Json(name))return true;return false;}
}
int main() {
  try {
    const auto core=suitecut::agent_tool_catalog();
    check(contains(core,"observe")&&contains(core,"act")&&contains(core,"profiles")&&!contains(core,"evaluate")&&!contains(core,"network"),"Core is not a small explicit capability catalog");
    check(contains(suitecut::agent_tool_catalog("network,state"),"network")&&contains(suitecut::agent_tool_catalog("network,state"),"storage"),"Combined capability groups lost tools");
    const auto all=suitecut::agent_tool_catalog("all");const auto driver=suitecut::AgentDriver::tools();
    for(const auto& tool:driver.as_array())check(contains(all,tool.at("name").as_string()),"Full driver capability lost");
    rejects([]{suitecut::agent_tool_catalog("core,typo");});rejects([]{suitecut::agent_tool_catalog("core,");});
    const Json compact{{"format","compact"}};
    auto request=suitecut::control::cli_request({"fill","@e2","literal $() `text`\nUTF8 ✓"},compact,"none");
    check(request.at("arguments").at("value")==Json("literal $() `text`\nUTF8 ✓")&&request.at("arguments").at("ref")==Json("e2")&&request.at("arguments").at("response")==Json("none"),"CLI changes text or response semantics");
    request=suitecut::control::cli_request({"dblclick","10.5","20"},compact);
    check(request.at("arguments").at("event")==Json("doubleclick")&&request.at("arguments").at("x").as_double()==10.5,"Coordinate double-click lost");
    request=suitecut::control::cli_request({"drag","10","20","30","40"},compact);
    check(request.at("arguments").at("steps")==Json(20)&&request.at("arguments").at("to").at("x")==Json(30.0),"Coordinate drag lacks interpolation default");
    request=suitecut::control::cli_request({"wait","e3","enabled"},compact);
    check(request.at("tool")==Json("wait")&&request.at("arguments").at("state")==Json("enabled"),"Wait condition parsed incorrectly");
    request=suitecut::control::cli_request({"find","Checkout"},compact);
    check(request.at("arguments").at("query")==Json("Checkout"),"Snapshot search lost");
    request=suitecut::control::cli_request({"tabs","switch","2"},compact);
    check(request.at("tool")==Json("pages")&&request.at("arguments").at("pageId")==Json(2),"Tab command parsed incorrectly");
    rejects([&]{suitecut::control::cli_request({"click","e2","junk"},compact);});
    rejects([&]{suitecut::control::cli_request({"drag","1","2","3"},compact);});
    rejects([&]{suitecut::control::cli_request({"click","../bad"},compact);});
    rejects([&]{suitecut::control::cli_request({"batch","{}"},compact);});
    rejects([&]{suitecut::control::cli_request({"call","evaluate","[]"},compact);});
    std::cout<<"PASS native CLI parsing, literal input, capability discovery and full-surface preservation\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
