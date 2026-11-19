// SPDX-License-Identifier: MIT
// Browser-level CDP event retention: no browser process or network needed.
#include "cdp_network.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using suitecut::Json;
using suitecut::detail::CdpNetwork;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Json event(const char* method,Json params,const char* session=nullptr){Json out{{"method",method}};out["params"]=std::move(params);if(session)out["sessionId"]=session;return out;}
Json begin(const std::string& guid,const char* frame){return event("Browser.downloadWillBegin",Json{{"guid",guid},{"frameId",frame},{"url","https://fixture.test/file"},{"suggestedFilename","file"}});}
Json progress(const std::string& guid,const char* state,std::int64_t bytes){return event("Browser.downloadProgress",Json{{"guid",guid},{"state",state},{"receivedBytes",bytes},{"totalBytes",100000}});}
std::uint64_t sequence(const Json& event){return static_cast<std::uint64_t>(event.at("_suitecutEventSequence").as_int());}
}
int main(){
  try{
    CdpNetwork network([](suitecut::detail::CdpAutomatic){});
    network.observe(begin("a","frame-1"));network.observe(begin("b","frame-2"));
    // A long download's progress stream is coalesced to its newest state instead of accumulating.
    for(int i=1;i<=100000;++i)network.observe(progress("a","inProgress",i));
    auto all=network.events("Browser.download");
    check(all.as_array().size()==3&&all.at(2).at("params").at("receivedBytes")==Json(100000),"Superseded progress events were retained");
    // A consumer cursor reads only what is newer than the events it already applied.
    const auto cursor=sequence(all.at(2));
    check(network.events("Browser.download",cursor).as_array().empty(),"Cursor returned already-applied events");
    network.observe(progress("a","completed",100000));
    const auto fresh=network.events("Browser.download",cursor);
    check(fresh.as_array().size()==1&&fresh.at(0).at("params").at("state")==Json("completed"),"Cursor missed the newest event");
    check(network.events("Browser.downloadWillBegin").as_array().size()==2,"Method prefix no longer selects one method");
    // The owner consumed the final state: everything about that download is released; others stay.
    network.release_downloads({"a"});
    all=network.events("Browser.download");
    check(all.as_array().size()==1&&all.at(0).at("params").at("guid")==Json("b"),"Finished download events were not released");
    // A closed page releases every download its frame began, consumed or not.
    network.observe(progress("b","inProgress",5));network.observe(begin("c","frame-2"));network.observe(begin("d","frame-3"));
    network.release_downloads({},"frame-2");
    all=network.events("Browser.download");
    check(all.as_array().size()==1&&all.at(0).at("params").at("guid")==Json("d"),"Closed page's downloads were not released");
    // Drag interceptions are released when their drag finishes and when their session goes away.
    network.observe(event("Input.dragIntercepted",Json{{"data",Json::object{}}},"root-1"));
    network.observe(event("Input.dragIntercepted",Json{{"data",Json::object{}}},"root-2"));
    const auto through=network.sequence();
    network.observe(event("Input.dragIntercepted",Json{{"data",Json::object{}}},"root-1"));
    network.release_drags("root-1",through);
    auto drags=network.events("Input.dragIntercepted");
    check(drags.as_array().size()==2&&sequence(drags.at(1))>through&&drags.at(0).at("sessionId")==Json("root-2"),"Finished drag interceptions were not released");
    network.forget("root-1");network.forget("root-2");
    check(network.events("Input.dragIntercepted").as_array().empty(),"Forgotten sessions kept drag interceptions");
    check(network.events("Browser.download").as_array().size()==1,"Forgetting a session released downloads");
    std::cout<<"CDP browser events: coalesced progress, cursors and natural-end release passed\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
