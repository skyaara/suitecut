// SPDX-License-Identifier: MIT
// Standalone ownership regression. Run only under the native proof supervisor.
#include "suitecut/browser.hpp"
#include "suitecut/client.h"
#include "../src/cdp_internal.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {
using suitecut::Json;
using namespace std::chrono_literals;
using Clock=std::chrono::steady_clock;
std::string data_url(const std::string& html) {
  constexpr char hex[]="0123456789ABCDEF";
  std::string result="data:text/html;charset=utf-8,";
  for(unsigned char c:html){result.push_back('%');result.push_back(hex[c>>4]);result.push_back(hex[c&15]);}
  return result;
}
void check(bool condition,const std::string& name,Json& report){
  if(!condition)throw std::runtime_error("Attach ownership check failed: "+name);
  report["checks"].as_array().push_back(name);
  std::cout<<Json{{"event","check"},{"name",name}}.dump()<<std::endl;
}
#ifndef _WIN32
struct CallerChrome {
  pid_t pid=-1;
  int exit_status=-1;
  std::filesystem::path profile;
  std::string port,websocket;
  explicit CallerChrome(const std::string& executable,const std::filesystem::path& log){
    auto pattern=(std::filesystem::temp_directory_path()/"suitecut-attach-caller-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    const char* made=mkdtemp(name.data());
    if(!made)throw std::runtime_error("Cannot create caller-owned Chrome profile");
    profile=made;
    posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;
    bool actions_ready=false,attributes_ready=false;
    try{
      if(posix_spawn_file_actions_init(&actions))throw std::runtime_error("Cannot initialize caller Chrome actions");
      actions_ready=true;
      if(posix_spawnattr_init(&attributes))throw std::runtime_error("Cannot initialize caller Chrome attributes");
      attributes_ready=true;
      if(posix_spawn_file_actions_addopen(&actions,0,"/dev/null",O_RDONLY,0)||
          posix_spawn_file_actions_addopen(&actions,1,log.c_str(),O_WRONLY|O_CREAT|O_APPEND,0600)||
          posix_spawn_file_actions_adddup2(&actions,1,2))throw std::runtime_error("Cannot route caller Chrome streams");
      sigset_t empty,defaults;sigemptyset(&empty);sigemptyset(&defaults);
      for(int signal:{SIGINT,SIGTERM,SIGHUP,SIGPIPE})sigaddset(&defaults,signal);
      short flags=POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF;
#ifdef POSIX_SPAWN_SETSID
      flags|=POSIX_SPAWN_SETSID;
#else
      flags|=POSIX_SPAWN_SETPGROUP;
      if(posix_spawnattr_setpgroup(&attributes,0))throw std::runtime_error("Cannot isolate caller Chrome group");
#endif
      if(posix_spawnattr_setsigmask(&attributes,&empty)||posix_spawnattr_setsigdefault(&attributes,&defaults)||
          posix_spawnattr_setflags(&attributes,flags))throw std::runtime_error("Cannot configure caller Chrome signals");
      const auto html="<!doctype html><title>Caller-owned attachment proof</title><input id='persist' value='caller input'>"
          "<script>window.__callerToken='original-caller-state';window.__callerViewport={width:innerWidth,height:innerHeight,dpr:devicePixelRatio};</script>";
      std::vector<std::string> args{executable,"--headless=new","--remote-debugging-port=0",
          "--user-data-dir="+profile.string(),"--window-size=947,663","--force-device-scale-factor=1",
          "--disable-dev-shm-usage","--no-first-run","--no-default-browser-check",data_url(html)};
      std::vector<char*> argv;for(auto& arg:args)argv.push_back(arg.data());argv.push_back(nullptr);
      const int error=posix_spawn(&pid,executable.c_str(),&actions,&attributes,argv.data(),environ);
      if(error){pid=-1;throw std::runtime_error("Cannot launch caller-owned Chrome");}
      posix_spawn_file_actions_destroy(&actions);actions_ready=false;
      posix_spawnattr_destroy(&attributes);attributes_ready=false;
      const auto deadline=Clock::now()+10s;
      while(Clock::now()<deadline){
        std::ifstream active(profile/"DevToolsActivePort");std::string path;
        if(std::getline(active,port)&&std::getline(active,path)&&!port.empty()&&path.starts_with("/devtools/browser/")){
          websocket="ws://127.0.0.1:"+port+path;return;
        }
        int status=0;
        if(waitpid(pid,&status,WNOHANG)==pid){pid=-1;throw std::runtime_error("Caller Chrome exited during startup; inspect stderr log");}
        std::this_thread::sleep_for(20ms);
      }
      throw std::runtime_error("Caller Chrome debugging port did not become ready");
    }catch(...){
      if(actions_ready)posix_spawn_file_actions_destroy(&actions);
      if(attributes_ready)posix_spawnattr_destroy(&attributes);
      retire();throw;
    }
  }
  bool alive(){
    if(pid<1)return false;
    int status=0;auto reaped=waitpid(pid,&status,WNOHANG);
    if(reaped==pid){exit_status=status;pid=-1;return false;}
    if(reaped<0&&errno==ECHILD){pid=-1;return false;}
    return kill(pid,0)==0;
  }
  bool wait_natural(std::chrono::milliseconds timeout){
    const auto deadline=Clock::now()+timeout;
    while(alive()&&Clock::now()<deadline)std::this_thread::sleep_for(10ms);
    return !alive()&&exit_status>=0&&WIFEXITED(exit_status)&&WEXITSTATUS(exit_status)==0;
  }
  void retire() noexcept {
    if(pid>0){
      const auto owned=pid;
      if(alive()){
        (void)kill(-owned,SIGTERM);
        const auto deadline=Clock::now()+500ms;
        while(Clock::now()<deadline&&alive())std::this_thread::sleep_for(10ms);
        if(pid>0){(void)kill(-owned,SIGKILL);while(waitpid(owned,nullptr,0)<0&&errno==EINTR){}pid=-1;}
      }
    }
    if(!profile.empty()){std::error_code ignored;std::filesystem::remove_all(profile,ignored);profile.clear();}
  }
  ~CallerChrome(){retire();}
};
std::unique_ptr<suitecut::detail::CdpConnection> probe(const std::string& endpoint){
  suitecut::LaunchOptions options;options.backend=suitecut::BrowserBackend::chromium;
  options.cdp_endpoint=endpoint;options.startup_timeout=3s;options.shutdown_timeout=500ms;
  return suitecut::detail::CdpConnection::launch(options);
}
Json raw(suitecut::detail::CdpConnection& wire,const std::string& method,Json params=Json::object{},const std::string& session={}){
  return wire.call(method,std::move(params),session,Clock::now()+3s);
}
std::set<std::string> contexts(suitecut::detail::CdpConnection& wire){
  std::set<std::string> result;
  const auto response=raw(wire,"Target.getBrowserContexts");
  for(const auto& id:response.at("browserContextIds").as_array())result.insert(id.as_string());
  return result;
}
std::set<std::string> targets(suitecut::detail::CdpConnection& wire){
  std::set<std::string> result;
  const auto response=raw(wire,"Target.getTargets");
  for(const auto& target:response.at("targetInfos").as_array())
    if(target.at("type")==Json("page"))result.insert(target.at("targetId").as_string());
  return result;
}
Json caller_state(suitecut::detail::CdpConnection& wire,const std::string& target){
  auto attached=raw(wire,"Target.attachToTarget",Json{{"targetId",target},{"flatten",true}});
  const auto session=attached.at("sessionId").as_string();
  try{
    auto state=raw(wire,"Runtime.evaluate",Json{{"expression",
        "({token:window.__callerToken,input:document.querySelector('#persist')?.value,viewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio},initialViewport:window.__callerViewport,url:location.href})"},
        {"returnByValue",true}},session).at("result").at("value");
    raw(wire,"Target.detachFromTarget",Json{{"sessionId",session}});return state;
  }catch(...){try{raw(wire,"Target.detachFromTarget",Json{{"sessionId",session}});}catch(...){}throw;}
}
#endif
} // namespace

int main(int argc,char** argv){
  std::string executable,output="/tmp/chromium-attach-results.json";
  for(int i=1;i<argc;i+=2){
    if(i+1>=argc){std::cerr<<"Expected --browser/--output value\n";return 2;}
    if(std::string(argv[i])=="--browser")executable=argv[i+1];
    else if(std::string(argv[i])=="--output")output=argv[i+1];
    else{std::cerr<<"Unknown option\n";return 2;}
  }
  if(executable.empty()){std::cerr<<"--browser /absolute/path/to/chrome required\n";return 2;}
  Json report{{"passed",false},{"checks",Json::array{}},{"scope","Caller-owned local Chrome attach/detach; no Node/CEF"}};
  int code=0;
  try{
#ifdef _WIN32
    throw std::runtime_error("Native caller-owned Chrome ownership proof requires POSIX");
#else
    const auto destination=std::filesystem::absolute(output);
    std::filesystem::create_directories(destination.parent_path());
    CallerChrome caller(std::filesystem::absolute(executable).string(),destination.parent_path()/"caller-chrome-stderr.log");
    auto inspector=probe(caller.websocket);
    std::string caller_target;
    const auto deadline=Clock::now()+5s;
    Json baseline;
    while(Clock::now()<deadline){
      const auto response=raw(*inspector,"Target.getTargets");
      for(const auto& info:response.at("targetInfos").as_array())
        if(info.at("type")==Json("page")&&info.at("url").as_string().starts_with("data:text/html"))caller_target=info.at("targetId").as_string();
      if(!caller_target.empty()){
        try{baseline=caller_state(*inspector,caller_target);if(baseline.contains("token"))break;}catch(...){}
      }
      std::this_thread::sleep_for(20ms);
    }
    check(baseline.contains("token")&&baseline.at("token")==Json("original-caller-state"),"caller establishes page state before SDK attachment",report);
    const auto baseline_contexts=contexts(*inspector),baseline_targets=targets(*inspector);
    inspector->close();
    sc_client* c_handle=nullptr;sc_error c_error{};
    const auto c_connected=sc_client_connect_chromium(caller.port.c_str(),nullptr,&c_handle,&c_error);
    std::unique_ptr<sc_client,decltype(&sc_client_destroy)> c_client(c_handle,&sc_client_destroy);
    check(c_connected==SC_OK,"C ABI attaches to caller Chrome with default null options",report);
    check(sc_client_close(c_client.get(),&c_error)==SC_OK&&sc_client_close(c_client.get(),&c_error)==SC_OK,
          "C ABI attachment close is idempotent",report);
    c_client.reset();check(caller.alive(),"C ABI close and destroy preserve caller process lifetime",report);
    suitecut::LaunchOptions options;options.width=1280;options.height=720;
    options.startup_timeout=5s;options.shutdown_timeout=2s;
    auto browser=suitecut::Browser::connect_chromium(caller.port,options);
    auto page=browser.main_page();page.set_timeout(3s);
    check(page.evaluate("window.__callerToken")==Json("original-caller-state"),"public connect controls the existing caller page",report);
    check(page.evaluate("({width:innerWidth,height:innerHeight,dpr:devicePixelRatio})")==baseline.at("viewport"),"attachment preserves caller viewport",report);
    check(page.evaluate("document.querySelector('#persist').value")==Json("caller input"),"attachment preserves caller input state",report);
    auto owned_context=browser.new_context();auto owned_page=owned_context.new_page();
    owned_page.navigate(data_url("<!doctype html><title>SDK temporary context</title>SDK-owned target"));
    bool default_page_rejected=false;
    try{auto forbidden=browser.new_page();(void)forbidden;}catch(const std::exception&){default_page_rejected=true;}
    check(default_page_rejected,"attachment rejects creating targets in the caller default context",report);
    auto second_owned_page=owned_context.new_page();second_owned_page.navigate(data_url("<!doctype html><title>SDK second context page</title>SDK-owned second target"));
    inspector=probe(caller.websocket);
    check(contexts(*inspector).size()==baseline_contexts.size()+1,"SDK creates one independently owned context",report);
    check(targets(*inspector).size()==baseline_targets.size()+2,"SDK owns both temporary targets inside its own context",report);
    inspector->close();
    browser.close();
    check(caller.alive(),"SDK close leaves caller Chrome alive",report);
    check(std::filesystem::exists(caller.profile/"DevToolsActivePort"),"SDK close leaves caller profile intact",report);
    inspector=probe(caller.websocket);
    check(contexts(*inspector)==baseline_contexts,"SDK close retires only SDK-created contexts",report);
    check(targets(*inspector)==baseline_targets,"SDK close retires temporary targets and preserves caller target",report);
    check(caller_state(*inspector,caller_target)==baseline,"detachment preserves caller page state and viewport",report);
    inspector->close();
    auto reconnected=suitecut::Browser::connect_chromium(caller.websocket,options);
    check(reconnected.main_page().evaluate("window.__callerToken")==Json("original-caller-state"),"browser WebSocket URL supports reconnection",report);
    reconnected.close();check(caller.alive(),"repeated detach never owns caller process lifetime",report);
    inspector=probe(caller.websocket);
    // This is the caller's explicit process shutdown, independent of SDK detach.
    // Some Chrome versions close CDP before acknowledging Browser.close, so the
    // authoritative success condition is the naturally reaped zero process exit.
    try{raw(*inspector,"Browser.close");}catch(const std::exception& error){report["callerShutdownResponseError"]=error.what();}
    inspector->close();
    check(caller.wait_natural(3s),"caller gracefully closes and reaps its own Chrome with exit zero",report);
    report["callerExitCode"]=WEXITSTATUS(caller.exit_status);
    caller.retire();check(!caller.alive(),"caller physically retires its own Chrome",report);
    report["baselineState"]=baseline;report["passed"]=true;
#endif
  }catch(const std::exception& error){report["error"]=error.what();std::cerr<<error.what()<<'\n';code=1;}
  std::ofstream file(output);file<<report.dump()<<'\n';file.close();
  if(!file){std::cerr<<"Cannot finish attach proof report\n";return 1;}
  std::cout<<report.dump()<<std::endl;return code;
}
