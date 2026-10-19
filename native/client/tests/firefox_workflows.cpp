// SPDX-License-Identifier: MIT
// Real local HTTP proof of the public native Firefox/BiDi browser and agent.
// This executable starts one bounded fixture thread and no Node/driver process.
#include <suitecut/agent.hpp>
#include <suitecut/agent_driver.hpp>
#include <suitecut/browser.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
using suitecut::Json;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
class FixtureServer {
 public:
  FixtureServer(){
    socket_=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(socket_<0)throw std::runtime_error("Cannot create Firefox fixture socket");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(::bind(socket_,reinterpret_cast<sockaddr*>(&address),sizeof(address))||::listen(socket_,16)){::close(socket_);throw std::runtime_error("Cannot bind Firefox fixture socket");}
    socklen_t length=sizeof(address);if(::getsockname(socket_,reinterpret_cast<sockaddr*>(&address),&length)){::close(socket_);throw std::runtime_error("Cannot inspect fixture port");}
    port_=ntohs(address.sin_port);
    try{worker_=std::thread([this]{serve();});}catch(...){::close(socket_);throw;}
  }
  ~FixtureServer(){stopping_=true;::shutdown(socket_,SHUT_RDWR);if(worker_.joinable())worker_.join();::close(socket_);}
  std::string origin(bool alternate=false)const{return "http://"+std::string(alternate?"localhost":"127.0.0.1")+":"+std::to_string(port_);}
  bool failed()const{return failed_.load();}
  int remote_requests()const{return remote_requests_.load();}
 private:
  static std::string main_page(){return R"HTML(<!doctype html><html lang=en><meta charset=utf-8><title>Native Firefox HTTP proof</title>
<style>body{font:16px sans-serif;margin:16px}label,button,select{margin:6px;display:block}iframe{width:420px;height:190px;display:block;margin:8px}#drag-source{display:inline-block;width:80px;height:45px;background:#bdc}#drag-target{display:inline-block;width:110px;height:55px;background:#dcb;margin-left:170px}</style>
<label>Customer<input id=customer></label><button id=save>Save customer</button><output id=result>Waiting</output>
<label>Confirm order<input id=confirm type=checkbox></label><label>Region<select id=region><option value=us>United States</option><option value=fr>France</option></select></label>
<label>Tag<input class=tag></label><label>Tag<input class=tag></label><label>Keyboard<input id=keyboard></label>
<div role=textbox aria-label="Editable note" contenteditable id=editable></div><button id=hover>Hover proof</button><div id=shadow></div>
<label>Upload evidence<input id=upload type=file multiple></label><output id=upload-result></output>
<iframe id=child title="Child note" src=/frame></iframe><output id=frame-result></output>
<div id=drag-source>Pointer source</div><div id=drag-target>Pointer target</div><output id=drag-result></output>
<script>
window.nativeEvents=[];for(const type of ['pointermove','pointerdown','pointerup','click','keydown','input'])document.addEventListener(type,e=>{if(window.nativeEvents.length<128)window.nativeEvents.push({type,target:e.target.id||e.target.localName,x:e.clientX??null,y:e.clientY??null,key:e.key||null,trusted:e.isTrusted})},true);window.saveCount=0;window.keyEvents=[];
document.querySelector('#save').onclick=e=>{window.saveTrusted=e.isTrusted;window.saveCount++;document.querySelector('#result').textContent='Saved '+document.querySelector('#customer').value};
document.querySelector('#confirm').onchange=e=>window.checkTrusted=e.isTrusted;
document.querySelector('#keyboard').onkeydown=e=>keyEvents.push({key:e.key,trusted:e.isTrusted});
document.querySelector('#hover').onmouseenter=e=>window.hoverTrusted=e.isTrusted;
const root=document.querySelector('#shadow').attachShadow({mode:'open'});root.innerHTML='<label>Shadow code<input id=code></label><button>Save shadow</button><output></output>';
root.querySelector('button').onclick=e=>{window.shadowTrusted=e.isTrusted;root.querySelector('output').textContent=root.querySelector('input').value};
document.querySelector('#upload').onchange=async()=>{const rows=[];for(const f of document.querySelector('#upload').files)rows.push(f.name+':'+await f.text());document.querySelector('#upload-result').textContent=rows.join('|')};
addEventListener('message',e=>{if(e.data&&e.data.kind==='note')document.querySelector('#frame-result').textContent=e.data.value});
const source=document.querySelector('#drag-source');source.onpointerdown=e=>{window.dragDownTrusted=e.isTrusted;source.setPointerCapture(e.pointerId)};
source.onpointermove=e=>{if(e.buttons&1)window.dragLast={x:e.clientX,y:e.clientY,trusted:e.isTrusted}};
source.onpointerup=e=>{window.dragUpTrusted=e.isTrusted;const r=document.querySelector('#drag-target').getBoundingClientRect();document.querySelector('#drag-result').textContent=e.clientX>=r.left&&e.clientX<=r.right&&e.clientY>=r.top&&e.clientY<=r.bottom?'dropped':'outside'};
</script></html>)HTML";}
  static std::string frame_page(bool nested){return nested?R"HTML(<!doctype html><meta charset=utf-8><title>Nested Firefox frame</title><label>Nested note<input id=note></label><button>Send nested</button><script>window.nativeEvents=[];for(const type of ['pointermove','pointerdown','pointerup','click','keydown','input'])document.addEventListener(type,e=>{if(window.nativeEvents.length<128)window.nativeEvents.push({type,target:e.target.id||e.target.localName,x:e.clientX??null,y:e.clientY??null,key:e.key||null,trusted:e.isTrusted})},true);document.querySelector('button').onclick=e=>{window.lastTrusted=e.isTrusted;top.postMessage({kind:'note',value:document.querySelector('input').value},'*')}</script>)HTML":R"HTML(<!doctype html><meta charset=utf-8><title>Firefox frame</title><label>Frame note<input id=note></label><button>Send note</button><iframe id=nested title="Nested note" src=/nested style="width:320px;height:80px"></iframe><script>window.nativeEvents=[];for(const type of ['pointermove','pointerdown','pointerup','click','keydown','input'])document.addEventListener(type,e=>{if(window.nativeEvents.length<128)window.nativeEvents.push({type,target:e.target.id||e.target.localName,x:e.clientX??null,y:e.clientY??null,key:e.key||null,trusted:e.isTrusted})},true);document.querySelector('button').onclick=e=>{window.lastTrusted=e.isTrusted;top.postMessage({kind:'note',value:document.querySelector('input').value},'*')}</script>)HTML";}
  void serve()noexcept{
    while(!stopping_){
      const int client=::accept4(socket_,nullptr,nullptr,SOCK_CLOEXEC);if(client<0){if(!stopping_)failed_=true;return;}
      timeval timeout{1,0};::setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
      try{
        std::string request;char bytes[2048];
        while(request.size()<16384&&request.find("\r\n\r\n")==std::string::npos){const auto count=::recv(client,bytes,sizeof(bytes),0);if(count<=0)break;request.append(bytes,static_cast<std::size_t>(count));}
        const auto first=request.find(' '),last=request.find(' ',first==std::string::npos?0:first+1);
        if(first!=std::string::npos&&last!=std::string::npos){
          const auto path=request.substr(first+1,last-first-1);
          auto lowered=request;for(auto& c:lowered)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          if(lowered.find("host: localhost:")!=std::string::npos)++remote_requests_;
          const auto body=path.starts_with("/nested")?frame_page(true):path.starts_with("/frame")?frame_page(false):main_page();
          const auto response="HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
          std::size_t offset=0;while(offset<response.size()){const auto count=::send(client,response.data()+offset,response.size()-offset,MSG_NOSIGNAL);if(count<=0)break;offset+=static_cast<std::size_t>(count);}
        }
      }catch(...){failed_=true;}
      ::close(client);
    }
  }
  int socket_=-1,port_=0;std::thread worker_;std::atomic<bool> stopping_{false},failed_{false};std::atomic<int> remote_requests_{0};
};
void Check(bool condition,const std::string& name,Json& report){if(!condition)throw std::runtime_error("Check failed: "+name);report["checks"].as_array().emplace_back(name);std::cout<<Json{{"event","check"},{"name",name}}.dump()<<std::endl;}
void Write(const std::filesystem::path& path,const std::string& data){std::ofstream out(path,std::ios::binary);out.write(data.data(),static_cast<std::streamsize>(data.size()));if(!out)throw std::runtime_error("Cannot write "+path.string());}
std::string Ref(const Json& tree,const std::string& role,const std::string& name){
  if(tree.is_array()){for(const auto& value:tree.as_array()){auto found=Ref(value,role,name);if(!found.empty())return found;}}
  else if(tree.is_object()){if(tree.contains("role")&&tree.contains("name")&&tree.contains("ref")&&tree.at("role")==Json(role)&&tree.at("name")==Json(name))return tree.at("ref").as_string();if(tree.contains("children"))return Ref(tree.at("children"),role,name);}
  return {};
}
std::uint32_t U32(const std::vector<std::uint8_t>& bytes,std::size_t offset){return (std::uint32_t(bytes.at(offset))<<24)|(std::uint32_t(bytes.at(offset+1))<<16)|(std::uint32_t(bytes.at(offset+2))<<8)|std::uint32_t(bytes.at(offset+3));}
}
int main(int argc,char** argv){
  if(::prctl(PR_SET_CHILD_SUBREAPER,1)<0){std::cerr<<"Cannot establish standalone Firefox proof subreaper\n";return 2;}
  std::string executable,output="firefox-workflows.json";std::vector<std::string> arguments;
  for(int index=1;index<argc;index+=2){if(index+1==argc){std::cerr<<"Expected --browser/--output/--browser-arg value\n";return 2;}const std::string key=argv[index],value=argv[index+1];if(key=="--browser")executable=value;else if(key=="--output")output=value;else if(key=="--browser-arg")arguments.push_back(value);else{std::cerr<<"Unknown option "<<key<<'\n';return 2;}}
  if(executable.empty()){std::cerr<<"--browser /absolute/path/to/firefox is required\n";return 2;}
  Json report{{"client","native-cpp"},{"backend","firefox-bidi"},{"passed",false},{"checks",Json::array{}},
    {"scope","Finite real local HTTP Firefox workflows; native C++ browser/agent, actual WebDriver BiDi, no Node or WebDriver process."},
    {"helperRetirementPolicy","Standalone proof subreaper; SDK reaps only its owned Firefox group and rejects helper exits other than normal 0 or SIGTERM 15."},
    {"unverified",Json::array{"directory uploads","HTML5 drag data transfer"}}};
  const auto started=Clock::now();std::optional<suitecut::Browser> browser;std::optional<FixtureServer> fixture;int exit_code=0;std::string phase="startup";
  try{
    const auto directory=std::filesystem::absolute(output).parent_path();std::filesystem::create_directories(directory);fixture.emplace();auto& server=*fixture;
    suitecut::LaunchOptions options;options.executable=std::filesystem::absolute(executable).string();options.arguments=arguments;options.startup_timeout=15s;options.shutdown_timeout=3s;
    browser.emplace(suitecut::Browser::launch_firefox(options));report["ready"]=browser->ready();report["fixtureOrigin"]=server.origin();
    auto page=browser->main_page();page.set_timeout(5s);page.navigate(server.origin()+"/");
    Check(page.evaluate("document.title")==Json("Native Firefox HTTP proof"),"Firefox navigates a real local HTTP document",report);
    Check(page.evaluate("window.nativeMainWorld=17;window.nativeMainWorld")==Json(17),"evaluation executes in the author's actual main realm",report);
    const auto raw_tree=page.bidi("browsingContext.getTree",Json{{"maxDepth",0},{"moz:scope","content"}});
    Check(!raw_tree.at("contexts").as_array().empty(),"public native Page.bidi returns actual Firefox browsing contexts",report);
    {suitecut::AgentDriver raw_driver(*browser);
      const auto raw_result=raw_driver.call("bidi",Json{{"method","browsingContext.getTree"},{"params",Json{{"maxDepth",0},{"moz:scope","content"}}}});
      Check(!raw_result.at("contexts").as_array().empty(),"published agent bidi tool dispatches the actual native protocol",report);}
    bool no_cdp=false;try{page.devtools("Browser.getVersion");}catch(const std::exception&){no_cdp=true;}
    Check(no_cdp,"Firefox rejects Chromium DevTools calls explicitly",report);
    const auto observation=page.observe();const auto save_ref=Ref(observation,"button","Save customer");
    Check(!save_ref.empty(),"semantic observation provides a genuine actionable DOM reference",report);
    const Json customer{{"kind","label"},{"value","Customer"}};
    const auto batch=suitecut::Agent::run(page,Json::array{
      Json{{"action","fill"},{"target",customer},{"value","Firefox λ 😀"},{"expect",Json{{"kind","inputValue"},{"target",customer},{"value","Firefox λ 😀"}}}},
      Json{{"action","click"},{"target",Json{{"kind","ref"},{"value",save_ref}}},{"expect",Json{{"kind","text"},{"target",Json{{"kind","css"},{"value","#result"}}},{"value","Saved Firefox λ 😀"}}}}
    });
    report["agentBatch"]=batch.json();
    Check(batch.ok&&page.evaluate("window.saveTrusted===true&&window.saveCount===1").as_bool(),"native agent verifies real Unicode fill and trusted click",report);
    page.observe();bool stale=false;try{page.get_by_ref(save_ref).with_timeout(300ms).click();}catch(const std::exception&){stale=true;}
    Check(stale&&page.evaluate("window.saveCount===1").as_bool(),"a full observation invalidates previous document reference tokens before input",report);
    bool strict=false;try{page.get_by_label("Tag").with_timeout(300ms).fill("wrong");}catch(const std::exception&){strict=true;}
    Check(strict&&page.evaluate("Array.from(document.querySelectorAll('.tag')).every(e=>e.value==='')").as_bool(),"strict duplicate semantics reject before changing either control",report);
    page.get_by_label("Tag").nth(1).fill("second Ω");
    Check(page.evaluate("document.querySelectorAll('.tag')[0].value===''&&document.querySelectorAll('.tag')[1].value==='second Ω'").as_bool(),"semantic nth selects the requested actual DOM occurrence",report);
    page.get_by_label("Confirm order").check();
    Check(page.get_by_label("Confirm order").is_checked()&&page.evaluate("window.checkTrusted===true").as_bool(),"native check uses trusted browser input",report);
    page.get_by_label("Confirm order").uncheck();Check(!page.get_by_label("Confirm order").is_checked(),"native uncheck changes the actual checkbox",report);
    page.get_by_label("Region").select_option("fr");Check(page.get_by_label("Region").input_value()=="fr","native select updates the actual browser select control",report);
    page.get_by_label("Shadow code").fill("Shadow 漢字");page.get_by_role("button","Save shadow").click();
    Check(page.evaluate("document.querySelector('#shadow').shadowRoot.querySelector('output').textContent==='Shadow 漢字'&&window.shadowTrusted===true").as_bool(),"semantic and CSS queries enter open shadow DOM with trusted activation",report);
    page.get_by_role("textbox","Editable note").fill("Editable λ 😀");Check(page.evaluate("document.querySelector('#editable').textContent==='Editable λ 😀'").as_bool(),"contenteditable receives actual Unicode browser editing",report);
    page.get_by_label("Keyboard").type("ab");page.get_by_label("Keyboard").press("Control+A");page.get_by_label("Keyboard").press("Backspace");page.get_by_label("Keyboard").type("fox");
    Check(page.get_by_label("Keyboard").input_value()=="fox"&&page.evaluate("window.keyEvents.length>=5&&window.keyEvents.every(e=>e.trusted)").as_bool(),"type and modifier chords deliver trusted keyboard events",report);
    page.get_by_role("button","Hover proof").hover();Check(page.evaluate("window.hoverTrusted===true").as_bool(),"hover delivers a native trusted mouse event",report);
    auto child=page.frame_locator("#child");child.get_by_label("Frame note").fill("Frame λ");child.get_by_role("button","Send note").click();
    Check(page.wait_for_function("document.querySelector('#frame-result').textContent==='Frame λ'").as_bool()&&child.evaluate("window.lastTrusted===true").as_bool(),"same-origin frame locators preserve trusted input and actual frame evaluation",report);
    auto nested=child.frame_locator("#nested");nested.get_by_label("Nested note").fill("Nested 😀");nested.get_by_role("button","Send nested").click();
    Check(page.wait_for_function("document.querySelector('#frame-result').textContent==='Nested 😀'").as_bool()&&nested.evaluate("window.lastTrusted===true").as_bool(),"nested frame paths resolve actual BiDi WindowProxy contexts",report);
    page.evaluate("(()=>{const e=document.createElement('iframe');e.id='remote';e.src="+Json(server.origin(true)+"/frame").dump()+";document.body.append(e)})()");
    auto remote=page.frame_locator("#remote");phase="cross-origin fill";remote.get_by_label("Frame note").fill("Cross origin Ω");phase="cross-origin click";remote.get_by_role("button","Send note").click();phase="cross-origin outcome";
    Check(page.wait_for_function("document.querySelector('#frame-result').textContent==='Cross origin Ω'").as_bool()&&remote.evaluate("window.lastTrusted===true").as_bool(),"new cross-origin frame targeting stays inside the actual selected context",report);
    const auto upload_a=directory/"proof-λ.txt",upload_b=directory/"proof-漢字.txt";Write(upload_a,"Actual Firefox bytes 😀");Write(upload_b,"Second bytes Ω");
    page.get_by_label("Upload evidence").set_input_files({upload_a.string(),upload_b.string()});
    Check(page.wait_for_function("document.querySelector('#upload-result').textContent==='proof-λ.txt:Actual Firefox bytes 😀|proof-漢字.txt:Second bytes Ω'").as_bool(),"BiDi setFiles uploads actual file bytes and Unicode filenames",report);
    bool missing=false;try{page.get_by_label("Upload evidence").set_input_files({(directory/"does-not-exist.txt").string()});}catch(const std::exception&){missing=true;}
    Check(missing&&page.evaluate("document.querySelector('#upload').files.length===2").as_bool(),"missing upload files reject before replacing a real selection",report);
    page.get_by_label("Upload evidence").set_input_files({});Check(page.evaluate("document.querySelector('#upload').files.length===0").as_bool(),"native file selection can be cleared and verified",report);
    {
      suitecut::AgentDriver driver(*browser);const auto tools=suitecut::AgentDriver::tools();Check(tools.is_array()&&tools.as_array().size()>30,"native agent driver exposes its real Firefox tool schemas",report);
      driver.call("fill",Json{{"target",customer},{"text","Driver Ω"}});driver.call("click",Json{{"target",Json{{"kind","role"},{"value","button"},{"name","Save customer"}}}});
      Check(page.evaluate("document.querySelector('#result').textContent==='Saved Driver Ω'&&window.saveTrusted===true").as_bool(),"persistent agent tools drive the real Firefox document",report);
      const auto outcome=driver.call("drag",Json{{"source",Json{{"kind","css"},{"value","#drag-source"}}},{"target",Json{{"kind","css"},{"value","#drag-target"}}},{"options",Json{{"steps",8}}}});report["pointerDrag"]=outcome;
      Check(page.evaluate("document.querySelector('#drag-result').textContent==='dropped'&&window.dragDownTrusted===true&&window.dragUpTrusted===true&&window.dragLast.trusted===true").as_bool(),"native pointer drag delivers trusted down, movement and up to an actual widget",report);
      const auto submitted=driver.call("submit",Json{{"family","evaluate"},{"fields",Json{{"expression","window.promptReply=prompt('Native Firefox prompt','');window.promptReply"}}},{"timeoutMs",3000}});const auto id=submitted.at("id");
      bool opened=false;const auto limit=Clock::now()+1s;
      while(!opened&&Clock::now()<limit){const auto events=driver.call("events",Json{{"maxEvents",256},{"waitMs",20}});for(const auto& row:events.at("events").as_array()){const auto& data=row.at("data");if(data.is_object()&&data.contains("method")&&data.at("method")==Json("browsingContext.userPromptOpened")&&data.at("params").at("message")==Json("Native Firefox prompt"))opened=true;}}
      Check(opened&&driver.call("result",Json{{"id",id}}).at("status")==Json("pending"),"BiDi exposes the actual pending browser prompt without blocking result polling",report);
      page.handle_dialog(true,"Reply λ");const auto completed=driver.call("result",Json{{"id",id},{"waitMs",1000}});
      Check(completed.at("status")==Json("completed")&&page.evaluate("window.promptReply")==Json("Reply λ"),"a native continuation resolves the actual prompt and resumes its author script",report);
    }
    page.set_viewport(1024,768);Check(page.evaluate("({width:innerWidth,height:innerHeight})")==Json{{"width",1024},{"height",768}},"BiDi viewport changes actual browser CSS dimensions",report);
    const auto png=page.screenshot();Check(png.size()>24&&png[0]==137&&png[1]=='P'&&png[2]=='N'&&png[3]=='G'&&U32(png,16)==1024&&U32(png,20)==768,"Firefox screenshot contains actual PNG pixels at the requested viewport",report);
    Write(directory/"firefox-proof.png",std::string(reinterpret_cast<const char*>(png.data()),png.size()));
    auto context=browser->new_context();auto first=context.new_page();first.navigate(server.origin()+"/");context.add_cookies(Json::array{Json{{"name","native-fox"},{"value","cookie-proof"},{"domain","127.0.0.1"},{"path","/"}}});
    Check(first.evaluate("document.cookie.includes('native-fox=cookie-proof')").as_bool(),"BiDi context cookies reach the actual HTTP origin",report);
    auto second=context.new_page();second.navigate(server.origin()+"/");Check(second.evaluate("document.cookie.includes('native-fox=cookie-proof')").as_bool(),"new native Firefox tabs share their browser user context cookies",report);
    Check(!page.evaluate("document.cookie.includes('native-fox=cookie-proof')").as_bool(),"a separate Firefox user context isolates cookies from the main page",report);
    first.get_by_label("Customer").fill("first tab");Check(second.get_by_label("Customer").input_value().empty(),"shared-context tabs retain independent document form state",report);
    second.close();first.close();context.close();Check(!server.failed(),"bounded local HTTP fixture completed without a server failure",report);
    report["passed"]=true;
  }catch(const std::exception& error){
    report["error"]=error.what();report["failureStage"]=phase;if(fixture)report["remoteHttpRequests"]=fixture->remote_requests();exit_code=1;
    if(browser)try{
      auto diagnostic=browser->main_page().with_timeout(1000ms);
      report["failureFrames"]=diagnostic.frames();
      report["failureDocument"]=diagnostic.evaluate("(()=>{const e=document.querySelector('#remote');const r=e?.getBoundingClientRect();return {url:location.href,readyState:document.readyState,remoteSrc:e?.src||null,remoteBox:r?{x:r.x,y:r.y,width:r.width,height:r.height}:null,frameResult:document.querySelector('#frame-result')?.textContent||null,activeElement:document.activeElement?.id||document.activeElement?.localName||null,events:window.nativeEvents||[]};})()");
      report["failureRemoteDocument"]=diagnostic.frame_locator("#remote").evaluate("({url:location.href,note:document.querySelector('#note')?.value||'',activeElement:document.activeElement?.id||document.activeElement?.localName||null,lastTrusted:window.lastTrusted??null,events:window.nativeEvents||[]})");
      report["failureNestedDocument"]=diagnostic.frame_locator("#child").frame_locator("#nested").evaluate("({note:document.querySelector('#note')?.value||'',activeElement:document.activeElement?.id||document.activeElement?.localName||null,events:window.nativeEvents||[]})");
    }catch(const std::exception& detail){report["diagnosticError"]=detail.what();}
  }
  if(browser)try{browser->close();}catch(const std::exception& error){report["closeError"]=error.what();report["passed"]=false;exit_code=1;}
  report["elapsedMs"]=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
  try{Write(output,report.dump()+"\n");}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
  std::cout<<report.dump()<<std::endl;return exit_code;
}
