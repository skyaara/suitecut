// SPDX-License-Identifier: MIT
#include "suitecut/transport.hpp"
#include "suitecut/json.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
using suitecut::Json;
using namespace std::chrono_literals;
void check(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
int main(int argc,char**argv){
 try {
  suitecut::LaunchOptions options;
  options.executable=(argc>1?std::filesystem::absolute(argv[1]):std::filesystem::absolute(argv[0]).parent_path()/"transport_fake_engine").string();
  options.max_pending=1;options.startup_timeout=5s;options.shutdown_timeout=250ms;
  {
   auto transport=suitecut::Transport::launch(options);
   // No poll loop runs while the engine holds this reply behind exact-body
   // receipts. Internal ACKs must bypass the occupied public request slot.
   auto reply=transport->call_json(Json{{"command","network-body"}},2s);
   check(Json::parse(reply.header).at("ok").as_bool(),"Publication did not progress");
   std::map<std::string,std::vector<std::uint8_t>> copies;
   for(int i=0;i<6;++i){
    auto packet=transport->poll(1s);check(packet.has_value(),"Exact body chunk lost");auto h=Json::parse(packet->header);
    auto key=h.at("type")==Json("networkbody")?"page:"+std::to_string(h.at("pageId").as_int()):"worker:"+h.at("owner").as_string();
    auto& bytes=copies[key];check(h.at("offset").as_int()==static_cast<std::int64_t>(bytes.size()),"Body chunk reordered");bytes.insert(bytes.end(),packet->payload.begin(),packet->payload.end());
   }
   check(copies.size()==3,"Page and worker body IDs collided");
   for(auto&[key,bytes]:copies){(void)key;check(bytes==std::vector<std::uint8_t>({0,255,'x','\r','\n'}),"Binary POST body changed");}
   check(!transport->poll(0ms),"Internal ACK replies leaked as events");transport->close();
  }
  {
   auto transport=suitecut::Transport::launch(options);
   auto reply=transport->call_json(Json{{"command","body-retire"}},2s);
   check(Json::parse(reply.header).at("ok").as_bool(),"Closed pages or contexts leaked partial-body reservations");
   transport->close();
  }
  for(const char* mode:{"body-gap","body-oversize","body-identity","body-duplicate","body-size-change"}){
   options.arguments={std::string("--transport-test-mode=")+mode};auto transport=suitecut::Transport::launch(options);bool rejected=false;
   try{transport->call_json(Json{{"command","network-body"}},2s);}catch(const std::exception& error){rejected=std::string(error.what()).find("timed out")==std::string::npos;}
   check(rejected,"Invalid body did not fail closed before the request deadline");transport->close();
  }
  std::cout<<"Bounded automatic page/worker body receipts, binary ownership and malformed packets passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
