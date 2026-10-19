// SPDX-License-Identifier: MIT
// Real HTTP/Fission proof: native trusted pointer and HTML5 frame drags.
#include <suitecut/browser.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
using suitecut::Json;
using namespace std::chrono_literals;
namespace {
class Fixture {
 public:
  Fixture(){fd_=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd_<0)throw std::runtime_error("socket failed");sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(::bind(fd_,reinterpret_cast<sockaddr*>(&a),sizeof(a))||::listen(fd_,16))throw std::runtime_error("bind failed");socklen_t n=sizeof(a);if(::getsockname(fd_,reinterpret_cast<sockaddr*>(&a),&n))throw std::runtime_error("getsockname failed");port_=ntohs(a.sin_port);thread_=std::thread([this]{serve();});}
  ~Fixture(){stop_=true;::shutdown(fd_,SHUT_RDWR);if(thread_.joinable())thread_.join();::close(fd_);}
  std::string origin(bool remote=false)const{return "http://"+std::string(remote?"localhost":"127.0.0.1")+":"+std::to_string(port_);}
 private:
  std::string html(bool root)const {
    std::string body=R"HTML(<!doctype html><html><meta charset=utf-8><title>Trusted frame drag</title><style>body{margin:10px;font:16px sans-serif}.box{user-select:none;display:inline-block;box-sizing:border-box;width:100px;height:65px;padding:10px;margin:8px;background:#bdc;vertical-align:top}#target{background:#dcb}iframe{width:290px;height:230px;border:3px solid #abc;display:inline-block;margin:12px;vertical-align:top}</style><div id=source class=box>Source</div><div id=target class=box>Target</div>)HTML";
    if(root)body+="<div><iframe id=same src='/frame'></iframe><iframe id=remote src='"+origin(true)+"/frame'></iframe><iframe id=same2 src='/frame'></iframe></div>";
    body+=R"HTML(<script>
window.events=[];window.starts=[];window.outcomes=[];window.lastDrop=null;const source=document.querySelector('#source'),target=document.querySelector('#target');
function emit(kind,data){top.postMessage({kind,data},'*')}
for(const type of ['pointerdown','pointermove','pointerup','mousedown','mousemove','mouseup','dragstart','dragover','drop','dragend'])document.addEventListener(type,e=>{if(events.length<256)events.push({type,target:e.target.id,trusted:e.isTrusted,buttons:e.buttons??null,x:e.clientX,y:e.clientY})},true);
source.onpointerdown=e=>{if(!source.draggable)e.preventDefault();emit('start',{trusted:e.isTrusted})};
target.onpointermove=e=>{if(e.buttons&1)window.heldMoveTrusted=e.isTrusted};
target.onpointerup=e=>{if(!source.draggable)emit('outcome',{mode:'pointer',trusted:e.isTrusted,heldMoveTrusted:window.heldMoveTrusted===true})};
source.ondragstart=e=>{e.dataTransfer.setData('text/plain','Actual Firefox transfer λ');e.dataTransfer.effectAllowed='copy';window.dragStartTrusted=e.isTrusted;emit('htmlstart',{trusted:e.isTrusted})};
target.ondragover=e=>{e.preventDefault();e.dataTransfer.dropEffect='copy';window.dragOverTrusted=e.isTrusted};
target.ondrop=e=>{e.preventDefault();window.lastDrop={mode:'html5',trusted:e.isTrusted,overTrusted:window.dragOverTrusted===true,text:e.dataTransfer.getData('text/plain')};emit('outcome',lastDrop)};
source.ondragend=e=>emit('end',{trusted:e.isTrusted,effect:e.dataTransfer.dropEffect});
if(window===top)addEventListener('message',e=>{if(e.data?.kind==='start'||e.data?.kind==='htmlstart')starts.push(e.data);if(e.data?.kind==='outcome')outcomes.push(e.data.data)});
window.reset=(html5)=>{events=[];starts=[];outcomes=[];lastDrop=null;heldMoveTrusted=false;dragOverTrusted=false;dragStartTrusted=false;source.draggable=html5};
</script></html>)HTML";return body;
  }
  void serve()noexcept{while(!stop_){int c=::accept4(fd_,nullptr,nullptr,SOCK_CLOEXEC);if(c<0)return;timeval timeout{1,0};::setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));std::string request;char buf[2048];while(request.size()<16384&&request.find("\r\n\r\n")==std::string::npos){auto n=::recv(c,buf,sizeof(buf),0);if(n<=0)break;request.append(buf,static_cast<std::size_t>(n));}const auto body=html(request.starts_with("GET / "));const auto response="HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;std::size_t done=0;while(done<response.size()){auto n=::send(c,response.data()+done,response.size()-done,MSG_NOSIGNAL);if(n<=0)break;done+=static_cast<std::size_t>(n);}::close(c);}}
  int fd_=-1,port_=0;std::atomic<bool> stop_{false};std::thread thread_;
};
void write(const std::string& path,const Json& report){std::ofstream out(path);out<<report.dump()<<'\n';if(!out)throw std::runtime_error("Cannot write drag receipt");}
}
int main(int argc,char** argv){
  if(::prctl(PR_SET_CHILD_SUBREAPER,1)<0)return 2;
  std::string executable,output;bool html5=false,expect_limits=false;
  for(int i=1;i+1<argc;i+=2){std::string key=argv[i],value=argv[i+1];if(key=="--browser")executable=value;else if(key=="--output")output=value;else if(key=="--html5")html5=value=="true";else if(key=="--expect-unsupported")expect_limits=value=="true";else return 2;}
  if(executable.empty()||output.empty())return 2;
  std::filesystem::create_directories(std::filesystem::absolute(output).parent_path());
  Json report{{"ok",false},{"html5Requested",html5},{"expectedCapabilityLimits",expect_limits},{"checks",Json::array{}},{"scenarios",Json::array{}}};std::optional<suitecut::Browser> browser;std::optional<Fixture> server;int status=0;
  try{
    server.emplace();suitecut::LaunchOptions options;options.executable=executable;options.width=1100;options.height=700;options.startup_timeout=15s;options.shutdown_timeout=3s;
    browser.emplace(suitecut::Browser::launch_firefox(options));auto page=browser->main_page();page.set_timeout(4s);page.navigate(server->origin()+"/");
    auto same=page.frame_locator("#same"),remote=page.frame_locator("#remote"),same2=page.frame_locator("#same2");
    same.evaluate("document.title");remote.evaluate("document.title");same2.evaluate("document.title");
    struct Case{const char* name;int from,to;};
    const Case cases[]={{"same root",0,0},{"same frame",1,1},{"root to same-origin frame",0,1},{"same-origin frame to root",1,0},{"same-origin sibling frames",1,3},{"root to cross-origin frame",0,2},{"cross-origin frame to root",2,0},{"same-origin to cross-origin frame",1,2},{"cross-origin to same-origin frame",2,1}};
    const auto locator=[&](int context,const char* selector){if(context==1)return same.locator(selector);if(context==2)return remote.locator(selector);if(context==3)return same2.locator(selector);return page.locator(selector);};
    for(const auto& item:cases){
      const auto reset="window.reset("+std::string(html5?"true":"false")+")";page.evaluate(reset);same.evaluate(reset);remote.evaluate(reset);same2.evaluate(reset);
      Json row{{"name",item.name},{"from",item.from},{"to",item.to}};
      try{
        if(expect_limits&&(html5||item.from!=item.to)) {
          bool rejected=false;std::string reason;
          try{locator(item.from,"#source").drag_to(locator(item.to,"#target"),Json{{"steps",18}});}catch(const std::exception& error){reason=error.what();rejected=reason.find(html5?"HTML5 drag":"cross-context drag")!=std::string::npos;}
          const std::string untouched="window.events.every(e=>e.type!=='pointerdown'&&e.type!=='mousedown'&&e.type!=='dragstart')";
          if(!rejected||!page.evaluate(untouched).as_bool()||!same.evaluate(untouched).as_bool()||!remote.evaluate(untouched).as_bool()||!same2.evaluate(untouched).as_bool())throw std::runtime_error("Capability rejection did not precede native press");
          row["ok"]=true;row["unsupported"]=true;row["reason"]=reason;report["checks"].as_array().emplace_back(std::string("rejects before press: ")+item.name);report["scenarios"].as_array().push_back(row);continue;
        }
        row["result"]=locator(item.from,"#source").drag_to(locator(item.to,"#target"),Json{{"steps",18}});
        page.wait_for_function("window.outcomes.length>0");row["starts"]=page.evaluate("window.starts");row["outcomes"]=page.evaluate("window.outcomes");
        const auto success=page.evaluate(html5?"window.starts.some(e=>e.kind==='htmlstart'&&e.data.trusted)&&window.outcomes.some(e=>e.mode==='html5'&&e.trusted&&e.overTrusted&&e.text==='Actual Firefox transfer λ')":"window.starts.some(e=>e.kind==='start'&&e.data.trusted)&&window.outcomes.some(e=>e.mode==='pointer'&&e.trusted&&e.heldMoveTrusted)").as_bool();
        if(!success)throw std::runtime_error("Actual trusted UI outcome did not match");
        row["ok"]=true;report["checks"].as_array().emplace_back(item.name);
      }catch(const std::exception& error){row["ok"]=false;row["error"]=error.what();row["rootEvents"]=page.evaluate("window.events");row["sameEvents"]=same.evaluate("window.events");row["remoteEvents"]=remote.evaluate("window.events");row["same2Events"]=same2.evaluate("window.events");report["scenarios"].as_array().push_back(row);throw;}
      report["scenarios"].as_array().push_back(row);std::cout<<Json{{"check",item.name},{"ok",true}}.dump()<<std::endl;
    }
    report["ok"]=true;
  }catch(const std::exception& error){report["error"]=error.what();status=1;}
  if(browser)try{browser->close();}catch(const std::exception& error){report["closeError"]=error.what();report["ok"]=false;status=1;}
  write(output,report);return status;
}
