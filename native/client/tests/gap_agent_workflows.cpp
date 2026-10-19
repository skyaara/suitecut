// SPDX-License-Identifier: MIT
#include "suitecut/agent_driver.hpp"
#include "suitecut/image.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
using suitecut::Json;
namespace {
struct Fixture {
  int fd=-1; std::thread worker;std::atomic<bool> closing=false;int port=0;
  Fixture() {
    fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)throw std::runtime_error("fixture socket");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)) || listen(fd,8))throw std::runtime_error("fixture bind");
    socklen_t size=sizeof(address);getsockname(fd,reinterpret_cast<sockaddr*>(&address),&size);port=ntohs(address.sin_port);
    worker=std::thread([this]{while(!closing) {
      pollfd p{fd,POLLIN,0};if(poll(&p,1,50)<=0)continue;
      const int client=accept4(fd,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);if(client<0)continue;
      char request[8192];std::string received;pollfd q{client,POLLIN,0};if(poll(&q,1,1000)>0){const auto count=recv(client,request,sizeof(request),0);if(count>0)received.assign(request,static_cast<std::size_t>(count));}
      const auto first=received.find(' '),last=received.find(' ',first==std::string::npos?0:first+1);
      const auto path=first!=std::string::npos&&last!=std::string::npos?received.substr(first+1,last-first-1):std::string("/login");
      std::string body=R"(<html><title>Credential fixture</title><form><input name=username><input type=password><button>Login</button></form><output></output><script>document.querySelector('form').onsubmit=e=>{e.preventDefault();window.submitted={trusted:e.isTrusted,username:document.querySelector('[name=username]').value,passwordLength:document.querySelector('[type=password]').value.length};document.querySelector('output').textContent='Logged in'}</script></html>)";
      if(path.starts_with("/redirect")) {
        const auto destination=Json("http://localhost:"+std::to_string(port)+"/attacker").dump();
        body="<!doctype html><title>Missing password redirect</title><form><input name=username></form><script>const input=document.querySelector('[name=username]');let scheduled=false;function redirect(){if(scheduled)return;scheduled=true;const ready=input.value.length>0?'1':'0';setTimeout(()=>location.href="+destination+"+'?ready='+ready,80)}input.addEventListener('input',redirect);setTimeout(redirect,1000);</script>";
      } else if(path.starts_with("/attacker")) {
        body=R"(<!doctype html><title>Different origin credential target</title><form><input name=username><input type=password><button>Login</button></form><script>window.attackerInputs=0;window.attackerKeys=0;window.attackerSubmitted=false;document.addEventListener('input',()=>window.attackerInputs++);document.addEventListener('keydown',()=>window.attackerKeys++);document.querySelector('form').onsubmit=e=>{e.preventDefault();window.attackerSubmitted=true}</script>)";
      }
      const auto response="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
      std::size_t offset=0;while(offset<response.size()) {const auto n=send(client,response.data()+offset,response.size()-offset,MSG_NOSIGNAL);if(n<=0)break;offset+=n;}
      close(client);
    }});
  }
  ~Fixture(){closing=true;if(worker.joinable())worker.join();if(fd>=0)close(fd);}
};
void save(const std::filesystem::path& p,const std::vector<std::uint8_t>& bytes){std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!out)throw std::runtime_error("save failed");}
}
int main(int argc,char** argv) {
  if(argc!=3)return 2;
  Json::array checks;
  auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);checks.emplace_back(label);};
  try {
    const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
    suitecut::LaunchOptions options;options.executable=argv[1];options.width=320;options.height=240;
    auto browser=suitecut::Browser::launch_chromium(options);auto page=browser.main_page();suitecut::AgentDriver driver(browser);
    check(suitecut::AgentDriver::tools().as_array().size()==53,"53 native agent tools");
    page.navigate("data:text/html,<html style='background:rgb(10,20,30)'><title>Pixels</title></html>");
    const auto png=page.screenshot();save(output/"baseline.png",png);
    const auto equal=driver.call("image_compare",Json{{"expectedPath",(output/"baseline.png").string()}});
    check(equal.at("equal").as_bool() && equal.at("changedPixels").as_int()==0,"agent live screenshot exact pixel comparison");
    page.evaluate("document.documentElement.style.background='rgb(11,20,30)'");
    const auto tolerant=driver.call("image_compare",Json{{"expectedPath",(output/"baseline.png").string()},{"channelTolerance",1}});
    check(tolerant.at("equal").as_bool(),"agent pixel channel tolerance");
    const auto changed=driver.call("image_compare",Json{{"expectedPath",(output/"baseline.png").string()},{"diffPath",(output/"difference.png").string()}});
    check(!changed.at("equal").as_bool() && changed.at("changedRatio").as_double()==1,"agent full viewport changed pixel count");
    check(std::filesystem::file_size(output/"difference.png")>40,"native PNG diff artifact");
    bool overwritten=false;try{driver.call("image_compare",Json{{"expectedPath",(output/"baseline.png").string()},{"diffPath",(output/"difference.png").string()}});}catch(...){overwritten=true;}
    check(overwritten,"existing diff output preserved");
    Fixture fixture;const auto url="http://127.0.0.1:"+std::to_string(fixture.port)+"/login";
    auto vault=std::make_shared<suitecut::CredentialVault>((output/"private"/"vault.bin").string(),"test master key only");
    driver.set_credential_vault(vault);
    const Json credential{{"url",url},{"username","native-user"},{"password","test-secret-123"}};
    const auto saved=driver.call("credentials",Json{{"action","save"},{"name","fixture"},{"credential",credential}});
    check(saved.at("saved").as_bool() && saved.dump().find("test-secret")==std::string::npos,"agent encrypted credential save has no secret response");
    const auto listed=driver.call("credentials",Json{{"action","list"}});
    check(listed.at("names").as_array().size()==1,"agent lists credential names only");
    const auto login=driver.call("credentials",Json{{"action","login"},{"name","fixture"}});
    check(login.at("submitted").as_bool(),"agent encrypted credential native login");
    const auto submitted=page.evaluate("window.submitted");
    check(submitted.at("trusted").as_bool() && submitted.at("username")==Json("native-user") && submitted.at("passwordLength")==Json(15),"real native login form trusted submit and field readback");
    auto redirected_credential=credential;redirected_credential["url"]="http://127.0.0.1:"+std::to_string(fixture.port)+"/redirect";
    driver.call("credentials",Json{{"action","save"},{"name","redirect-fixture"},{"credential",redirected_credential}});
    bool redirect_blocked=false;try{driver.call("credentials",Json{{"action","login"},{"name","redirect-fixture"}});}catch(const std::exception& error){redirect_blocked=std::string(error.what()).find("origin")!=std::string::npos;}
    page.with_timeout(std::chrono::seconds(3)).wait_for_function("location.hostname==='localhost'&&typeof window.attackerInputs==='number'");
    const auto redirect_state=page.evaluate("({usernameReady:new URLSearchParams(location.search).get('ready')==='1',usernameLength:document.querySelector('[name=username]').value.length,passwordLength:document.querySelector('[type=password]').value.length,inputCount:window.attackerInputs,keyCount:window.attackerKeys,submitted:window.attackerSubmitted})");
    check(redirect_blocked&&redirect_state.at("usernameReady").as_bool()&&redirect_state.at("usernameLength")==Json(0)&&redirect_state.at("passwordLength")==Json(0)&&redirect_state.at("inputCount")==Json(0)&&redirect_state.at("keyCount")==Json(0)&&!redirect_state.at("submitted").as_bool(),"credential redirect while password locator is missing rejects before entering any secret on the other origin");
    check(driver.call("credentials",Json{{"action","remove"},{"name","redirect-fixture"}}).at("removed").as_bool(),"redirect fixture credential removal");
    check(driver.call("credentials",Json{{"action","remove"},{"name","fixture"}}).at("removed").as_bool(),"agent credential removal");
    browser.close();
    const Json result{{"ok",true},{"checks",checks},{"imageComparison",changed},{"credentialRedirect",redirect_state},{"tools",53}};
    std::ofstream(output/"results.json")<<result.dump()<<'\n';std::cout<<result.dump()<<'\n';return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';std::cout<<Json{{"ok",false},{"checks",checks},{"error",error.what()}}.dump()<<'\n';return 1;}
}
