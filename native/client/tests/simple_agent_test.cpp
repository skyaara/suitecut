// SPDX-License-Identifier: MIT
// Controlled real-transport seam, not a browser correctness claim.
#include "suitecut/simple_agent.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
using suitecut::Json;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* why) {if(!value)throw std::runtime_error(why);}
void packet(Json head,const Json& value={}) {
  const auto data=head.at("type")==Json("ready")?std::string{}:value.dump();
  head["version"]=1;head["bytes"]=data.size();const auto text=head.dump();
  const auto n=static_cast<std::uint32_t>(text.size());
  const char prefix[]{static_cast<char>(n),static_cast<char>(n>>8),static_cast<char>(n>>16),static_cast<char>(n>>24)};
  std::cout.write(prefix,4).write(text.data(),static_cast<std::streamsize>(text.size()))
    .write(data.data(),static_cast<std::streamsize>(data.size())).flush();
}
int engine() {
  packet(Json{{"type","ready"},{"pageId",1},{"timestampMs",1},{"cefVersion","simple-test"}});
  int mutations=0,snapshots=0;bool many=false;Json last;
  std::string line;
  while(std::getline(std::cin,line)) {
    const auto q=Json::parse(line);const auto command=q.at("command").as_string();
    Json result;bool ok=true;std::string error;
    if(command=="snapshotReferences") {
      ++snapshots;
      Json::array nodes;
      for(int i=0;i<(many?4200:2);++i)nodes.emplace_back(Json{{"role",i?"button":"generic"},
        {"name",i?"Run":"Password"},{"ref","native_doc_"+std::to_string(i)},
        {"inputType",i?"button":"password"},{"readOnly",false},{"editable",!i}});
      if(!nodes.empty()) {
        nodes[0]["fixtureBoxes"]=q.at("accessibilityBoxes");
        nodes[0]["fixtureDepth"]=q.contains("referenceDepth")?q.at("referenceDepth"):Json(-1);
        nodes[0]["fixtureFrame"]=q.contains("frame")?q.at("frame"):Json("main");
      }
      result=Json{{"snapshotJSON",Json(nodes).dump()}};
    } else if(command=="evaluate") {
      const auto expression=q.at("expression").as_string();
      Json value;
      if(expression=="location.href")value="https://fixture.test/";
      else if(expression=="document.title")value="Fixture";
      else if(expression=="report")value=Json{{"mutations",mutations},{"snapshots",snapshots},{"last",last}};
      else if(expression=="many")many=true;
      result=Json{{"result",Json{{"value",value}}}};
    } else if(command=="queryEvaluate") {
      result=Json{{"result",Json{{"value",q.contains("elementState")?Json(q.at("state")!=Json("hidden")):Json("Visible text")}}}};
    } else if(command=="frames")result=Json::array{Json{{"id","frame_1"}}};
    else if(command=="dialog")result=Json::array{};
    else if(command=="artifact")result=Json{{"data","iVBORw0KGgo="}};
    else if(command!="cancel") {
      ++mutations;last=q;
      if(q.contains("key")&&q.at("key")==Json("FAIL")) {ok=false;error="Fixture input failed after dispatch";}
      if(q.contains("key")&&q.at("key")==Json("SLOW"))std::this_thread::sleep_for(150ms);
    }
    packet(Json{{"type","response"},{"id",q.at("id")},{"ok",ok},{"error",error}},result);
  }
  return 0;
}
std::string first(const Json& observation) {return observation.at("snapshot").at(0).at("ref").as_string();}
} // namespace
int main(int argc,char** argv) {
  for(int i=1;i<argc;++i)if(std::string(argv[i])=="--simple-test-engine")return engine();
  try {
    char path[4096];const auto size=::readlink("/proc/self/exe",path,sizeof(path));
    check(size>0&&static_cast<std::size_t>(size)<sizeof(path),"Resolve test path");
    suitecut::LaunchOptions options;options.executable=std::string(path,static_cast<std::size_t>(size));
    options.arguments={"--simple-test-engine"};
    auto browser=suitecut::Browser::launch(options);
    suitecut::AgentDriver driver(browser);suitecut::SimpleAgent simple(driver);
    const auto catalog=suitecut::SimpleAgent::tools();
    check(catalog.as_array().size()==3&&catalog.at(0).at("name")==Json("observe")&&
      catalog.at(1).at("name")==Json("act")&&catalog.at(2).at("name")==Json("wait"),"Exactly three tools");
    auto observed=simple.call("observe");const auto old=first(observed);
    check(old=="e1"&&observed.at("controls").at(0).at("inputType")==Json("password"),"Public control metadata and short refs");
    const auto acted=simple.call("act",Json{{"action","fill"},{"ref",old},{"value","secret"}});
    check(acted.at("ok").as_bool()&&acted.at("delivered").as_bool()&&acted.at("effect")==Json("unverified")&&
      acted.at("completion")==Json("unverified"),"Delivered input is not verified task completion");
    const auto current=first(acted.at("observation"));check(current!=old,"Aliases never rebind");
    const auto before=driver.call("evaluate",Json{{"expression","report"}}).at("mutations");
    const auto stale=simple.call("act",Json{{"action","click"},{"ref",old}});
    check(!stale.at("ok").as_bool()&&stale.at("execution")==Json("notSent"),"Stale alias rejected before dispatch");
    check(driver.call("evaluate",Json{{"expression","report"}}).at("mutations")==before,"Stale alias never mutates");
    auto failure=simple.call("act",Json{{"action","press"},{"key","FAIL"}});
    check(failure.at("execution")==Json("outcomeUnknown")&&failure.at("delivered").is_null()&&
      failure.at("error").at("cause")==Json("Fixture input failed after dispatch")&&failure.contains("observation"),
      "Original failure preserved with uncertainty and fresh observation");
    const auto confirmed=simple.call("act",Json{{"action","press"},{"key","Tab"},
      {"expect",Json{{"url","https://fixture.test/"}}}});
    check(confirmed.at("effect")==Json("confirmed")&&confirmed.at("completion")==Json("unverified"),"UI predicate distinct from task grade");
    const auto changed=simple.call("wait",Json{{"changed",true},{"timeoutMs",100}});
    check(!changed.at("ok").as_bool(),"Regenerated aliases do not count as semantic change");
    simple.call("observe",Json{{"boxes",true},{"depth",12},{"frame","frame_1"}});
    check(!simple.call("wait",Json{{"changed",true},{"timeoutMs",100}}).at("ok").as_bool(),
      "Changed compares identical box/depth/frame observation settings");
    const auto started=std::chrono::steady_clock::now();
    const auto slow=simple.call("act",Json{{"action","press"},{"key","SLOW"},{"timeoutMs",60}});
    check(!slow.at("ok").as_bool()&&slow.at("execution")==Json("outcomeUnknown")&&
      std::chrono::steady_clock::now()-started<130ms,"One action/observation deadline bounds timeout");
    // Let the controlled late response retire without racing subsequent tests.
    std::this_thread::sleep_for(180ms);
    observed=simple.call("observe");
    const auto baseline=driver.call("evaluate",Json{{"expression","report"}});
    auto sequence=simple.call("act",Json{{"actions",Json::array{
      Json{{"action","mouse"},{"event","move"},{"x",10},{"y",20}},
      Json{{"action","keyboard"},{"event","down"},{"key","Shift"}},
      Json{{"action","keyboard"},{"event","up"},{"key","Shift"}}}}});
    const auto after=driver.call("evaluate",Json{{"expression","report"}});
    check(sequence.at("ok").as_bool()&&sequence.at("results").as_array().size()==3&&
      after.at("mutations").as_int()==baseline.at("mutations").as_int()+3&&
      after.at("snapshots").as_int()==baseline.at("snapshots").as_int()+1,"Known sequence gets exactly one final observation");
    const auto preflight=driver.call("evaluate",Json{{"expression","report"}}).at("mutations");
    const auto invalid=simple.call("act",Json{{"actions",Json::array{
      Json{{"action","press"},{"key","Enter"}},Json{{"action","evaluate"},{"value","evil"}}}}});
    check(!invalid.at("ok").as_bool()&&driver.call("evaluate",Json{{"expression","report"}}).at("mutations")==preflight,
      "Sequence prevalidation prevents partial mutation on invalid UI action");
    for(const auto& malformed:Json::array{
      Json{{"action","mouse"},{"event","explode"}},
      Json{{"action","keyboard"},{"event","eval"}},
      Json{{"action","mouse"},{"event","click"},{"x","wrong"},{"y",20}},
      Json{{"action","mouse"},{"event","click"},{"x",10},{"y",20},{"options",Json{{"force","wrong"}}}},
      Json{{"action","mouse"},{"event","click"},{"x",10},{"y",20},{"options",Json{{"modifiers",Json::array{Json("eval")}}}}}}) {
      const auto rejected=simple.call("act",Json{{"actions",Json::array{
        Json{{"action","press"},{"key","Enter"}},malformed}}});
      check(!rejected.at("ok").as_bool()&&driver.call("evaluate",Json{{"expression","report"}}).at("mutations")==preflight,
        "Whole sequence validates mouse, keyboard and pointer options before any mutation");
    }
    const auto stopped=simple.call("act",Json{{"actions",Json::array{
      Json{{"action","press"},{"key","FAIL"}},Json{{"action","press"},{"key","Enter"}}}}});
    check(!stopped.at("ok").as_bool()&&stopped.at("results").at(1).at("status")==Json("skipped"),"Sequence stops without retry");
    const auto framed=simple.call("observe",Json{{"frame","frame_1"}});
    const auto frame_action=simple.call("act",Json{{"action","click"},{"ref",first(framed)}});
    check(frame_action.at("ok").as_bool()&&driver.call("evaluate",Json{{"expression","report"}}).at("last").at("frame")==Json("frame_1"),
      "Observed frame context is retained by aliases");
    check(simple.call("observe",Json{{"mode","screenshot"}}).at("mimeType")==Json("image/png"),"Screenshot passthrough");
    check(!simple.call("evaluate",Json{{"expression","evil"}}).at("ok").as_bool(),"Raw evaluation is not a simple tool");
    auto compact=simple.call("observe",Json{{"format","compact"}});
    check(compact.at("ok").as_bool()&&!compact.contains("snapshot")&&compact.contains("controls"),"Compact output still duplicates the full snapshot");
    const auto compact_ref=compact.at("controls").at(0).at("ref").as_string();
    const auto snapshots_before=driver.call("evaluate",Json{{"expression","report"}}).at("snapshots");
    auto ack=simple.call("act",Json{{"action","fill"},{"ref",compact_ref},{"value","x"},{"response","none"}});
    check(ack.at("ok").as_bool()&&!ack.contains("observation")&&driver.call("evaluate",Json{{"expression","report"}}).at("snapshots")==snapshots_before,
      "Acknowledgement-only action performs an unwanted snapshot");
    check(simple.target_ref(compact_ref).at("kind")==Json("ref"),"Acknowledgement-only action unexpectedly invalidates aliases");
    auto delta=simple.call("observe",Json{{"format","compact"},{"delta",true}});
    check(!delta.at("reset").as_bool()&&delta.contains("refIndex")&&!delta.contains("controls")&&delta.at("changes").at("changed").as_array().empty(),
      "Unchanged compact delta is not a bounded current-ref update");
    check(!simple.call("act",Json{{"action","click"},{"ref",compact_ref},{"response","none"}}).at("ok").as_bool(),"Delta silently reuses old aliases");
    auto double_click=simple.call("act",Json{{"action","mouse"},{"event","doubleclick"},{"x",10},{"y",20},{"response","none"}});
    check(double_click.at("ok").as_bool()&&driver.call("evaluate",Json{{"expression","report"}}).at("last").at("clickCount")==Json(2),"Coordinate double-click is not one native input with clickCount2");
    const auto mutations_before_drag=driver.call("evaluate",Json{{"expression","report"}}).at("mutations").as_int();
    auto drag=simple.call("act",Json{{"action","drag"},{"from",Json{{"x",10},{"y",20}}},{"to",Json{{"x",100},{"y",120}}},{"response","none"}});
    check(drag.at("ok").as_bool()&&drag.at("result").at("steps")==Json(20)&&
      driver.call("evaluate",Json{{"expression","report"}}).at("mutations").as_int()==mutations_before_drag+4,"Coordinate drag lacks its bounded interpolated native gesture");
    const auto scoped=simple.call("observe",Json{{"scope","#form"},{"format","compact"}});
    check(scoped.at("ok").as_bool(),"Scoped snapshot rejected");
    const auto searched=simple.call("observe",Json{{"format","compact"},{"query","RUN"},{"maxTextBytes",24}});
    std::size_t text_bytes=0;
    auto count_text=[&](auto&& self,const Json& node)->void {
      if(node.is_string())text_bytes+=node.as_string().size();
      else if(node.is_array())for(const auto& child:node.as_array())self(self,child);
      else if(node.is_object())for(const auto& [key,child]:node.as_object())if(key!="actions")self(self,child);
    };
    count_text(count_text,searched.at("controls"));
    check(searched.at("ok").as_bool()&&!searched.at("controls").as_array().empty()&&text_bytes<=24,
      "Search did not enforce its output text budget");
    auto short_failure=simple.call("act",Json{{"actions",Json::array{Json{{"action","press"},{"key","FAIL"}}}},{"response","compact"}});
    check(short_failure.at("error").contains("cause")&&!short_failure.at("results").at(0).at("error").contains("cause"),"Batch duplicates the original error cause");
    driver.call("evaluate",Json{{"expression","many"}});
    const auto capped=simple.call("observe");
    check(capped.at("truncated").as_bool()&&capped.at("controls").as_array().size()<=256,"Reference and control output bounds");
    std::cout<<"PASS simple aliases, validation, UI evidence, sequence, frame context and bounded deadline\n";
    return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
