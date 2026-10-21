// SPDX-License-Identifier: MIT
#include "suitecut/browser.hpp"
#include "posix_fixture.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using suitecut::Json;
using namespace std::chrono_literals;
namespace fs=std::filesystem;
class Fixture {
 public:
  Fixture(){
    socket_=suitecut_test::socket();sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(socket_<0||bind(socket_,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(socket_,16))throw std::runtime_error("Cannot start restricted bootstrap fixture");
    socklen_t size=sizeof(address);if(getsockname(socket_,reinterpret_cast<sockaddr*>(&address),&size))throw std::runtime_error("Cannot read fixture port");
    port_=ntohs(address.sin_port);thread_=std::thread([this]{Serve();});
  }
  ~Fixture(){stopped_=true;if(thread_.joinable())thread_.join();if(socket_>=0)::close(socket_);}
  std::string Url(const std::string&path,bool cross=false)const{return std::string("http://")+(cross?"localhost":"127.0.0.1")+":"+std::to_string(port_)+path;}
 private:
  void Serve(){while(!stopped_){
    const int client=suitecut_test::accept(socket_);if(client<0)continue;
    timeval timeout{2,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
#ifdef SO_NOSIGPIPE
    int one=1;setsockopt(client,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
#endif
    char request[8192];const auto n=recv(client,request,sizeof(request),0);std::string path=n>0?std::string(request,static_cast<size_t>(n)):"";
    const bool parent=path.find("GET /parent-")!=std::string::npos;
    const bool cross=path.find("GET /parent-cross")!=std::string::npos;
    const bool nested=path.find("GET /restricted-nested")!=std::string::npos;
    std::string body="<!doctype html><title>Restricted bootstrap regression</title><button onclick='globalThis.clicked=(globalThis.clicked||0)+1'>Actual button</button><script>globalThis.bootstrapProof={seed:globalThis.bootstrapSeed??null,early:document.readyState==='loading',leaked:'nativeSnapshot' in globalThis,xhrBlocked:false};try{const r=new XMLHttpRequest;r.open('GET','/data',false);r.send()}catch(e){bootstrapProof.xhrBlocked=true}</script>";
    if(parent)body+="<iframe id='child' allow=\"sync-xhr 'none'\" src='"+Url(cross?"/restricted-nested":"/restricted",cross)+"'></iframe>";
    if(nested)body+="<iframe id='nested' allow=\"sync-xhr 'none'\" src='"+Url("/restricted",true)+"'></iframe>";
    const std::string headers="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nPermissions-Policy: sync-xhr=()\r\nConnection: close\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n";
    const auto response=headers+body;size_t offset=0;while(offset<response.size()){
#ifdef MSG_NOSIGNAL
      const auto wrote=send(client,response.data()+offset,response.size()-offset,MSG_NOSIGNAL);
#else
      const auto wrote=send(client,response.data()+offset,response.size()-offset,0);
#endif
      if(wrote<=0)break;offset+=static_cast<size_t>(wrote);
    }::close(client);
  }}
  int socket_=-1;unsigned port_=0;std::atomic_bool stopped_{false};std::thread thread_;
};
int main(int argc,char**argv){
  if(argc!=3)return 2;const auto out=fs::absolute(argv[2]);fs::create_directories(out);
  Json report{{"ok",false},{"cases",Json::array{}}};
  try{
    Fixture fixture;suitecut::LaunchOptions options;options.executable=argv[1];options.width=800;options.height=600;
    report["stage"]="launch";auto browser=suitecut::Browser::launch(options);
    auto add=[&](const std::string&source){auto packet=browser.transport()->call_json(Json{{"command","context"},{"action","addInitScript"},{"params",Json{{"contextId",1},{"source",source}}}},10s);report["registration"]=suitecut::response_json(packet);};
    report["stage"]="register";add("globalThis.bootstrapSeed=42;void 0");auto page=browser.main_page();page.set_timeout(10s);
    int version=42;
    for(const auto&path:{"/restricted","/parent-same","/parent-cross"}){
      Json row{{"path",path}};report["stage"]=std::string("navigate:")+path;page.navigate(fixture.Url(path));report["stage"]=std::string("evaluate:")+path;row["root"]=page.evaluate("globalThis.bootstrapProof");report["pending"]=row;
      auto check=[&](const Json&proof){if(!proof.is_object()||!proof.contains("seed")||!proof.at("seed").is_number()||proof.at("seed").as_double()!=version||!proof.at("early").as_bool()||proof.at("leaked").as_bool()||!proof.at("xhrBlocked").as_bool())throw std::runtime_error("Document-start initialization or sync-XHR policy invariant failed: "+proof.dump());};
      row["currentSeed"]=page.evaluate("globalThis.bootstrapSeed??null");report["pending"]=row;check(row.at("root"));
      if(std::string(path)!="/restricted"){
        report["stage"]="child-evaluate";auto child=page.frame_locator("#child");row["child"]=child.evaluate("globalThis.bootstrapProof");report["pending"]=row;check(row.at("child"));
        if(std::string(path)=="/parent-cross"){row["nested"]=child.frame_locator("#nested").evaluate("globalThis.bootstrapProof");report["pending"]=row;check(row.at("nested"));}
      }
      row["diagnostics"]=page.command("diagnostics");if(std::string(path)=="/parent-cross"&&row.at("diagnostics").at("sessions").as_double()<1)throw std::runtime_error("Cross-origin fixture did not create an actual child session");const auto bytes=page.screenshot();std::ofstream f(out/(std::to_string(version)+".png"),std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
      row["ok"]=true;report["cases"].as_array().push_back(row);std::ofstream(out/"results.json")<<report.dump();
      version+=42;add("globalThis.bootstrapSeed="+std::to_string(version)+";void 0");
    }
    if(browser.main_page().evaluate("6*7").as_double()!=42)throw std::runtime_error("Root did not remain responsive");browser.close();report["closed"]=true;report["ok"]=true;report["stage"]="complete";report.as_object().erase("pending");
  }catch(const std::exception&e){report["error"]=e.what();}
  std::ofstream(out/"results.json")<<report.dump()<<'\n';std::cout<<report.dump()<<'\n';return report.at("ok").as_bool()?0:1;
}
