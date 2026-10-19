// SPDX-License-Identifier: MIT
#include "cdp_internal.hpp"
#include "cdp_frame.hpp"
#include <filesystem>
#include "../../shared/dom_driver.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace suitecut::detail {
namespace {
constexpr const char* state_key = kSuiteCutDOMDriverStateKey;
bool flag(const Json& j, const char* k, bool fallback=false) {
  return j.contains(k) ? j.at(k).as_bool() : fallback;
}
std::string text(const Json& j, const char* k, const char* fallback="") {
  return j.contains(k) ? j.at(k).as_string() : fallback;
}
void pause(Deadline deadline) {
  if(std::chrono::steady_clock::now()>=deadline) throw std::runtime_error("Native DOM operation timed out");
  std::this_thread::sleep_until(std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(20)));
}
bool missing_context(const std::exception& error) {
  const std::string message=error.what(),prefix="CDP error: ";
  if(!message.starts_with(prefix))return false;
  try {const auto detail=Json::parse(message.substr(prefix.size()));return detail.at("code")==Json(-32000)&&detail.at("message")==Json("Cannot find context with specified id");}catch(...){return false;}
}
void reset_world(CdpPage& page) {page.isolated_context=0;page.isolated_loader.clear();page.isolated_frame.clear();}
void validate_wire(const Json& value,std::size_t& work,int depth=0) {
  if(++work>100000||depth>64)throw std::runtime_error("Native DOM request traversal limit exceeded");
  if(value.is_string()&&value.as_string().size()>65536)throw std::runtime_error("Native DOM request string exceeds 64 KiB");
  if(value.is_array())for(const auto& child:value.as_array())validate_wire(child,work,depth+1);
  if(value.is_object())for(const auto& [key,child]:value.as_object())validate_wire(child,work,depth+1);
}
struct ScopedModifiers {
  CdpRuntime& rt;CdpPage& page;CdpFrameScope& scope;Deadline deadline;std::vector<std::string> added;
  ScopedModifiers(CdpRuntime& runtime,CdpPage& target,CdpFrameScope& frame,const Json& request,Deadline limit,bool pointer):rt(runtime),page(target),scope(frame),deadline(limit) {
    if(!pointer||!request.contains("modifiers"))return;
    if(!request.at("modifiers").is_array())throw std::runtime_error("Native pointer modifiers must be an array");
    std::vector<std::pair<std::string,int>> desired;
    for(const auto& value:request.at("modifiers").as_array()) {
      auto key=value.as_string();int bit=key=="Alt"?1:key=="Control"||key=="ControlOrMeta"?2:key=="Meta"?4:key=="Shift"?8:0;
      if(!bit)throw std::runtime_error("Unsupported native pointer modifier: "+key);
      desired.emplace_back(key=="ControlOrMeta"?"Control":key,bit);
    }
    try {for(const auto& [key,bit]:desired)if(!(page.modifiers&bit)) {
      scope.input(Json{{"kind","keyboard"},{"action","down"},{"key",key}},deadline);added.push_back(key);
    }} catch(...) {release();throw;}
  }
  void release() noexcept {
    if(added.empty())return;
    CdpCleanupScope cleanup_scope;
    const auto cleanup=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
    for(auto it=added.rbegin();it!=added.rend();++it)try {
      scope.input(Json{{"kind","keyboard"},{"action","up"},{"key",*it}},cleanup);
    }catch(...){}
    added.clear();
  }
  ~ScopedModifiers(){release();}
};
// Each isolated execution context owns these authored functions and a bounded,
// document-specific weak reference registry. Author script cannot access it.
void bootstrap(CdpRuntime& rt,CdpPage& page,Deadline deadline,CdpFrameScope* frame=nullptr) {
  for(;;)try {
  if(rt.value(page,"!!globalThis."+std::string(state_key),deadline,true).as_bool()) return;
  const std::string source=SuiteCutDOMDriverSource();
  rt.value(page,source,deadline,true);return;
  }catch(const std::exception& error) {
    if(!missing_context(error))throw;
    if(frame)frame->refresh(deadline);else reset_world(page);
    pause(deadline);
  }
}
Json operation(CdpRuntime& rt,CdpPage& page,const char* name,const Json& request,Deadline deadline,CdpFrameScope* frame=nullptr) {
  for(;;) {
    // Runtime may select a newly committed document after bootstrap's probe.
    // A missing private helper proves that no DOM action has run in this world.
    Json result;
    try {result=rt.value(page,"(()=>{const state=globalThis."+std::string(state_key)+";return state?state."+name+"("+request.dump()+"):({__suitecutMissingBootstrap:true});})()",deadline,true);}
    catch(const std::exception& error) {
      if(!missing_context(error))throw;
      if(frame)frame->refresh(deadline);else reset_world(page);
      bootstrap(rt,page,deadline,frame);pause(deadline);continue;
    }
    if(!result.is_object()||!result.contains("__suitecutMissingBootstrap"))return result;
    bootstrap(rt,page,deadline,frame);pause(deadline);
  }
}
std::string element_object(CdpRuntime& rt,CdpPage& page,const Json& request,Deadline deadline,CdpFrameScope* frame=nullptr) {
  for(;;) {
    bootstrap(rt,page,deadline,frame);
    Json remote;
    try {
      remote=rt.call(page,"Runtime.evaluate",Json{
        {"expression","(()=>{const state=globalThis."+std::string(state_key)+";return state?state.element("+request.dump()+"):'__suitecutMissingBootstrap';})()"},
        {"contextId",page.isolated_context},{"returnByValue",false},{"objectGroup","suitecut-native-dom-selected"}},deadline);
    }catch(const std::exception& error) {
      // This CDP rejection occurs before expression execution. Never retry an
      // ambiguous context-destroyed failure after a DOM/input action may run.
      if(!missing_context(error))throw;
      if(frame)frame->refresh(deadline);else reset_world(page);
      pause(deadline);continue;
    }
    if(remote.contains("exceptionDetails"))throw std::runtime_error("Native target resolution failed: "+remote.at("exceptionDetails").dump());
    const auto& result=remote.at("result");
    if(result.contains("value")&&result.at("value")==Json("__suitecutMissingBootstrap")){pause(deadline);continue;}
    return result.contains("objectId")?result.at("objectId").as_string():std::string{};
  }
}
Json upload(CdpRuntime& rt,CdpPage& page,const Json& request,Deadline deadline,CdpFrameScope* frame) {
  if(!request.contains("files")||!request.at("files").is_array()||request.at("files").as_array().size()>1000)
    throw std::runtime_error("Native upload requires at most 1000 file paths");
  Json::array files;
  std::size_t directories=0;
  for(const auto& file:request.at("files").as_array()) {
    const auto path=file.is_string()?file.as_string():file.at("path").as_string();
    if(path.empty()||path.size()>4096||path.find('\0')!=std::string::npos)throw std::runtime_error("Invalid native upload path");
    const auto absolute=std::filesystem::absolute(std::filesystem::path(path));
    const bool directory=std::filesystem::is_directory(absolute);
    if(directory)++directories;
    else if(!std::filesystem::is_regular_file(absolute))throw std::runtime_error("Native upload path is not a regular file or directory: "+path);
    if(file.is_object()&&file.contains("directory")&&file.at("directory").as_bool()&&!directory)
      throw std::runtime_error("Native upload directory metadata does not match its path: "+path);
    files.emplace_back(absolute.string());
  }
  if(directories&&(directories!=1||files.size()!=1))throw std::runtime_error("Native directory upload requires exactly one directory path");
  std::string object;
  while((object=element_object(rt,page,request,deadline,frame)).empty())pause(deadline);
  struct Release {
    CdpRuntime& rt;CdpPage page;std::string object;
    ~Release(){try {CdpCleanupScope cleanup;rt.call(page,"Runtime.releaseObject",Json{{"objectId",object}},std::chrono::steady_clock::now()+std::chrono::milliseconds(100));}catch(...){}}
  };
  CdpPage release_page;release_page.id=page.id;release_page.context=page.context;release_page.session=page.session;
  Release release{rt,std::move(release_page),object};
  const auto meta=rt.call(page,"Runtime.callFunctionOn",Json{{"objectId",object},{"functionDeclaration","function(){if(!(this instanceof HTMLInputElement)||this.type!=='file')throw new Error('Native upload target must be input[type=file]');return {multiple:this.multiple,directory:this.hasAttribute('webkitdirectory')};}"},{"returnByValue",true}},deadline);
  if(meta.contains("exceptionDetails"))throw std::runtime_error("Native upload target failed: "+meta.at("exceptionDetails").dump());
  if(files.size()>1&&!meta.at("result").at("value").at("multiple").as_bool())throw std::runtime_error("Native upload target does not accept multiple files");
  const bool directory_input=meta.at("result").at("value").at("directory").as_bool();
  if(directories&&!directory_input)throw std::runtime_error("Native upload target does not accept directories");
  if(!directories&&!files.empty()&&directory_input)throw std::runtime_error("Native directory input requires one directory path");
  if(files.empty()) {
    // CDP treats an empty file chooser selection as cancellation on some Chrome
    // versions. Clear the genuine input with its native value setter instead.
    // These notification events are JavaScript-generated (isTrusted=false).
    const auto cleared=rt.call(page,"Runtime.callFunctionOn",Json{{"objectId",object},{"returnByValue",true},
      {"functionDeclaration",R"JS(function(){
        if(!(this instanceof HTMLInputElement)||this.type!=='file')throw new Error('Native upload target must be input[type=file]');
        const changed=this.files.length!==0||this.value!=='';
        const setter=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')?.set;
        if(typeof setter!=='function')throw new Error('Native file input value setter is unavailable');
        setter.call(this,'');
        if(this.files.length!==0||this.value!=='')throw new Error('Native file input did not clear');
        if(changed){this.dispatchEvent(new Event('input',{bubbles:true,composed:true}));this.dispatchEvent(new Event('change',{bubbles:true}));}
        if(this.files.length!==0||this.value!=='')throw new Error('File input selection changed while notifying clear');
        return true;
      })JS"}},deadline);
    if(cleared.contains("exceptionDetails"))throw std::runtime_error("Native file clear failed: "+cleared.at("exceptionDetails").dump());
    if(!cleared.at("result").at("value").as_bool())throw std::runtime_error("Native file clear was not verified");
    return Json();
  }
  rt.call(page,"DOM.setFileInputFiles",Json{{"objectId",object},{"files",Json(std::move(files))}},deadline);
  return Json();
}
void mouse(CdpFrameScope& scope,const std::string& type,double x,double y,const Json& request,Deadline deadline) {
  Json params{{"kind","mouse"},{"action",type=="mouseMoved"?"move":type=="mousePressed"?"down":"up"},{"x",x},{"y",y}};
  if(type!="mouseMoved") {
    params["button"]=text(request,"button","left");
    params["clickCount"]=request.contains("clickCount")?request.at("clickCount"):Json(1);
  }
  scope.input(params,deadline);
}
bool same_position(const Json& a,const Json& b) {
  return a.contains("identity")&&b.contains("identity")&&a.at("identity")==b.at("identity")&&a.at("box")==b.at("box")&&a.at("x")==b.at("x")&&a.at("y")==b.at("y");
}
std::int64_t drag_sequence(const Json& event) {
  return event.contains("_suitecutEventSequence")?event.at("_suitecutEventSequence").as_int():0;
}
Json drag(CdpRuntime& rt,CdpPage& page,CdpFrameScope& source_scope,const Json& request,Deadline deadline) {
  if(!request.contains("target")||!request.at("target").is_object())throw std::runtime_error("Native drag requires a target locator");
  const auto steps=request.contains("steps")?request.at("steps").as_int():1;
  if(steps<1||steps>10000)throw std::runtime_error("Native drag steps must be 1..10000");
  const bool force=flag(request,"force"),trial=flag(request,"trial");
  Json source=request;source["action"]="click";
  if(request.contains("sourcePosition"))source["position"]=request.at("sourcePosition");
  CdpFrameScope* active_scope=&source_scope;
  const auto wait_ready=[&](const Json& target){
    for(;;){auto state=operation(rt,page,"inspect",target,deadline,active_scope);if(state.at("ready").as_bool())return state;pause(deadline);}
  };
  Json initial,point;
  for(;;) {
    initial=wait_ready(source);
    point=source_scope.map_point(initial.at("x").as_double(),initial.at("y").as_double(),force,deadline);
    if(!point.at("ready").as_bool()){pause(deadline);continue;}
    mouse(source_scope,"mouseMoved",point.at("x").as_double(),point.at("y").as_double(),source,deadline);
    pause(deadline);
    const auto after=wait_ready(source);
    const auto next=source_scope.map_point(after.at("x").as_double(),after.at("y").as_double(),force,deadline);
    if(!next.at("ready").as_bool()||(!force&&(!same_position(initial,after)||point.at("x")!=next.at("x")||point.at("y")!=next.at("y"))))continue;
    point=next;break;
  }
  struct Restore {
    CdpFrameScope& scope;bool held=false,intercept=false;double x=0,y=0;
    ~Restore(){CdpCleanupScope cleanup;const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
      if(held)try{scope.input(Json{{"kind","mouse"},{"action","up"},{"x",x},{"y",y},{"button","left"}},end);}catch(...){}
      if(intercept)try{scope.call_root("Input.setInterceptDrags",Json{{"enabled",false}},end);}catch(...){}
    }
  } restore{source_scope};
  if(page.mouse_buttons&1)throw std::runtime_error("Native drag cannot start with the left mouse button already held");
  const auto root_session=source_scope.root_session();
  std::int64_t baseline=0;
  const auto baseline_rows=rt.browser_events("Input.dragIntercepted");
  for(const auto& event:baseline_rows.as_array())if(text(event,"sessionId")==root_session)baseline=std::max(baseline,drag_sequence(event));
  const auto fresh_data=[&](){Json data;std::int64_t latest=baseline;
    const auto fresh_rows=rt.browser_events("Input.dragIntercepted",baseline);
    for(const auto& event:fresh_rows.as_array())if(text(event,"sessionId")==root_session&&drag_sequence(event)>latest){latest=drag_sequence(event);data=event.at("params").at("data");}
    return data;
  };
  if(!trial){
    operation(rt,page,"markDragSource",source,deadline,&source_scope);
    source_scope.call_root("Input.setInterceptDrags",Json{{"enabled",true}},deadline);restore.intercept=true;
    restore.x=point.at("x").as_double();restore.y=point.at("y").as_double();restore.held=true;
    mouse(source_scope,"mousePressed",restore.x,restore.y,source,deadline);
  }
  source_scope.restore_root();
  Json target=request.at("target");target["action"]="hover";target["force"]=force;target["dragging"]=true;
  if(request.contains("targetPosition"))target["position"]=request.at("targetPosition");
  auto target_scope=cdp_resolve_frame(rt,page,target,deadline);active_scope=target_scope.get();bootstrap(rt,page,deadline,active_scope);
  Json destination,target_point;
  for(;;){destination=wait_ready(target);target_point=target_scope->map_point(destination.at("x").as_double(),destination.at("y").as_double(),force,deadline);if(target_point.at("ready").as_bool())break;pause(deadline);}
  if(trial)return Json{{"mode","trial"},{"intercepted",false},{"steps",steps}};
  const auto end_x=target_point.at("x").as_double(),end_y=target_point.at("y").as_double();
  const auto start_x=restore.x,start_y=restore.y;
  Json data;bool entered=false;
  const auto dispatch_drag=[&](const char* type,double x,double y){source_scope.call_root("Input.dispatchDragEvent",Json{{"type",type},{"x",x},{"y",y},{"data",data},{"modifiers",page.modifiers}},deadline);};
  for(std::int64_t step=1;step<=steps;++step) {
    restore.x=start_x+(end_x-start_x)*step/steps;restore.y=start_y+(end_y-start_y)*step/steps;
    if(data.is_null()) {
      mouse(source_scope,"mouseMoved",restore.x,restore.y,request,deadline);
      data=fresh_data();
    }
    if(!data.is_null()){if(!entered){dispatch_drag("dragEnter",restore.x,restore.y);entered=true;}dispatch_drag("dragOver",restore.x,restore.y);}
  }
  // Interception can arrive just after the final move's acknowledgement.
  pause(deadline);if(data.is_null())data=fresh_data();
  for(;;) {
    const auto after=wait_ready(target);
    const auto mapped=target_scope->map_point(after.at("x").as_double(),after.at("y").as_double(),force,deadline);
    if(!mapped.at("ready").as_bool()){pause(deadline);continue;}
    restore.x=mapped.at("x").as_double();restore.y=mapped.at("y").as_double();
    if(data.is_null())mouse(source_scope,"mouseMoved",restore.x,restore.y,request,deadline);
    else {if(!entered){dispatch_drag("dragEnter",restore.x,restore.y);entered=true;}dispatch_drag("dragOver",restore.x,restore.y);}
    pause(deadline);
    const auto stable=wait_ready(target);const auto final=target_scope->map_point(stable.at("x").as_double(),stable.at("y").as_double(),force,deadline);
    if(!final.at("ready").as_bool())continue;
    if(force||(same_position(after,stable)&&mapped.at("x")==final.at("x")&&mapped.at("y")==final.at("y")))break;
  }
  if(data.is_null())data=fresh_data();
  if(!data.is_null()) {if(!entered){dispatch_drag("dragEnter",restore.x,restore.y);dispatch_drag("dragOver",restore.x,restore.y);}dispatch_drag("drop",restore.x,restore.y);}
  mouse(source_scope,"mouseReleased",restore.x,restore.y,request,deadline);restore.held=false;
  source_scope.call_root("Input.setInterceptDrags",Json{{"enabled",false}},deadline);restore.intercept=false;
  std::int64_t intercepted_count=0;
  const auto intercepted_rows=rt.browser_events("Input.dragIntercepted",baseline);
  for(const auto& event:intercepted_rows.as_array())if(text(event,"sessionId")==root_session)++intercepted_count;
  return Json{{"mode",data.is_null()?"pointer":"html5"},{"intercepted",!data.is_null()},{"interceptedEventCount",intercepted_count},
    {"dragData",data},{"sourcePoint",Json{{"x",start_x},{"y",start_y}}},{"targetPoint",Json{{"x",restore.x},{"y",restore.y}}},{"steps",steps}};
}

}
std::unique_ptr<CdpFrameScope> cdp_resolve_frame(CdpRuntime& rt,CdpPage& page,const Json& request,Deadline deadline) {
  auto scope=std::make_unique<CdpFrameScope>(rt,page,deadline);
  if(request.contains("frame")&&!request.at("frame").is_null())scope->enter_frame(request.at("frame").as_string(),deadline);
  if(request.contains("framePath")) {
    const auto& path=request.at("framePath");
    if(!path.is_array()||path.as_array().size()>64)throw std::runtime_error("Invalid native frame path");
    for(const auto& segment:path.as_array()) {
      if(!segment.is_array())throw std::runtime_error("Native frame path segments must be locator query arrays");
      Json owner{{"query",segment},{"strict",true}};
      std::string object;
      while((object=element_object(rt,page,owner,deadline,scope.get())).empty())pause(deadline);
      scope->enter_owner(object,deadline);
    }
  }
  return scope;
}
Json cdp_dom_command(CdpRuntime& rt,CdpPage& page,const Json& request,Deadline deadline) {
  std::size_t wire_work=0;validate_wire(request,wire_work);
  if(request.dump().size()>1024*1024)throw std::runtime_error("Native DOM request exceeds 1 MiB");
  auto frame_scope=cdp_resolve_frame(rt,page,request,deadline);
  bootstrap(rt,page,deadline,frame_scope.get());
  const auto family=text(request,"command");
  const auto action=text(request,"action");
  if((family=="form"||family=="locator")&&action=="dragTo")return drag(rt,page,*frame_scope,request,deadline);
  if(family=="waitFor") {
    const auto state=text(request,"state","visible");
    if(state!="attached"&&state!="detached"&&state!="visible"&&state!="hidden")throw std::runtime_error("Invalid native locator wait state");
    Json wait=request;wait["state"]=state;
    while(!operation(rt,page,"elementState",wait,deadline,frame_scope.get()).as_bool())pause(deadline);
    return Json();
  }
  if(family=="form"&&action=="setInputFiles")return upload(rt,page,request,deadline,frame_scope.get());
  if(family=="queryEvaluate"||family=="snapshotReferences") {
    for(;;) {
      auto result=operation(rt,page,family=="queryEvaluate"?"read":"observe",request,deadline,frame_scope.get());
      if(result.at("ready").as_bool()) {
        Json value=result.contains("value")?result.at("value"):Json();
        if(family=="snapshotReferences")return value;
        return Json{{"result",Json{{"type",value.is_null()?"object":value.is_string()?"string":value.is_bool()?"boolean":value.is_number()?"number":"object"},{"value",std::move(value)}}}};
      }
      pause(deadline);
    }
  }
  if(family!="locator"&&family!="form")throw std::runtime_error("Unsupported native Chrome DOM command: "+family);
  const bool pointer=action=="click"||action=="tap"||action=="hover"||action=="setChecked";
  if(!pointer&&action!="fill"&&action!="clear"&&action!="type"&&action!="press"&&action!="focus"&&action!="scrollIntoViewIfNeeded"&&action!="inputValue"&&action!="selectOption")throw std::runtime_error("Unsupported native Chrome DOM action: "+action);
  ScopedModifiers modifiers(rt,page,*frame_scope,request,deadline,pointer);
  std::string last_reason="element to be ready";
  for(;;) {
    if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Native DOM operation timed out waiting for "+last_reason);
    auto state=operation(rt,page,"inspect",request,deadline,frame_scope.get());
    if(!state.at("ready").as_bool()){last_reason=text(state,"reason","element to be ready");pause(deadline);continue;}
    if(flag(state,"done"))return state.contains("value")?state.at("value"):Json();
    if(action=="inputValue")return state.at("value");
    if(action=="focus"||action=="scrollIntoViewIfNeeded")return Json();
    if(action=="fill"||action=="clear"||action=="type") {
      const auto value=action=="clear"?std::string():text(request,"value");
      if(value.size()>65536)throw std::runtime_error("Native input text exceeds 64 KiB");
      if(action=="type")return frame_scope->input(Json{{"kind","keyboard"},{"action","type"},{"text",value},{"delay",request.contains("delay")?request.at("delay"):Json(0)}},deadline);
      // Selection is prepared on the resolved element in the isolated world;
      // Chromium's editing pipeline receives actual Unicode fill text via CDP.
      if(value.empty()) {
        frame_scope->input(Json{{"kind","keyboard"},{"action","press"},{"key","Backspace"}},deadline);
      } else frame_scope->insert_text(value,deadline);
      return Json();
    }
    if(action=="press")return frame_scope->input(Json{{"kind","keyboard"},{"action","press"},{"key",text(request,"key")},{"delay",request.contains("delay")?request.at("delay"):Json(0)}},deadline);
    const auto top=frame_scope->map_point(state.at("x").as_double(),state.at("y").as_double(),flag(request,"force"),deadline);
    if(!top.at("ready").as_bool()){last_reason="iframe owners to receive pointer events";pause(deadline);continue;}
    const double x=top.at("x").as_double(),y=top.at("y").as_double();
    mouse(*frame_scope,"mouseMoved",x,y,request,deadline);
    // Hover can alter layout, replace the target, or install an overlay. Resolve
    // again after input and require two equal geometry observations before down.
    pause(deadline);
    auto after=operation(rt,page,"inspect",request,deadline,frame_scope.get());
    if(!after.at("ready").as_bool()){last_reason=text(after,"reason","element to receive pointer events");continue;}
    if(flag(after,"done"))return after.contains("value")?after.at("value"):Json();
    if(!flag(request,"force")&&!same_position(state,after)){last_reason="element position to be stable after hover";continue;}
    const auto final_top=frame_scope->map_point(after.at("x").as_double(),after.at("y").as_double(),flag(request,"force"),deadline);
    if(!final_top.at("ready").as_bool()){last_reason="iframe owners to receive pointer events after hover";continue;}
    if(!flag(request,"force")&&(top.at("x")!=final_top.at("x")||top.at("y")!=final_top.at("y"))){last_reason="iframe owners position to be stable";continue;}
    if(action=="hover"||flag(request,"trial"))return Json();
    try {
      mouse(*frame_scope,"mousePressed",final_top.at("x").as_double(),final_top.at("y").as_double(),request,deadline);
      mouse(*frame_scope,"mouseReleased",final_top.at("x").as_double(),final_top.at("y").as_double(),request,deadline);
    } catch(...) {
      CdpCleanupScope cleanup_scope;
      try {mouse(*frame_scope,"mouseReleased",final_top.at("x").as_double(),final_top.at("y").as_double(),request,
                 std::chrono::steady_clock::now()+std::chrono::milliseconds(100));}catch(...){}
      throw;
    }
    if(action=="setChecked"&&!operation(rt,page,"verifyChecked",request,deadline,frame_scope.get()).as_bool())throw std::runtime_error("Click did not change checkbox to requested state");
    return Json();
  }
}
} // namespace suitecut::detail
