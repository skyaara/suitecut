// SPDX-License-Identifier: MIT
// Run only under the native proof supervisor and measurement lock.
#include "suitecut/browser.hpp"
#include "suitecut/client.h"
#include "cdp_internal.hpp"
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <unistd.h>
extern char** environ;
#endif
namespace {
using suitecut::Json;
using namespace std::chrono_literals;
using Clock=std::chrono::steady_clock;
void check(bool condition,const std::string& label,Json& report){if(!condition)throw std::runtime_error(label);report["checks"].as_array().push_back(label);std::cout<<Json{{"event","check"},{"name",label}}.dump()<<std::endl;}
Json raw(suitecut::detail::CdpConnection& wire,const std::string& method,Json params=Json::object{}){return wire.call(method,std::move(params),"",Clock::now()+5s);}
auto connect(const std::string& endpoint){suitecut::LaunchOptions options;options.backend=suitecut::BrowserBackend::firefox;options.cdp_endpoint=endpoint;options.startup_timeout=5s;options.shutdown_timeout=500ms;return suitecut::detail::CdpConnection::connect_websocket(options);}
void session(suitecut::detail::CdpConnection& wire){raw(wire,"session.new",Json{{"capabilities",Json{{"alwaysMatch",Json{{"unhandledPromptBehavior","ignore"}}}}}});}
Json script(suitecut::detail::CdpConnection& wire,const std::string& context,const std::string& expression){const auto result=raw(wire,"script.evaluate",Json{{"expression",expression},{"target",Json{{"context",context}}},{"awaitPromise",true},{"resultOwnership","none"}});if(result.at("type")!=Json("success"))throw std::runtime_error("Caller script failed: "+result.dump());return result.at("result");}
std::set<std::string> context_ids(suitecut::detail::CdpConnection& wire){const auto result=raw(wire,"browser.getUserContexts");std::set<std::string> ids;for(const auto& item:result.at("userContexts").as_array())ids.insert(item.at("userContext").as_string());return ids;}
#ifndef _WIN32
struct CallerFirefox {
  pid_t pid=-1,group=-1;std::filesystem::path profile,log;std::string endpoint;int exit_status=-1;Json helper_exits=Json::array{};bool helper_statuses_ok=true;
  CallerFirefox(const std::string& executable,const std::filesystem::path& directory){
    log=directory/"caller-firefox.log";auto pattern=(std::filesystem::temp_directory_path()/"suitecut-firefox-attach-XXXXXX").string();std::vector<char> bytes(pattern.begin(),pattern.end());bytes.push_back(0);const auto made=mkdtemp(bytes.data());if(!made)throw std::runtime_error("Cannot create caller Firefox profile");profile=made;
    posix_spawn_file_actions_t files;posix_spawnattr_t attrs;posix_spawn_file_actions_init(&files);posix_spawnattr_init(&attrs);posix_spawn_file_actions_addopen(&files,0,"/dev/null",O_RDONLY,0);posix_spawn_file_actions_addopen(&files,1,log.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);posix_spawn_file_actions_adddup2(&files,1,2);posix_spawnattr_setflags(&attrs,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attrs,0);
    std::vector<std::string> args{executable,"--headless","--no-remote","--remote-debugging-port=0","--profile",profile.string()};std::vector<char*> argv;for(auto& value:args)argv.push_back(value.data());argv.push_back(nullptr);const int error=posix_spawn(&pid,executable.c_str(),&files,&attrs,argv.data(),environ);posix_spawn_file_actions_destroy(&files);posix_spawnattr_destroy(&attrs);if(error){pid=-1;retire();throw std::runtime_error("Cannot launch caller Firefox");}group=pid;
    try{const auto deadline=Clock::now()+15s;while(Clock::now()<deadline){std::ifstream input(log);std::string line;while(std::getline(input,line)){const std::string marker="WebDriver BiDi listening on ";const auto offset=line.find(marker);if(offset!=std::string::npos){endpoint=line.substr(offset+marker.size());const auto end=endpoint.find_first_of(" \r\n\t");if(end!=std::string::npos)endpoint.resize(end);if(endpoint.ends_with('/'))endpoint.pop_back();endpoint+="/session";return;}}if(!alive())throw std::runtime_error("Caller Firefox exited before endpoint readiness");std::this_thread::sleep_for(20ms);}throw std::runtime_error("Caller Firefox endpoint readiness timed out");}catch(...){retire();throw;}
  }
  bool alive(){if(pid<1)return false;int status=0;const auto result=waitpid(pid,&status,WNOHANG);if(result==pid){exit_status=status;pid=-1;return false;}return result==0&&kill(pid,0)==0;}
  bool drain_helpers(){
    if(group<1)return true;
    const auto deadline=Clock::now()+1s;
    while(Clock::now()<deadline){
      int status=0;const auto child=waitpid(-group,&status,WNOHANG);
      if(child>0){const bool accepted=(WIFEXITED(status)&&WEXITSTATUS(status)==0)||(WIFSIGNALED(status)&&WTERMSIG(status)==SIGTERM);Json item{{"pid",child},{"rawStatus",status},{"accepted",accepted}};if(WIFEXITED(status))item["exitCode"]=WEXITSTATUS(status);if(WIFSIGNALED(status))item["signal"]=WTERMSIG(status);helper_exits.as_array().push_back(std::move(item));helper_statuses_ok=helper_statuses_ok&&accepted;continue;}
      if(child<0&&errno==ECHILD)return helper_statuses_ok;
      if(child<0&&errno!=EINTR)return false;
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }
  bool natural(){const auto deadline=Clock::now()+5s;while(alive()&&Clock::now()<deadline)std::this_thread::sleep_for(10ms);return pid<1&&exit_status>=0&&WIFEXITED(exit_status)&&WEXITSTATUS(exit_status)==0&&drain_helpers();}
  void retire()noexcept{if(pid>0){const auto owned=pid;if(alive()){kill(-owned,SIGTERM);const auto deadline=Clock::now()+500ms;while(alive()&&Clock::now()<deadline)std::this_thread::sleep_for(10ms);if(pid>0){kill(-owned,SIGKILL);while(waitpid(owned,nullptr,0)<0&&errno==EINTR){}pid=-1;}}}if(group>0){kill(-group,SIGTERM);drain_helpers();}if(!profile.empty()){std::error_code ignored;std::filesystem::remove_all(profile,ignored);profile.clear();}}
  ~CallerFirefox(){retire();}
};
#endif
}
int main(int argc,char** argv){
  if(argc!=3){std::cerr<<"usage: firefox_attach FIREFOX OUTPUT_DIRECTORY\n";return 2;}
  const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);Json report{{"scope","Native Firefox WebDriver BiDi attachment, caller preservation and C ABI; no Node/geckodriver"},{"passed",false},{"checks",Json::array{}}};
  try{
#ifdef _WIN32
    throw std::runtime_error("Firefox ownership fixture currently requires POSIX");
#else
#ifdef __linux__
    if(prctl(PR_SET_CHILD_SUBREAPER,1)<0)throw std::runtime_error("Cannot enable proof-local subreaper");
#endif
    report["processAudit"]=Json{{"subreaperScope","test executable only; SDK does not change embedding reaper policy"},{"sdkOwnedGroups","backend validates helper exit 0 or SIGTERM and bounded-reaps only its private group"}};
    CallerFirefox caller(argv[1],output);auto probe=connect(caller.endpoint);session(*probe);const auto tree=raw(*probe,"browsingContext.getTree",Json{{"maxDepth",0},{"moz:scope","content"}});const auto& roots=tree.at("contexts").as_array();if(roots.empty())throw std::runtime_error("Caller Firefox has no initial page");auto main=raw(*probe,"browsingContext.create",Json{{"type","tab"},{"userContext","default"}}).at("context").as_string();
    raw(*probe,"browsingContext.navigate",Json{{"context",main},{"url","data:text/html,<title>Caller Firefox</title><input id=persist value=caller>"},{"wait","complete"}});raw(*probe,"browsingContext.setViewport",Json{{"context",main},{"viewport",Json{{"width",913},{"height",617}}}});script(*probe,main,"window.callerToken='preserve-native-firefox'; document.querySelector('#persist').value='caller input'; true");const auto before=script(*probe,main,"JSON.stringify({token:callerToken,input:document.querySelector('#persist').value,w:innerWidth,h:innerHeight})").at("value").as_string();const auto original_contexts=context_ids(*probe);raw(*probe,"session.end");probe->close();probe.reset();check(caller.alive(),"caller browser survives ending its setup session",report);
    auto attached=suitecut::Browser::connect_firefox(caller.endpoint);auto page=attached.main_page();check(attached.ready().at("backend")==Json("firefox"),"public native Firefox attachment reports its actual backend",report);check(page.evaluate("JSON.stringify({token:callerToken,input:document.querySelector('#persist').value,w:innerWidth,h:innerHeight})").as_string()==before,"SDK attachment preserves caller document, input and viewport",report);
    {bool rejected=false;try{auto duplicate=suitecut::Browser::connect_firefox(caller.endpoint);duplicate.close();}catch(const std::exception&){rejected=true;}check(rejected&&caller.alive(),"occupied BiDi session rejects without ending the active caller session",report);}
    {auto context=attached.new_context();auto one=context.new_page();auto two=context.new_page();one.set_content("<p>SDK owned one</p>");two.set_content("<p>SDK owned two</p>");check(one.evaluate("document.body.innerText")==Json("SDK owned one"),"SDK-owned Firefox contexts and tabs are usable",report);attached.close();}
    attached.close();attached.close();check(caller.alive()&&std::filesystem::exists(caller.profile),"idempotent SDK close preserves caller Firefox process and profile",report);
    probe=connect(caller.endpoint);session(*probe);{const auto rebound=raw(*probe,"browsingContext.getTree",Json{{"maxDepth",0},{"moz:scope","content"}});main.clear();for(const auto& item:rebound.at("contexts").as_array())if(item.at("url").as_string().starts_with("data:text/html,<title>Caller Firefox</title>")){if(!main.empty())throw std::runtime_error("Caller page identity is ambiguous");main=item.at("context").as_string();}if(main.empty())throw std::runtime_error("Caller page disappeared across BiDi sessions");}check(context_ids(*probe)==original_contexts,"SDK-created user contexts retire on attachment close",report);check(script(*probe,main,"JSON.stringify({token:callerToken,input:document.querySelector('#persist').value,w:innerWidth,h:innerHeight})").at("value").as_string()==before,"caller tab identity and state survive SDK disconnect",report);raw(*probe,"session.end");probe->close();probe.reset();
    sc_client* client=nullptr;sc_error error{};check(sc_client_connect_firefox(caller.endpoint.c_str(),nullptr,&client,&error)==SC_OK&&client,"C ABI Firefox connect accepts nullable launch options",report);sc_packet* packet=nullptr;check(sc_client_ready(client,&packet,&error)==SC_OK,"C ABI Firefox ready succeeds",report);std::size_t size=0;const char* header=sc_packet_header(packet,&size);const auto ready=Json::parse(std::string_view(header,size));sc_packet_destroy(packet);check(ready.at("backend")==Json("firefox"),"C ABI reports Firefox without a CDP compatibility claim",report);const std::string command="{\"command\":\"evaluate\",\"expression\":\"6*7\"}";packet=nullptr;check(sc_client_call(client,command.data(),command.size(),5000,&packet,&error)==SC_OK,"C ABI evaluates over native BiDi",report);const auto* payload=sc_packet_payload(packet,&size);const auto result=Json::parse(std::string_view(reinterpret_cast<const char*>(payload),size));sc_packet_destroy(packet);check(result.at("result").at("value")==Json(42),"C ABI preserves normalized public evaluate result",report);check(sc_client_close(client,&error)==SC_OK,"C ABI attachment close succeeds",report);sc_client_destroy(client);check(caller.alive(),"C ABI disconnect preserves caller-owned Firefox",report);
    probe=connect(caller.endpoint);session(*probe);{const auto rebound=raw(*probe,"browsingContext.getTree",Json{{"maxDepth",0},{"moz:scope","content"}});main.clear();for(const auto& item:rebound.at("contexts").as_array())if(item.at("url").as_string().starts_with("data:text/html,<title>Caller Firefox</title>"))main=item.at("context").as_string();if(main.empty())throw std::runtime_error("Caller page disappeared after C ABI disconnect");}check(script(*probe,main,"callerToken").at("value")==Json("preserve-native-firefox"),"caller can establish a new real BiDi session after C ABI disconnect",report);raw(*probe,"browser.close");probe->close();probe.reset();check(caller.natural(),"caller explicitly closes Firefox and process retires naturally",report);report["processAudit"]["callerParentRawStatus"]=caller.exit_status;report["processAudit"]["callerHelperExits"]=caller.helper_exits;
    sc_launch_options owned_options;sc_launch_options_init(&owned_options);owned_options.executable=argv[1];owned_options.width=640;owned_options.height=480;owned_options.startup_timeout_ms=15000;owned_options.shutdown_timeout_ms=3000;client=nullptr;
    check(sc_client_launch_firefox(&owned_options,&client,&error)==SC_OK&&client,"C ABI owns a separate native Firefox launch",report);
    const auto cleanup_client=[](sc_client* value){if(value)sc_client_destroy(value);};std::unique_ptr<sc_client,decltype(cleanup_client)> owned_client(client,cleanup_client);
    packet=nullptr;check(sc_client_call(client,command.data(),command.size(),5000,&packet,&error)==SC_OK,"C ABI owned Firefox evaluates through actual BiDi",report);payload=sc_packet_payload(packet,&size);const auto owned_result=Json::parse(std::string_view(reinterpret_cast<const char*>(payload),size));sc_packet_destroy(packet);check(owned_result.at("result").at("value")==Json(42),"C ABI native launch returns expected value",report);check(sc_client_close(client,&error)==SC_OK,"C ABI owned Firefox closes and retires its process/profile",report);owned_client.reset();
#endif
    report["passed"]=true;
  }catch(const std::exception& error){report["error"]=error.what();}
  std::ofstream file(output/"results.json");file<<report.dump()<<'\n';std::cout<<report.dump()<<std::endl;return report.at("passed").as_bool()?0:1;
}
