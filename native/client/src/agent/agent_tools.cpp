// SPDX-License-Identifier: MIT
#include "suitecut/agent_tools.hpp"
#include "suitecut/agent_driver.hpp"
#include "suitecut/simple_agent.hpp"
#include <map>
#include <set>
#include <stdexcept>

namespace suitecut {
namespace {
const std::map<std::string,std::string> groups{
  {"navigate","core"},{"snapshot","core"},{"click","core"},{"double_click","core"},
  {"hover","core"},{"focus","core"},{"clear","core"},{"check","core"},{"uncheck","core"},
  {"scroll_into_view","core"},{"fill","core"},{"type","core"},{"select","core"},
  {"press","core"},{"insert_text","core"},{"keyboard","core"},{"mouse","core"},
  {"drag","core"},{"read","core"},{"upload","core"},{"wait_for","core"},
  {"history","core"},{"frames","core"},{"frame_snapshot","core"},{"viewport","core"},
  {"get","core"},{"batch","core"},{"pages","core"},{"diff","core"},{"dialog","core"},
  {"screenshot","core"},{"network","network"},{"download","network"},{"webmcp","network"},
  {"state","state"},{"storage","state"},{"credentials","state"},
  {"evaluate","debug"},{"devtools","debug"},{"console","debug"},{"initScript","debug"},
  {"submit","debug"},{"result","debug"},{"cancel","debug"},{"events","debug"},
  {"emulation","debug"},{"tracing","debug"},{"profiler","debug"},
  {"recording","media"},{"pdf","media"},{"image_compare","media"}
};
}
Json agent_tool_profiles() {
  return Json::array{
    Json{{"name","core"},{"description","Compact UI observations, actions, waits, navigation, frames and tabs"}},
    Json{{"name","network"},{"description","Network routing, downloads and page-published WebMCP tools"}},
    Json{{"name","state"},{"description","Cookies, origin storage, saved state and credential vault"}},
    Json{{"name","debug"},{"description","Evaluation, DevTools, console, emulation, traces and profiling"}},
    Json{{"name","media"},{"description","Screenshots, PDF, recording and image comparison"}},
    Json{{"name","all"},{"description","Complete native driver plus the compact agent interface"}}
  };
}
Json agent_tool_catalog(const std::string& profiles) {
  std::set<std::string> selected{"core"};
  if(profiles.empty()||profiles.size()>256)throw std::invalid_argument("Invalid tool profile list");
  for(std::size_t begin=0;begin<profiles.size();) {
    const auto end=profiles.find(',',begin);const auto name=profiles.substr(begin,end==std::string::npos?end:end-begin);
    if(name!="core"&&name!="network"&&name!="state"&&name!="debug"&&name!="media"&&name!="all")
      throw std::invalid_argument("Unknown tool profile: "+name);
    selected.insert(name);
    if(end==std::string::npos)break;
    begin=end+1;if(begin==profiles.size())throw std::invalid_argument("Empty tool profile");
  }
  static const auto simple_tools=SimpleAgent::tools();
  auto result=simple_tools;
  result.as_array().push_back(Json{{"name","profiles"},{"description","List native tool capability groups; this does not launch a browser"},
    {"inputSchema",Json{{"type","object"},{"properties",Json::object{}},{"additionalProperties",false}}}});
  // Keep the common catalog small. Explicit primitive tools remain available
  // through all; core uses act rather than advertising every input spelling.
  const std::set<std::string> core{"navigate","read","pages","frames","viewport","get","upload","history"};
  static const auto driver_tools=AgentDriver::tools();
  for(const auto& tool:driver_tools.as_array()) {
    const auto& name=tool.at("name").as_string();const auto found=groups.find(name);
    if(selected.contains("all")||(found!=groups.end()&&
       ((found->second=="core"&&core.contains(name))||(found->second!="core"&&selected.contains(found->second)))))
      result.as_array().push_back(tool);
  }
  return result;
}
} // namespace suitecut
