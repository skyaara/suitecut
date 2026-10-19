// SPDX-License-Identifier: MIT
// Consumer for the supervised local TLS-to-real-Chrome ownership workflow.
#include "suitecut/browser.hpp"
#include "suitecut/client.h"
#include "../src/cdp_internal.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#ifndef _WIN32
#include <poll.h>
#include <unistd.h>
#endif

namespace {
using suitecut::Json;
using namespace std::chrono_literals;
using Clock=std::chrono::steady_clock;
void check(bool passed,const std::string& name,Json& report){
  if(!passed)throw std::runtime_error("TLS workflow check failed: "+name);
  report["checks"].as_array().push_back(name);
  std::cout<<Json{{"event","check"},{"name",name}}.dump()<<std::endl;
}
Json raw(suitecut::detail::CdpConnection& wire,const std::string& method,Json params=Json::object{},const std::string& session={}){
  return wire.call(method,std::move(params),session,Clock::now()+3s);
}
Json state(suitecut::detail::CdpConnection& wire,const std::string& target){
  const auto attached=raw(wire,"Target.attachToTarget",Json{{"targetId",target},{"flatten",true}});
  const auto session=attached.at("sessionId").as_string();
  try{
    const auto response=raw(wire,"Runtime.evaluate",Json{{"expression",
        "({token:window.__callerToken,name:document.querySelector('#name').value,receipt:window.receipt,viewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio}})"},
        {"returnByValue",true}},session);
    auto value=response.at("result").at("value");
    raw(wire,"Target.detachFromTarget",Json{{"sessionId",session}});return value;
  }catch(...){try{raw(wire,"Target.detachFromTarget",Json{{"sessionId",session}});}catch(...){}throw;}
}
} // namespace

int main(int argc,char** argv){
  std::string endpoint,ca,output;
  for(int i=1;i<argc;i+=2){
    if(i+1>=argc){std::cerr<<"Expected --endpoint/--ca/--output value\n";return 2;}
    const std::string name=argv[i];
    if(name=="--endpoint")endpoint=argv[i+1];else if(name=="--ca")ca=argv[i+1];
    else if(name=="--output")output=argv[i+1];else{std::cerr<<"Unknown option\n";return 2;}
  }
  if(endpoint.empty()||ca.empty()||output.empty()){std::cerr<<"--endpoint, --ca and --output required\n";return 2;}
  Json report{{"passed",false},{"checks",Json::array{}},{"scope","Verified authenticated HTTPS discovery/WSS controls real caller-owned Chrome"}};
  int code=0;
  try{
    suitecut::LaunchOptions options;options.tls_ca_file=ca;
    options.connect_headers={{"Authorization","Bearer suitecut-local-chrome-proof"},{"X-Native-Proof","real-chrome"}};
    options.startup_timeout=5s;options.shutdown_timeout=2s;
    auto browser=suitecut::Browser::connect_chromium(endpoint,options);
    auto page=browser.main_page();page.set_timeout(3s);
    page.wait_for_function("window.__callerToken==='caller-owned-TLS-browser'");
    check(page.evaluate("window.__callerToken")==Json("caller-owned-TLS-browser"),"verified TLS reaches the pre-existing caller page",report);
    check(page.evaluate("JSON.stringify(window.__callerViewport)===JSON.stringify({width:innerWidth,height:innerHeight,dpr:devicePixelRatio})")==Json(true),
          "TLS attachment preserves caller viewport",report);
    page.get_by_label("Name",true).fill("Native TLS λ");
    page.get_by_role("button","Save",true).click();
    check(page.evaluate("window.receipt")==Json{{"name","Native TLS λ"},{"clickTrusted",true},{"fillTrusted",true}},
          "TLS native locator fill and click dispatch trusted Chrome input",report);
    const auto expected=page.evaluate("({token:window.__callerToken,name:document.querySelector('#name').value,receipt:window.receipt,viewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio}})");
    suitecut::LaunchOptions wire_options=options;wire_options.cdp_endpoint=endpoint;
    auto verifier=suitecut::detail::CdpConnection::launch(wire_options);
    const auto before_contexts=raw(*verifier,"Target.getBrowserContexts");
    const auto before_targets=raw(*verifier,"Target.getTargets");
    std::string caller_target;
    for(const auto& target:before_targets.at("targetInfos").as_array())
      if(target.at("type")==Json("page")&&target.at("title")==Json("Caller TLS proof"))caller_target=target.at("targetId").as_string();
    check(!caller_target.empty(),"independent TLS connection identifies caller ownership",report);
    auto context=browser.new_context();auto temporary=context.new_page();
    temporary.set_content("<!doctype html><title>Temporary TLS SDK page</title>Owned by SDK context");
    browser.close();
    const auto after_contexts=raw(*verifier,"Target.getBrowserContexts");
    check(after_contexts==before_contexts,"SDK TLS close retires its own context",report);
    const auto after_targets=raw(*verifier,"Target.getTargets");
    int pages=0;
    for(const auto& target:after_targets.at("targetInfos").as_array())if(target.at("type")==Json("page"))++pages;
    check(pages==1,"SDK TLS close preserves only the caller page",report);
    check(state(*verifier,caller_target)==expected,"caller page state survives SDK TLS disconnect",report);
    sc_connection_options connection;sc_connection_options_init(&connection);
    connection.headers_json="{\"Authorization\":\"Bearer suitecut-local-chrome-proof\",\"X-Native-Proof\":\"real-chrome\"}";
    connection.tls_ca_file=ca.c_str();
    sc_client* c_handle=nullptr;sc_error error{};
    const auto connected=sc_client_connect_remote(SC_REMOTE_CHROMIUM,endpoint.c_str(),nullptr,&connection,&c_handle,&error);
    std::unique_ptr<sc_client,decltype(&sc_client_destroy)> c_client(c_handle,&sc_client_destroy);
    check(connected==SC_OK,"supplemental C ABI attaches with verified TLS and auth headers",report);
    sc_packet* ready_handle=nullptr;
    const auto ready_status=sc_client_ready(c_client.get(),&ready_handle,&error);
    std::unique_ptr<sc_packet,decltype(&sc_packet_destroy)> ready(ready_handle,&sc_packet_destroy);
    std::size_t size=0;const char* header=sc_packet_header(ready.get(),&size);
    check(ready_status==SC_OK&&header&&Json::parse(std::string_view(header,size)).at("backend")==Json("chromium"),
          "supplemental C ABI reports secure Chromium readiness",report);
    constexpr char request[]="{\"command\":\"evaluate\",\"expression\":\"window.__callerToken\"}";
    sc_packet* result_handle=nullptr;
    const auto evaluated=sc_client_call(c_client.get(),request,sizeof(request)-1,3000,&result_handle,&error);
    std::unique_ptr<sc_packet,decltype(&sc_packet_destroy)> result(result_handle,&sc_packet_destroy);
    const auto* payload=sc_packet_payload(result.get(),&size);
    check(evaluated==SC_OK&&payload&&Json::parse(std::string_view(reinterpret_cast<const char*>(payload),size)).at("result").at("value")==Json("caller-owned-TLS-browser"),
          "supplemental C ABI evaluates the caller page over WSS",report);
    check(sc_client_close(c_client.get(),&error)==SC_OK&&state(*verifier,caller_target)==expected,
          "supplemental C ABI disconnect preserves caller state",report);
    c_client.reset();
    std::cout<<Json{{"event","sdkDetached"},{"passed",true}}.dump()<<std::endl;
#ifdef _WIN32
    throw std::runtime_error("Supervised TLS ownership workflow currently requires POSIX");
#else
    pollfd input{STDIN_FILENO,POLLIN,0};
    if(::poll(&input,1,10000)<=0)throw std::runtime_error("Caller did not authorize its own browser shutdown");
    std::string instruction;std::getline(std::cin,instruction);
    if(instruction!="quit")throw std::runtime_error("Unexpected caller shutdown instruction");
    // This explicit caller command is separate from the SDK's detach operation.
    try{raw(*verifier,"Browser.close");}catch(const std::exception& error){report["callerShutdownResponseError"]=error.what();}
    verifier->close();report["passed"]=true;
#endif
  }catch(const std::exception& error){report["error"]=error.what();std::cerr<<error.what()<<'\n';code=1;}
  std::ofstream file(output);file<<report.dump()<<'\n';file.close();
  if(!file){std::cerr<<"Cannot finish TLS workflow report\n";return 1;}
  std::cout<<report.dump()<<std::endl;return code;
}
