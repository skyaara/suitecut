// SPDX-License-Identifier: MIT
// Identical public-API HTTP workflows for native CEF, Chromium and Firefox.
#include "suitecut/browser.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using suitecut::Json;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr const char* text_body="native HAR text λ";
constexpr const char* post_body="posted=HAR-proof";
constexpr std::size_t slow_bytes=8*1024*1024;
const std::string binary_body=[]{
  const unsigned char data[]{0,1,2,127,128,255,'n','a','t','i','v','e'};
  return std::string(reinterpret_cast<const char*>(data),sizeof data);
}();
const std::string download_body=[]{std::string out="Native download bytes\n";out.push_back('\0');out+=binary_body;return out;}();

class Fixture {
 public:
  Fixture() {
    listener_=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(listener_<0)throw std::runtime_error("Cannot create fixture listener");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(::bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof address)||::listen(listener_,32)) {
      ::close(listener_);throw std::runtime_error("Cannot bind loopback fixture");
    }
    socklen_t size=sizeof address;
    if(::getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&size)) {
      ::close(listener_);throw std::runtime_error("Cannot inspect fixture port");
    }
    port_=ntohs(address.sin_port);
    try{for(int i=0;i<4;++i)workers_.emplace_back([this]{serve();});}
    catch(...){close();throw;}
  }
  ~Fixture(){close();}
  std::string origin()const{return "http://127.0.0.1:"+std::to_string(port_);}
  int slow_aborted()const{return slow_aborted_.load();}
  Json receipts()const {
    std::lock_guard lock(mutex_);Json::array out;
    for(const auto&[path,count]:requests_)out.emplace_back(Json{{"path",path},{"requests",count}});
    return Json{{"requests",std::move(out)},{"slowTransfersAborted",slow_aborted_.load()},{"slowTransfersCompleted",slow_completed_.load()},{"errors",errors_.load()}};
  }
 private:
  void close() {
    if(stopping_.exchange(true))return;
    ::shutdown(listener_,SHUT_RDWR);
    {std::lock_guard lock(mutex_);for(int client:clients_)::shutdown(client,SHUT_RDWR);}
    for(auto& worker:workers_)if(worker.joinable())worker.join();
    if(listener_>=0)::close(listener_);
  }
  bool send_all(int fd,const std::string& data) {
    std::size_t at=0;
    while(at<data.size()&&!stopping_) {
      const auto n=::send(fd,data.data()+at,data.size()-at,MSG_NOSIGNAL);
      if(n<=0)return false;
      at+=static_cast<std::size_t>(n);
    }
    return at==data.size();
  }
  static std::string header(const std::string& request,const std::string& name) {
    std::istringstream lines(request);std::string line;
    while(std::getline(lines,line)) {
      auto colon=line.find(':');if(colon==std::string::npos)continue;
      auto key=line.substr(0,colon);for(char& c:key)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if(key!=name)continue;
      auto value=line.substr(colon+1);while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.erase(value.begin());
      if(!value.empty()&&value.back()=='\r')value.pop_back();
      return value;
    }
    return {};
  }
  void handle(int fd) {
    std::string request;char buffer[4096];std::size_t wanted=0;
    for(;;) {
      const auto n=::recv(fd,buffer,sizeof buffer,0);if(n<=0)return;
      request.append(buffer,static_cast<std::size_t>(n));
      if(request.size()>65536)throw std::runtime_error("Fixture request exceeds 64 KiB");
      const auto end=request.find("\r\n\r\n");
      if(end!=std::string::npos) {
        const auto length=header(request.substr(0,end),"content-length");
        wanted=end+4+(length.empty()?0:static_cast<std::size_t>(std::stoul(length)));
        if(wanted>65536)throw std::runtime_error("Fixture request body exceeds budget");
        if(request.size()>=wanted)break;
      }
    }
    const auto first=request.find(' '),last=request.find(' ',first+1);
    if(first==std::string::npos||last==std::string::npos)return;
    const auto path=request.substr(first+1,last-first-1);
    {std::lock_guard lock(mutex_);++requests_[path];}
    std::string body,type="text/plain; charset=utf-8",extra,status="200 OK";
    if(path=="/"||path.starts_with("/?")) {
      type="text/html; charset=utf-8";
      body=R"HTML(<!doctype html><meta charset=utf-8><title>Common native features</title>
<style>body{font:16px sans-serif;margin:24px}a{display:block;padding:12px;width:260px}</style>
<a id=download href="/download">Download actual bytes</a><a id=slow href="/slow-download">Download slow bytes</a>
<script>window.downloadClickedTrusted=false;for(const a of document.querySelectorAll('a'))a.onclick=e=>window.downloadClickedTrusted=e.isTrusted;</script>)HTML";
    }else if(path.starts_with("/download")) {
      body=download_body;type="application/octet-stream";extra="Content-Disposition: attachment; filename=receipt.bin\r\n";
    }else if(path.starts_with("/slow-download")) {
      const std::string response="HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=slow.bin\r\nContent-Length: "+std::to_string(slow_bytes)+"\r\n\r\n";
      bool success=send_all(fd,response);const std::string chunk(65536,'S');
      for(std::size_t at=0;success&&at<slow_bytes;at+=chunk.size()) {
        success=send_all(fd,chunk);if(success)std::this_thread::sleep_for(20ms);
      }
      if(success)++slow_completed_;else ++slow_aborted_;return;
    }else if(path.starts_with("/har/disconnect")) {
      // A genuine transport failure, not an HTTP error response or mocked
      // browser rejection. The worker closes this accepted socket immediately.
      return;
    }else if(path.starts_with("/har/redirect")) {
      status="302 Found";extra="Location: /har/text?redirected=1\r\n";body="redirecting";
    }else if(path.starts_with("/har/text"))body=text_body;
    else if(path.starts_with("/har/binary")){type="application/octet-stream";body=binary_body;}
    else if(path.starts_with("/har/post"))body=request.substr(request.find("\r\n\r\n")+4);
    else if(path.starts_with("/ua"))body=header(request,"user-agent");
    else {status="404 Not Found";body="missing";}
    send_all(fd,"HTTP/1.1 "+status+"\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: "+type+"\r\n"+extra+"Content-Length: "+std::to_string(body.size())+"\r\n\r\n"+body);
  }
  void serve() {
    while(!stopping_) {
      const int fd=::accept4(listener_,nullptr,nullptr,SOCK_CLOEXEC);
      if(fd<0){if(stopping_)return;continue;}
      timeval timeout{2,0};::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
      {std::lock_guard lock(mutex_);clients_.insert(fd);}
      try{handle(fd);}catch(...){++errors_;}
      {std::lock_guard lock(mutex_);clients_.erase(fd);}
      ::close(fd);
    }
  }
  int listener_=-1,port_=0;
  mutable std::mutex mutex_;
  std::set<int> clients_;
  std::map<std::string,int> requests_;
  std::vector<std::thread> workers_;
  std::atomic<bool> stopping_{false};
  std::atomic<int> errors_{0},slow_aborted_{0},slow_completed_{0};
};

std::string read_file(const std::filesystem::path& path) {
  const auto size=std::filesystem::file_size(path);if(size>32*1024*1024)throw std::runtime_error("Fixture file exceeds read budget");
  std::ifstream in(path,std::ios::binary);std::string data(static_cast<std::size_t>(size),'\0');
  if(!in.read(data.data(),static_cast<std::streamsize>(data.size())))throw std::runtime_error("Cannot read fixture output");
  return data;
}
std::string decode64(const std::string& data) {
  const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if(data.size()%4||data.size()>32*1024*1024)throw std::runtime_error("Invalid fixture base64 body");
  std::string out;unsigned bits=0;int count=0;
  for(unsigned char c:data) {
    if(c=='=')break;
    const auto digit=alphabet.find(c);if(digit==std::string::npos)throw std::runtime_error("Invalid HAR base64 alphabet");
    bits=(bits<<6)|static_cast<unsigned>(digit);count+=6;
    if(count>=8){count-=8;out.push_back(static_cast<char>((bits>>count)&255));}
  }
  return out;
}
std::string content(const Json& entry) {
  const auto& c=entry.at("response").at("content");
  if(!c.contains("text")||!c.at("text").is_string())throw std::runtime_error("Embedded HAR response body is unavailable");
  return c.contains("encoding")&&c.at("encoding")==Json("base64")?decode64(c.at("text").as_string()):c.at("text").as_string();
}
void check(bool value,const std::string& name,Json::array& checks) {
  if(!value)throw std::runtime_error(name);
  checks.emplace_back(name);
}
template<class Predicate> void eventually(Predicate predicate,Clock::duration timeout=5s) {
  const auto deadline=Clock::now()+timeout;
  for(;;){if(predicate())return;if(Clock::now()>=deadline)throw std::runtime_error("Actual native outcome did not arrive before the fixture deadline");std::this_thread::sleep_for(10ms);}
}
Json wait_download(suitecut::Page& page,const std::string& url) {
  Json record;
  eventually([&]{const auto list=page.downloads();for(const auto& item:list.as_array())if(item.at("url")==Json(url)){record=item;return true;}return false;});
  return record;
}
void require_under(const std::filesystem::path& path,const std::filesystem::path& root) {
  auto relative=std::filesystem::canonical(path).lexically_relative(std::filesystem::canonical(root));
  if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("Download path escaped its configured context directory");
}
} // namespace

int main(int argc,char** argv) {
  std::string backend,executable;std::filesystem::path output;std::vector<std::string> arguments;
  for(int i=1;i<argc;i+=2) {
    if(i+1>=argc){std::cerr<<"Expected --backend/--browser/--output/--browser-arg value\n";return 2;}
    const std::string key=argv[i],value=argv[i+1];
    if(key=="--backend")backend=value;else if(key=="--browser")executable=value;
    else if(key=="--output")output=value;else if(key=="--browser-arg")arguments.push_back(value);
    else {std::cerr<<"Unknown argument\n";return 2;}
  }
  if((backend!="cef"&&backend!="chromium"&&backend!="firefox")||executable.empty()||output.empty())return 2;
#ifdef __linux__
  if(backend=="firefox"&&::prctl(PR_SET_CHILD_SUBREAPER,1)<0) {
    std::cerr<<"Cannot establish proof-local Firefox subreaper\n";return 2;
  }
#endif
  output=std::filesystem::absolute(output);std::filesystem::create_directories(output);
  Json report{{"scope","identical public native APIs against real loopback HTTP outcomes; no backend-specific scenario bypass"},
              {"backend",backend},{"scenarios",Json::array{}}};
  Json::array checks;bool all=true;
  try {
    Fixture fixture;suitecut::LaunchOptions options;options.executable=std::filesystem::absolute(executable).string();
    options.backend=backend=="cef"?suitecut::BrowserBackend::cef:backend=="chromium"?suitecut::BrowserBackend::chromium:suitecut::BrowserBackend::firefox;
    options.arguments=arguments;options.startup_timeout=20s;options.shutdown_timeout=5s;
    auto browser=suitecut::Browser::launch(options);auto page=browser.main_page();page.set_timeout(8s);
    report["ready"]=browser.ready();report["fixtureOrigin"]=fixture.origin();
    auto scenario=[&](const std::string& name,const std::function<Json()>& run) {
      const auto started=Clock::now();Json row{{"name",name}};
      try{row["result"]=run();row["ok"]=true;}catch(const std::exception& e){row["ok"]=false;row["error"]=e.what();all=false;}
      row["durationMs"]=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
      report["scenarios"].as_array().push_back(row);std::cout<<row.dump()<<std::endl;
    };
    scenario("download bytes, saveAs and context isolation",[&] {
      auto first=browser.new_context(),second=browser.new_context();auto one=first.new_page(),two=second.new_page();
      one.set_timeout(8s);two.set_timeout(8s);one.navigate(fixture.origin()+"/");two.navigate(fixture.origin()+"/");
      const auto dir1=output/"downloads-one",dir2=output/"downloads-two";std::filesystem::create_directories(dir1);std::filesystem::create_directories(dir2);
      one.set_download_directory(dir1.string());two.set_download_directory(dir2.string());
      one.locator("#download").click();auto info=wait_download(one,fixture.origin()+"/download");
      const auto id=info.at("downloadId").as_string();const auto path=one.download_path(id);require_under(path,dir1);
      check(read_file(path)==download_body,"download_path exposes actual completed binary bytes",checks);
      info=one.download_status(id);check(info.at("state")==Json("completed")&&info.at("suggestedFilename")==Json("receipt.bin"),"normalized download status comes from the native completion",checks);
      check(one.evaluate("window.downloadClickedTrusted").as_bool(),"download was triggered by trusted native click",checks);
      const auto saved=output/"saved-receipt.bin";one.save_download(id,saved.string());check(read_file(saved)==download_body,"saveAs publishes an exact completed download copy",checks);
      check(two.downloads().as_array().empty(),"other context does not list the first context download",checks);
      bool foreign=false;try{(void)two.download_status(id);}catch(const std::exception&){foreign=true;}
      check(foreign,"foreign-context download IDs are rejected",checks);
      auto sibling=first.new_page();sibling.set_timeout(8s);sibling.navigate(fixture.origin()+"/");
      bool sibling_status=false,sibling_path=false,sibling_cancel=false;
      try{(void)sibling.download_status(id);}catch(const std::exception&){sibling_status=true;}
      try{(void)sibling.download_path(id);}catch(const std::exception&){sibling_path=true;}
      try{sibling.cancel_download(id);}catch(const std::exception&){sibling_cancel=true;}
      check(sibling.downloads().as_array().empty()&&sibling_status&&sibling_path&&sibling_cancel,"sibling page cannot inspect, read or cancel another page's download",checks);
      two.locator("#download").click();const auto other=wait_download(two,fixture.origin()+"/download");
      const auto path2=two.download_path(other.at("downloadId").as_string());require_under(path2,dir2);
      check(read_file(path2)==download_body&&std::filesystem::canonical(path)!=std::filesystem::canonical(path2),"context-specific download directories hold distinct actual files",checks);
      first.close();second.close();return Json{{"first",info},{"second",other},{"savedBytes",download_body.size()}};
    });
    scenario("cancel an actual in-progress download",[&] {
      auto context=browser.new_context();auto download_page=context.new_page();download_page.set_timeout(8s);
      download_page.navigate(fixture.origin()+"/");const auto directory=output/"downloads-cancel";std::filesystem::create_directories(directory);
      download_page.set_download_directory(directory.string());const auto aborted_before=fixture.slow_aborted();
      download_page.locator("#slow").click();auto info=wait_download(download_page,fixture.origin()+"/slow-download");
      check(info.at("state")==Json("inProgress"),"slow HTTP transfer is in progress before cancellation",checks);
      const auto id=info.at("downloadId").as_string();
      download_page.cancel_download(id);
      eventually([&]{info=download_page.download_status(id);return info.at("state")==Json("canceled");});
      eventually([&]{return fixture.slow_aborted()>aborted_before;});
      bool unavailable=false;try{(void)download_page.download_path(id);}catch(const std::exception&){unavailable=true;}
      check(unavailable,"canceled transfer has no completed public file",checks);
      check(fixture.slow_aborted()>aborted_before,"native cancellation disconnects the real slow HTTP transfer",checks);
      context.close();return Json{{"supported",true},{"status",info}};
    });
    scenario("CSS media partial updates and null reset",[&] {
      page.navigate(fixture.origin()+"/");
      const auto baseline=page.evaluate("({dark:matchMedia('(prefers-color-scheme:dark)').matches,reduce:matchMedia('(prefers-reduced-motion:reduce)').matches})");
      page.emulate_media(Json{{"colorScheme","dark"},{"reducedMotion","reduce"}});
      check(page.evaluate("matchMedia('(prefers-color-scheme:dark)').matches&&matchMedia('(prefers-reduced-motion:reduce)').matches").as_bool(),"media overrides change actual CSS media query outcomes",checks);
      page.emulate_media(Json{{"colorScheme","light"}});
      check(page.evaluate("matchMedia('(prefers-color-scheme:light)').matches&&matchMedia('(prefers-reduced-motion:reduce)').matches").as_bool(),"omitted media override retains its previous value",checks);
      page.emulate_media(Json{{"colorScheme",Json{}},{"reducedMotion",Json{}}});
      const auto restored=page.evaluate("({dark:matchMedia('(prefers-color-scheme:dark)').matches,reduce:matchMedia('(prefers-reduced-motion:reduce)').matches})");
      check(restored==baseline,"null media reset restores actual browser defaults",checks);
      return Json{{"baseline",baseline},{"restored",restored}};
    });
    scenario("native geolocation coordinates",[&] {
      auto context=browser.new_context();context.grant_permissions({"geolocation"},fixture.origin());auto geo=context.new_page();geo.set_timeout(8s);geo.navigate(fixture.origin()+"/");
      geo.emulate_geolocation(37.7749,-122.4194,7);
      const auto position=geo.evaluate("new Promise((resolve,reject)=>navigator.geolocation.getCurrentPosition(p=>resolve({latitude:p.coords.latitude,longitude:p.coords.longitude,accuracy:p.coords.accuracy}),e=>reject(new Error(e.message)),{maximumAge:0,timeout:5000}))");
      check(std::abs(position.at("latitude").as_double()-37.7749)<1e-6&&std::abs(position.at("longitude").as_double()+122.4194)<1e-6&&std::abs(position.at("accuracy").as_double()-7)<1e-6,"geolocation callback returns actual native emulated coordinates",checks);
      geo.clear_geolocation();context.close();return position;
    });
    scenario("native timezone override",[&] {
      page.navigate(fixture.origin()+"/");page.emulate_timezone("America/New_York");
      const auto zone=page.evaluate("({zone:Intl.DateTimeFormat().resolvedOptions().timeZone,offset:new Date('2026-01-01T12:00:00Z').getTimezoneOffset()})");
      check(zone.at("zone")==Json("America/New_York")&&zone.at("offset").as_double()==300,"timezone changes Intl and Date native outcomes",checks);
      page.emulate_timezone("");return zone;
    });
    scenario("native user-agent override in script and HTTP",[&] {
      page.navigate(fixture.origin()+"/");const std::string ua="SuiteCutCrossBrowserProof/1.0";page.emulate_user_agent(ua);
      const auto result=page.evaluate("fetch('/ua').then(r=>r.text()).then(header=>({header,navigator:navigator.userAgent}))");
      check(result.at("header")==Json(ua)&&result.at("navigator")==Json(ua),"user-agent override reaches real HTTP header and navigator",checks);
      page.emulate_user_agent("");return result;
    });
    scenario("HAR 1.2 text, binary, redirect and POST records",[&] {
      page.start_har(Json{{"content","embed"}});page.navigate(fixture.origin()+"/");
      const auto observed=page.evaluate(R"JS((async()=>{
        const text=await fetch('/har/text').then(r=>r.text());
        const bytes=[...new Uint8Array(await fetch('/har/binary').then(r=>r.arrayBuffer()))];
        const redirected=await fetch('/har/redirect').then(r=>r.text());
        const post=await fetch('/har/post',{method:'POST',headers:{'Content-Type':'text/plain'},body:'posted=HAR-proof'}).then(r=>r.text());
        return {text,bytes,redirected,post};})())JS");
      check(observed.at("text")==Json(text_body)&&observed.at("redirected")==Json(text_body)&&observed.at("post")==Json(post_body)&&observed.at("bytes").as_array().size()==binary_body.size(),"HAR fixture requests return actual text, binary, redirect and POST bodies",checks);
      Json archive;
      eventually([&]{archive=page.export_har();const auto& entries=archive.at("log").at("entries").as_array();return std::count_if(entries.begin(),entries.end(),[&](const Json& e){return e.at("request").at("url").as_string().find("/har/")!=std::string::npos;})>=5;});
      archive=page.stop_har();const auto& log=archive.at("log");
      // Preserve the exact native archive even when a later content assertion
      // fails, so unavailable-body markers remain reviewable evidence.
      std::ofstream(output/"network.har.json")<<archive.dump()<<'\n';
      check(log.at("version")==Json("1.2")&&log.at("creator").is_object()&&log.at("entries").is_array(),"export is an actual HAR 1.2 archive",checks);
      std::map<std::string,Json> entries;
      for(const auto& entry:log.at("entries").as_array()) {
        const auto url=entry.at("request").at("url").as_string();if(url.find("/har/")==std::string::npos)continue;
        entries[url]=entry;
        check(entry.at("request").at("headers").is_array()&&entry.at("response").at("headers").is_array()&&
          entry.at("request").at("httpVersion").is_string()&&entry.at("response").at("httpVersion").is_string()&&
          entry.at("startedDateTime").as_string().find('T')!=std::string::npos&&entry.at("time").as_double()>=0,
          "HAR entry contains standard native network metadata for "+url.substr(url.find("/har/")),checks);
        for(const auto&[key,value]:entry.at("timings").as_object()) {
          if(key.starts_with("_"))continue;
          check(value.is_number()&&value.as_double()>=-1,"HAR timing is measured or explicitly unavailable: "+key,checks);
        }
      }
      const auto& text=entries.at(fixture.origin()+"/har/text");
      const auto& binary=entries.at(fixture.origin()+"/har/binary");
      const auto& redirect=entries.at(fixture.origin()+"/har/redirect");
      const auto& final=entries.at(fixture.origin()+"/har/text?redirected=1");
      const auto& post=entries.at(fixture.origin()+"/har/post");
      check(content(text)==text_body&&content(binary)==binary_body&&content(final)==text_body&&content(post)==post_body,"embedded HAR body bytes match the real HTTP responses",checks);
      check(text.at("response").at("content").at("size").as_double()==std::string(text_body).size()&&
        binary.at("response").at("content").at("size").as_double()==binary_body.size(),"HAR content sizes count actual UTF-8 and decoded binary bytes",checks);
      check(redirect.at("response").at("status").as_int()==302&&final.at("response").at("status").as_int()==200,"HAR preserves both actual redirect hops with integer HTTP status codes",checks);
      check(post.at("request").at("method")==Json("POST")&&post.at("request").at("postData").at("text")==Json(post_body),"HAR preserves actual POST data",checks);
      return Json{{"entries",log.at("entries").as_array().size()},{"fixtureResponse",observed}};
    });
    scenario("HAR omit policy, restart and native page/context isolation",[&] {
      bool invalid=false;try{page.start_har(Json{{"content","invalid-policy"}});}catch(const std::exception&){invalid=true;}
      check(invalid,"unsupported HAR content policy is rejected explicitly",checks);
      page.start_har(Json{{"content","omit"}});
      check(page.evaluate("fetch('/har/text?omit=1').then(r=>r.text())")==Json(text_body),"omit policy still performs a real successful HTTP request",checks);
      auto sibling=browser.new_page();sibling.set_timeout(8s);sibling.navigate(fixture.origin()+"/?harSibling=1");
      check(sibling.evaluate("fetch('/har/text?sibling=1').then(r=>r.text())")==Json(text_body),"sibling page produces a distinct actual HTTP request",checks);
      auto omitted=page.stop_har();std::ofstream(output/"network-omit.har.json")<<omitted.dump()<<'\n';
      bool own=false,foreign=false;
      for(const auto& entry:omitted.at("log").at("entries").as_array()) {
        const auto url=entry.at("request").at("url").as_string();
        if(url.ends_with("/har/text?omit=1")) {
          own=true;const auto& c=entry.at("response").at("content");
          check(!c.contains("text")&&!c.contains("encoding"),"omit HAR contains native response metadata without retained response bytes",checks);
        }
        if(url.find("sibling=1")!=std::string::npos||url.find("harSibling=1")!=std::string::npos)foreign=true;
      }
      check(own&&!foreign,"page HAR excludes actual sibling-page requests in the same context",checks);sibling.close();
      auto other_context=browser.new_context();auto other=other_context.new_page();other.set_timeout(8s);other.navigate(fixture.origin()+"/");
      page.start_har(Json{{"content","embed"}});other.start_har(Json{{"content","embed"}});
      check(other.evaluate("fetch('/har/text?isolated=1').then(r=>r.text())")==Json(text_body)&&
        page.evaluate("fetch('/har/text?restart=1').then(r=>r.text())")==Json(text_body),"restarted and separate-context HAR requests both execute against the real server",checks);
      const auto restarted=page.stop_har(),isolated=other.stop_har();
      std::ofstream(output/"network-restart.har.json")<<restarted.dump()<<'\n';std::ofstream(output/"network-isolated.har.json")<<isolated.dump()<<'\n';
      bool restart_body=false,isolated_body=false,leaked=false,stale=false;
      for(const auto& entry:restarted.at("log").at("entries").as_array()) {
        const auto url=entry.at("request").at("url").as_string();
        if(url.ends_with("/har/text?restart=1"))restart_body=content(entry)==text_body;
        if(url.find("isolated=1")!=std::string::npos)leaked=true;
        if(url.find("omit=1")!=std::string::npos||url.ends_with("/har/post"))stale=true;
      }
      for(const auto& entry:isolated.at("log").at("entries").as_array()) {
        const auto url=entry.at("request").at("url").as_string();
        if(url.ends_with("/har/text?isolated=1"))isolated_body=content(entry)==text_body;
        if(url.find("restart=1")!=std::string::npos)leaked=true;
      }
      check(restart_body&&!stale,"HAR restart clears the previous capture and restores embedded native body collection",checks);
      check(isolated_body&&!leaked,"concurrent HAR captures retain only their own browser-context HTTP requests",checks);
      other_context.close();return Json{{"omittedEntries",omitted.at("log").at("entries").as_array().size()},
        {"restartedEntries",restarted.at("log").at("entries").as_array().size()},{"isolatedEntries",isolated.at("log").at("entries").as_array().size()}};
    });
    scenario("HAR transport failure and subsequent request recovery",[&] {
      page.start_har(Json{{"content","embed"}});
      const auto failure=page.evaluate("fetch('/har/disconnect').then(r=>({rejected:false,status:r.status}),e=>({rejected:true,message:String(e)}))");
      check(failure.at("rejected").as_bool(),"closing the real HTTP socket rejects the browser fetch",checks);
      check(page.evaluate("fetch('/har/text?recovery=1').then(r=>r.text())")==Json(text_body),"browser performs a real successful request after the transport failure",checks);
      const auto archive=page.stop_har();std::ofstream(output/"network-failure.har.json")<<archive.dump()<<'\n';
      bool failed=false,recovered=false;
      for(const auto& entry:archive.at("log").at("entries").as_array()) {
        const auto url=entry.at("request").at("url").as_string();const auto& response=entry.at("response");
        if(url.ends_with("/har/disconnect")) {
          const bool native_error=(entry.contains("_error")&&entry.at("_error").is_string()&&!entry.at("_error").as_string().empty())||
            (response.contains("_failureText")&&response.at("_failureText").is_string()&&!response.at("_failureText").as_string().empty());
          failed=failed||(response.at("status").as_double()==0&&native_error);
        }
        if(url.ends_with("/har/text?recovery=1"))recovered=content(entry)==text_body;
      }
      check(failed,"HAR preserves the native failed request and its actual failure reason",checks);
      check(recovered,"HAR body collection remains usable after a native request failure",checks);
      return Json{{"fetchOutcome",failure},{"entries",archive.at("log").at("entries").as_array().size()}};
    });
    report["serverReceipts"]=fixture.receipts();browser.close();
  }catch(const std::exception& error){all=false;report["fatalError"]=error.what();}
  report["checks"]=Json(std::move(checks));report["ok"]=all;
  std::ofstream(output/"results.json")<<report.dump()<<'\n';std::cout<<report.dump()<<std::endl;return all?0:1;
}
