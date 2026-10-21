// SPDX-License-Identifier: MIT
#include "bidi_dom.hpp"
#include "dom_driver.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>
namespace suitecut::detail {
namespace {
std::string text(const Json& value,const char* key,const char* fallback="") {
  return value.contains(key)?value.at(key).as_string():std::string(fallback);
}
bool flag(const Json& value,const char* key,bool fallback=false) {
  return value.contains(key)?value.at(key).as_bool():fallback;
}
void pause(Deadline deadline) {
  bidi_check_active(deadline);
  std::this_thread::sleep_until(std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(20)));
  bidi_check_active(deadline);
}
void validate_wire(const Json& value,std::size_t& work,int depth=0) {
  if(++work>100000||depth>64)throw std::runtime_error("Native BiDi DOM request traversal limit exceeded");
  if(value.is_string()&&value.as_string().size()>65536)throw std::runtime_error("Native BiDi DOM request string exceeds 64 KiB");
  if(value.is_array())for(const auto& child:value.as_array())validate_wire(child,work,depth+1);
  if(value.is_object())for(const auto& [key,child]:value.as_object()){(void)key;validate_wire(child,work,depth+1);}
}
std::string state_name(){return std::string("globalThis.")+kSuiteCutDOMDriverStateKey;}
void bootstrap(BidiRuntime& rt,BidiPage& page,Deadline deadline) {
  if(rt.value(page,"!!"+state_name(),deadline,true).as_bool())return;
  rt.value(page,SuiteCutDOMDriverSource(),deadline,true);
}
Json operation(BidiRuntime& rt,BidiPage& page,const char* name,const Json& request,Deadline deadline) {
  for(;;) {
    const auto result=rt.value(page,"(()=>{const state="+state_name()+";return state?state."+name+"("+request.dump()+"):({__suitecutMissingBootstrap:true});})()",deadline,true);
    if(!result.is_object()||!result.contains("__suitecutMissingBootstrap"))return result;
    bootstrap(rt,page,deadline);pause(deadline);
  }
}
Json script_target(BidiPage& page) {
  // The runtime owns sandbox naming; ensure its isolated target has been used
  // before passing the same name to a raw script call.
  Json target{{"context",page.browsing_context}};
  if(!page.sandbox.empty())target["sandbox"]=page.sandbox;
  return target;
}
const Json& remote_result(const Json& result) {
  if(text(result,"type")=="exception"||result.contains("exceptionDetails"))
    throw std::runtime_error("Native BiDi DOM evaluation failed: "+result.dump());
  if(!result.contains("result"))throw std::runtime_error("Native BiDi script result is unavailable");
  return result.at("result");
}
Json raw_script(BidiRuntime& rt,BidiPage& page,const std::string& expression,Deadline deadline) {
  bootstrap(rt,page,deadline);
  const auto result=rt.call("script.evaluate",Json{{"expression",expression},{"target",script_target(page)},
    {"awaitPromise",true},{"resultOwnership","none"},{"serializationOptions",Json{{"maxObjectDepth",64},{"maxDomDepth",0}}}},deadline);
  return remote_result(result);
}
std::string child_context(BidiRuntime& rt,BidiPage& page,const Json& owner,Deadline deadline) {
  for(;;) {
    const auto remote=raw_script(rt,page,"(()=>{const state="+state_name()+";if(!state)return '__suitecutMissingBootstrap';const e=state.element("+owner.dump()+");if(!e)return null;if(!['iframe','frame'].includes(e.localName))throw new Error('Native frame locator target is not an iframe or frame');return e.contentWindow;})()",deadline);
    if(text(remote,"type")=="string"&&remote.contains("value")&&remote.at("value")==Json("__suitecutMissingBootstrap")){pause(deadline);continue;}
    if(text(remote,"type")=="null"||text(remote,"type")=="undefined"){pause(deadline);continue;}
    if(text(remote,"type")!="window"||!remote.contains("value")||!remote.at("value").contains("context"))throw std::runtime_error("Native BiDi frame WindowProxy has no browsing context identity");
    return remote.at("value").at("context").as_string();
  }
}
std::string node_shared_id(BidiRuntime& rt,BidiPage& page,const Json& request,Deadline deadline) {
  for(;;) {
    const auto remote=raw_script(rt,page,"(()=>{const state="+state_name()+";return state?state.element("+request.dump()+"):'__suitecutMissingBootstrap';})()",deadline);
    if(text(remote,"type")=="string"&&remote.contains("value")&&remote.at("value")==Json("__suitecutMissingBootstrap")){pause(deadline);continue;}
    if(text(remote,"type")=="null"||text(remote,"type")=="undefined"){pause(deadline);continue;}
    if(text(remote,"type")!="node"||!remote.contains("sharedId"))throw std::runtime_error("Native BiDi target has no genuine node sharedId");
    return remote.at("sharedId").as_string();
  }
}
Json runtime_envelope(Json value) {
  const auto type=value.is_null()?"object":value.is_string()?"string":value.is_bool()?"boolean":value.is_number()?"number":"object";
  return Json{{"result",Json{{"type",type},{"value",std::move(value)}}}};
}
bool same_position(const Json& a,const Json& b) {
  return a.contains("identity")&&b.contains("identity")&&a.at("identity")==b.at("identity")&&a.at("box")==b.at("box")&&a.at("x")==b.at("x")&&a.at("y")==b.at("y");
}
void mouse(BidiFrameScope& scope,const char* action,double x,double y,const Json& options,Deadline deadline) {
  Json request{{"kind","mouse"},{"action",action},{"x",x},{"y",y}};
  if(std::string(action)!="move") {
    request["button"]=text(options,"button","left");
    request["clickCount"]=options.contains("clickCount")?options.at("clickCount"):Json(1);
  }
  scope.input(request,deadline);
}
struct ScopedModifiers {
  BidiFrameScope& scope;BidiPage& page;std::vector<std::string> added;
  ScopedModifiers(BidiFrameScope& owner,BidiPage& target,const Json& options,Deadline deadline,bool pointer):scope(owner),page(target) {
    if(!pointer||!options.contains("modifiers"))return;
    if(!options.at("modifiers").is_array())throw std::runtime_error("Native BiDi pointer modifiers must be an array");
    std::vector<std::pair<std::string,int>> desired;
    for(const auto& value:options.at("modifiers").as_array()) {
      const auto key=value.as_string();const int bit=key=="Alt"?1:key=="Control"||key=="ControlOrMeta"?2:key=="Meta"?4:key=="Shift"?8:0;
      if(!bit)throw std::runtime_error("Unsupported native pointer modifier: "+key);
      desired.emplace_back(key=="ControlOrMeta"?"Control":key,bit);
    }
    try {for(const auto& [key,bit]:desired)if(!(page.modifiers&bit)){scope.input(Json{{"kind","keyboard"},{"action","down"},{"key",key}},deadline);added.push_back(key);}}
    catch(...){release();throw;}
  }
  void release() noexcept {
    if(added.empty())return;
    BidiCleanupScope cleanup;const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
    for(auto it=added.rbegin();it!=added.rend();++it)try{scope.input(Json{{"kind","keyboard"},{"action","up"},{"key",*it}},deadline);}catch(...){}
    added.clear();
  }
  ~ScopedModifiers(){release();}
};
bool find_context_path(const Json& tree,const std::string& context,std::vector<std::string>& path) {
  path.push_back(tree.at("context").as_string());
  if(path.back()==context)return true;
  if(tree.contains("children")&&tree.at("children").is_array())for(const auto& child:tree.at("children").as_array())if(find_context_path(child,context,path))return true;
  path.pop_back();return false;
}
}
struct BidiFrameScope::Impl {
  BidiRuntime& rt;BidiPage& page;std::string root,input_context;
  struct Ancestor{BidiPage parent;Json owner;std::string child;};
  std::vector<Ancestor> ancestors;
  Impl(BidiRuntime& runtime,BidiPage& target):rt(runtime),page(target),root(target.browsing_context),input_context(target.browsing_context){}
};
BidiFrameScope::BidiFrameScope(BidiRuntime& rt,BidiPage& page):impl_(std::make_unique<Impl>(rt,page)){}
BidiFrameScope::~BidiFrameScope(){restore_root();}
void BidiFrameScope::restore_root(){impl_->page.browsing_context=impl_->root;}
void BidiFrameScope::enter_owner(Json owner,Deadline deadline) {
  auto& p=*impl_;if(p.ancestors.size()>=64)throw std::runtime_error("Native BiDi frame path depth exceeds 64");
  const auto child=child_context(p.rt,p.page,owner,deadline);
  auto parent=p.page;p.ancestors.push_back({std::move(parent),std::move(owner),child});
  p.page.browsing_context=child;p.input_context=child;bootstrap(p.rt,p.page,deadline);
}
void BidiFrameScope::enter_frame(const std::string& context,Deadline deadline) {
  auto& p=*impl_;if(context.empty()||context==p.page.browsing_context)return;
  const auto tree=p.rt.call("browsingContext.getTree",Json{{"root",p.root},{"maxDepth",64}},deadline);
  std::vector<std::string> path;
  const auto& contexts=tree.at("contexts").as_array();
  if(contexts.empty()||!find_context_path(contexts.front(),context,path))throw std::runtime_error("Native BiDi frame identity is detached or unavailable");
  for(std::size_t depth=1;depth<path.size();++depth) {
    Json query{{"query",Json::array{Json{{"engine","css"},{"selector","iframe,frame"}}}},{"strict",true}};
    const auto windows=raw_script(p.rt,p.page,"(()=>{const state="+state_name()+";return state.all("+query.dump()+").map(e=>e.contentWindow);})()",deadline);
    if(text(windows,"type")!="array"||!windows.contains("value"))throw std::runtime_error("Native BiDi frame owners are unavailable");
    const auto& values=windows.at("value").as_array();if(values.size()>4096)throw std::runtime_error("Native BiDi frame owner limit exceeded");
    bool found=false;
    for(std::size_t index=0;index<values.size();++index)if(text(values[index],"type")=="window"&&values[index].contains("value")&&values[index].at("value").at("context")==Json(path[depth])) {
      query["query"].as_array().push_back(Json{{"engine","nth"},{"index",index}});enter_owner(query,deadline);found=true;break;
    }
    if(!found)throw std::runtime_error("Native BiDi frame has no current DOM owner");
  }
}
bool BidiFrameScope::ancestors_visible(Deadline deadline) {
  auto& p=*impl_;
  for(auto& ancestor:p.ancestors) {
    const auto visible=p.rt.value(ancestor.parent,"(()=>{const e="+state_name()+"?.element("+ancestor.owner.dump()+");if(!e||!e.isConnected)return false;const r=e.getBoundingClientRect(),s=getComputedStyle(e);return r.width>0&&r.height>0&&s.display!=='none'&&s.visibility!=='hidden'&&s.visibility!=='collapse';})()",deadline,true);
    if(!visible.as_bool())return false;
  }
  return true;
}
Json BidiFrameScope::map_point(double x,double y,bool force,Deadline deadline) {
  auto& p=*impl_;
  for(auto it=p.ancestors.rbegin();it!=p.ancestors.rend();++it) {
    if(child_context(p.rt,it->parent,it->owner,deadline)!=it->child)return Json{{"ready",false}};
    const auto point=p.rt.value(it->parent,"(()=>{const e="+state_name()+".element("+it->owner.dump()+");if(!e||!e.isConnected)return {ready:false};e.scrollIntoView({block:'center',inline:'center',behavior:'instant'});const r=e.getBoundingClientRect(),s=getComputedStyle(e);let depth=0;for(let n=e;n;n=n.parentElement||n.getRootNode().host||null){if(++depth>64)throw new Error('Native iframe transform ancestor depth limit exceeded');const style=getComputedStyle(n),m=style.transform==='none'?null:new DOMMatrixReadOnly(style.transform);if(style.perspective!=='none'||(m&&(!m.is2D||Math.abs(m.b)>1e-8||Math.abs(m.c)>1e-8||m.a<=0||m.d<=0)))throw new Error('Native pointer targeting does not support rotated, skewed or perspective iframe owners');}const sx=e.offsetWidth?r.width/e.offsetWidth:0,sy=e.offsetHeight?r.height/e.offsetHeight:0;if(!sx||!sy||s.visibility==='hidden'||s.display==='none')return {ready:false};const x="+Json(x).dump()+",y="+Json(y).dump()+";if(x<0||y<0||x>=e.clientWidth||y>=e.clientHeight)return {ready:false};const px=r.left+(e.clientLeft+x)*sx,py=r.top+(e.clientTop+y)*sy;if(px<0||py<0||px>=innerWidth||py>=innerHeight)return {ready:false};if(!"+std::string(force?"true":"false")+"){let hit=document.elementFromPoint(px,py);for(let depth=0;hit?.shadowRoot&&depth<64;depth++){const next=hit.shadowRoot.elementFromPoint(px,py);if(!next||next===hit)break;hit=next;}let receives=false;for(let n=hit;n;n=n.assignedSlot||n.parentElement||n.getRootNode().host||null)if(n===e){receives=true;break;}if(!receives)return {ready:false};}return {ready:true,x:px,y:py};})()",deadline,true);
    if(!point.at("ready").as_bool())return point;
    x=point.at("x").as_double();y=point.at("y").as_double();
  }
  return Json{{"ready",true},{"x",x},{"y",y}};
}
Json BidiFrameScope::input(const Json& request,Deadline deadline) {
  auto& p=*impl_;const auto selected=p.page.browsing_context;
  Json global_point;
  if(text(request,"kind")=="mouse"&&request.contains("x")&&request.contains("y"))
    global_point=map_point(request.at("x").as_double(),request.at("y").as_double(),true,deadline);
  // Firefox's synchronous BiDi input actor targets the selected context window:
  // key dispatch and viewport pointer positions are local to that context.
  p.page.browsing_context=p.input_context;
  try{
    auto result=bidi_input_command(p.rt,p.page,request,deadline);
    if(global_point.is_object()&&global_point.at("ready").as_bool()){p.page.mouse_x=global_point.at("x").as_double();p.page.mouse_y=global_point.at("y").as_double();}
    p.page.browsing_context=selected;return result;
  }catch(...){p.page.browsing_context=selected;throw;}
}
std::unique_ptr<BidiFrameScope> bidi_resolve_frame(BidiRuntime& rt,BidiPage& page,const Json& request,Deadline deadline) {
  auto scope=std::make_unique<BidiFrameScope>(rt,page);
  if(request.contains("frame")&&!request.at("frame").is_null())scope->enter_frame(request.at("frame").as_string(),deadline);
  if(request.contains("framePath")) {
    const auto& path=request.at("framePath");
    if(!path.is_array()||path.as_array().size()>64)throw std::runtime_error("Invalid native BiDi frame path");
    for(const auto& query:path.as_array()){if(!query.is_array())throw std::runtime_error("Native BiDi frame path segments must be locator query arrays");scope->enter_owner(Json{{"query",query},{"strict",true}},deadline);}
  }
  return scope;
}
namespace {
Json wait_load(BidiRuntime& rt,BidiPage& page,const Json& request,Deadline deadline) {
  const auto wanted=text(request,"waitUntil","load");
  if(wanted=="commit")return Json();
  if(wanted!="load"&&wanted!="domcontentloaded")throw std::runtime_error("Unsupported native BiDi load state: "+wanted);
  const auto field=wanted=="load"?"loadEventEnd":"domContentLoadedEventEnd";
  const auto fallback=wanted=="load"?"document.readyState==='complete'":"document.readyState!=='loading'";
  const std::string expression="(()=>{const n=performance.getEntriesByType('navigation')[0];return n?n."+std::string(field)+">0:"+fallback+";})()";
  while(!rt.value(page,expression,deadline,true).as_bool())pause(deadline);
  return Json();
}
Json page_wait(BidiRuntime& rt,BidiPage& page,const Json& request,Deadline deadline) {
  const auto family=text(request,"command");
  if(family=="waitLoadState")return wait_load(rt,page,request,deadline);
  if(family=="waitForSelector"||family=="waitForText") {
    Json wait=request;wait["state"]=text(request,"state","visible");
    const auto state=wait.at("state").as_string();
    if(state!="visible"&&state!="hidden"&&state!="attached"&&state!="detached")throw std::runtime_error("Invalid native selector wait state");
    if(!wait.contains("strict"))wait["strict"]=false;
    if(!wait.contains("query"))wait["query"]=family=="waitForSelector"?Json(Json::array{Json{{"engine","css"},{"selector",request.at("selector")}}}):Json(Json::array{Json{{"engine","text"},{"text",request.at("text")},{"exact",flag(request,"exact")}}});
    while(!operation(rt,page,"elementState",wait,deadline).as_bool())pause(deadline);
    return Json();
  }
  std::string expression;
  if(family=="waitForFunction")expression="(async()=>{const f=("+request.at("expression").as_string()+");const v=await(typeof f==='function'?f():f);return v?{ready:true,value:v}:{ready:false};})()";
  else if(family=="waitForURL") {
    const auto match=text(request,"match","exact"),wanted=Json(request.at("url").as_string()).dump();
    if(match=="exact")expression="location.href==="+wanted;
    else if(match=="regex")expression="new RegExp("+wanted+","+Json(text(request,"flags")).dump()+").test(location.href)";
    else if(match=="glob")expression="(()=>{const g="+wanted+";let p='^';for(let i=0;i<g.length;i++){const c=g[i];if(c==='*'){if(g[i+1]==='*'){p+='.*';i++;}else p+='[^/]*';}else if(c==='?')p+='.';else p+="+Json("\\^$.*+?()[]{}|").dump()+".includes(c)?"+Json("\\").dump()+"+c:c;}return new RegExp(p+'$').test(location.href);})()";
    else throw std::runtime_error("Native URL match must be exact, glob or regex");
  }else throw std::runtime_error("Unsupported native BiDi page wait: "+family);
  for(;;) {
    const auto value=rt.value(page,expression,deadline,family!="waitForFunction");
    if(family=="waitForFunction") {if(value.is_object()&&flag(value,"ready"))return value.contains("value")?value.at("value"):Json();}
    else if(value.is_bool()&&value.as_bool()){wait_load(rt,page,request,deadline);return Json();}
    pause(deadline);
  }
}

Json upload(BidiRuntime& rt,BidiPage& page,const Json& request,Deadline deadline) {
  if(!request.contains("files")||!request.at("files").is_array()||request.at("files").as_array().size()>1000)throw std::runtime_error("Native BiDi upload requires at most 1000 file paths");
  Json::array files;std::size_t directories=0;
  for(const auto& entry:request.at("files").as_array()) {
    const auto path=entry.is_string()?entry.as_string():entry.at("path").as_string();
    if(path.empty()||path.size()>4096||path.find('\0')!=std::string::npos)throw std::runtime_error("Invalid native upload path");
    const auto absolute=std::filesystem::absolute(std::filesystem::path(path));
    const bool directory=std::filesystem::is_directory(absolute);
    if(directory)++directories;else if(!std::filesystem::is_regular_file(absolute))throw std::runtime_error("Native upload path is not a regular file or directory: "+path);
    if(entry.is_object()&&flag(entry,"directory")&&!directory)throw std::runtime_error("Native upload directory metadata does not match its path");
    files.emplace_back(absolute.string());
  }
  if(directories&&(directories!=1||files.size()!=1))throw std::runtime_error("Native directory upload requires exactly one directory path");
  const auto shared_id=node_shared_id(rt,page,request,deadline);
  const auto metadata=rt.value(page,"(()=>{const e="+state_name()+".element("+request.dump()+");if(!(e instanceof HTMLInputElement)||e.type!=='file')throw new Error('Native upload target must be input[type=file]');return {multiple:e.multiple,directory:e.hasAttribute('webkitdirectory')};})()",deadline,true);
  if(files.size()>1&&!metadata.at("multiple").as_bool())throw std::runtime_error("Native upload target does not accept multiple files");
  if(directories&&!metadata.at("directory").as_bool())throw std::runtime_error("Native upload target does not accept directories");
  if(!directories&&!files.empty()&&metadata.at("directory").as_bool())throw std::runtime_error("Native directory input requires one directory path");
  // sharedId is returned by Firefox for this actual selected DOM node. No
  // FileList, names or file contents are manufactured in JavaScript.
  rt.call("input.setFiles",Json{{"context",page.browsing_context},{"element",Json{{"sharedId",shared_id}}},{"files",Json(files)}},deadline);
  if(files.empty()) {
    const auto clear=rt.value(page,"(()=>{const e="+state_name()+".element("+request.dump()+");if(!(e instanceof HTMLInputElement)||e.type!=='file')throw new Error('Native file input is unavailable');if(e.files.length===0&&e.value==='')return {cleared:true,events:'native'};const setter=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')?.set;if(typeof setter!=='function')throw new Error('Native file value setter is unavailable');setter.call(e,'');if(e.files.length!==0||e.value!=='')throw new Error('Native file input did not clear');e.dispatchEvent(new Event('input',{bubbles:true,composed:true}));e.dispatchEvent(new Event('change',{bubbles:true}));if(e.files.length!==0||e.value!=='')throw new Error('File selection changed while notifying clear');return {cleared:true,events:'javascript'};})()",deadline,true);
    return clear;
  }
  return Json();
}
Json drag(BidiRuntime& rt,BidiPage& page,BidiFrameScope& source_scope,const Json& request,Deadline deadline) {
  if(!request.contains("target")||!request.at("target").is_object())throw std::runtime_error("Native BiDi drag requires a target locator");
  const auto steps=request.contains("steps")?request.at("steps").as_int():1;
  if(steps<1||steps>10000)throw std::runtime_error("Native drag steps must be 1..10000");
  const bool force=flag(request,"force"),trial=flag(request,"trial");
  Json source=request;source["action"]="click";
  if(request.contains("sourcePosition"))source["position"]=request.at("sourcePosition");
  const auto wait_ready=[&](const Json& target){for(;;){auto value=operation(rt,page,"inspect",target,deadline);if(value.at("ready").as_bool())return value;pause(deadline);}};
  Json source_point,source_local;
  for(;;) {
    const auto before=wait_ready(source);
    source_point=source_scope.map_point(before.at("x").as_double(),before.at("y").as_double(),force,deadline);
    if(!source_point.at("ready").as_bool()){pause(deadline);continue;}
    mouse(source_scope,"move",before.at("x").as_double(),before.at("y").as_double(),source,deadline);pause(deadline);
    const auto after=wait_ready(source);
    const auto mapped=source_scope.map_point(after.at("x").as_double(),after.at("y").as_double(),force,deadline);
    if(mapped.at("ready").as_bool()&&(force||(same_position(before,after)&&source_point.at("x")==mapped.at("x")&&source_point.at("y")==mapped.at("y")))){source_point=mapped;source_local=after;break;}
  }
  if(page.mouse_buttons&1)throw std::runtime_error("Native drag cannot start with the left mouse button already held");
  const auto source_context=page.browsing_context;
  const bool html5_source=rt.value(page,"(()=>{let e="+state_name()+".element("+source.dump()+");for(let depth=0;e&&depth<64;depth++,e=e.parentElement||e.getRootNode().host||null)if(e.draggable)return true;return false;})()",deadline,true).as_bool();
  if(html5_source)throw std::runtime_error("Native Firefox HTML5 drag completion is unavailable: this engine's native drag-service transfer/drop has not been verified");
  source_scope.restore_root();
  Json target=request.at("target");target["action"]="hover";target["force"]=force;target["dragging"]=true;
  if(request.contains("targetPosition"))target["position"]=request.at("targetPosition");
  auto target_scope=bidi_resolve_frame(rt,page,target,deadline);bootstrap(rt,page,deadline);
  const bool cross_context=page.browsing_context!=source_context;
  if(cross_context)throw std::runtime_error("Native Firefox cross-context drag cannot preserve Gecko document capture; source and target must share a browsing context");
  Json target_point,target_local;
  for(;;){const auto state=wait_ready(target);target_point=target_scope->map_point(state.at("x").as_double(),state.at("y").as_double(),force,deadline);if(target_point.at("ready").as_bool()){target_local=state;break;}pause(deadline);}
  // Resolving the destination may scroll an ancestor shared by the source.
  // Revalidate both before pressing; no input is retried after mutation.
  for(;;) {
    const auto selected=page.browsing_context;
    Json current,current_point;
    page.browsing_context=source_context;
    try{current=wait_ready(source);current_point=source_scope.map_point(current.at("x").as_double(),current.at("y").as_double(),force,deadline);page.browsing_context=selected;}
    catch(...){page.browsing_context=selected;throw;}
    if(!current_point.at("ready").as_bool()){pause(deadline);continue;}
    const auto destination=wait_ready(target);
    const auto destination_point=target_scope->map_point(destination.at("x").as_double(),destination.at("y").as_double(),force,deadline);
    if(!destination_point.at("ready").as_bool()){pause(deadline);continue;}
    if(force||(same_position(source_local,current)&&source_point.at("x")==current_point.at("x")&&source_point.at("y")==current_point.at("y")&&same_position(target_local,destination)&&target_point.at("x")==destination_point.at("x")&&target_point.at("y")==destination_point.at("y"))) {
      source_local=current;source_point=current_point;target_local=destination;target_point=destination_point;break;
    }
    source_local=current;source_point=current_point;target_local=destination;target_point=destination_point;pause(deadline);
  }
  if(trial)return Json{{"mode","trial"},{"crossContext",cross_context},{"steps",steps}};
  // Keep cleanup in the actual source actor, including after cancellation.
  // Native document capture and app pointer/legacy capture remain untouched.
  struct Release {
    BidiFrameScope& scope;bool held=false;double x=0,y=0;
    void send(const char* action,double px,double py,Deadline until){x=px;y=py;mouse(scope,action,x,y,Json{{"button","left"}},until);}
    ~Release(){if(held)try{BidiCleanupScope cleanup;send("up",x,y,std::chrono::steady_clock::now()+std::chrono::milliseconds(100));}catch(...){}}
  } release{source_scope};
  const auto start_x=source_local.at("x").as_double(),start_y=source_local.at("y").as_double();
  const auto end_x=target_local.at("x").as_double(),end_y=target_local.at("y").as_double();
  const auto selected=page.browsing_context;page.browsing_context=source_context;
  try{operation(rt,page,"markDragSource",source,deadline);page.browsing_context=selected;}catch(...){page.browsing_context=selected;throw;}
  release.held=true;release.send("down",start_x,start_y,deadline);
  for(std::int64_t step=1;step<=steps;++step)release.send("move",start_x+(end_x-start_x)*step/steps,start_y+(end_y-start_y)*step/steps,deadline);
  pause(deadline);
  for(;;) {
    const auto before=wait_ready(target);
    const auto mapped=target_scope->map_point(before.at("x").as_double(),before.at("y").as_double(),force,deadline);
    if(!mapped.at("ready").as_bool()){pause(deadline);continue;}
    release.send("move",before.at("x").as_double(),before.at("y").as_double(),deadline);pause(deadline);
    const auto after=wait_ready(target);const auto final=target_scope->map_point(after.at("x").as_double(),after.at("y").as_double(),force,deadline);
    if(final.at("ready").as_bool()&&(force||(same_position(before,after)&&mapped.at("x")==final.at("x")&&mapped.at("y")==final.at("y"))))break;
  }
  release.send("up",release.x,release.y,deadline);release.held=false;
  return Json{{"mode","pointer"},{"intercepted",false},{"crossContext",cross_context},{"html5Source",html5_source},{"sourcePoint",Json{{"x",start_x},{"y",start_y}}},
    {"targetPoint",Json{{"x",release.x},{"y",release.y}}},{"steps",steps}};
}
}
Json bidi_dom_command(BidiRuntime& rt,BidiPage& page,const Json& request,Deadline deadline) {
  std::size_t work=0;validate_wire(request,work);
  if(request.dump().size()>1024*1024)throw std::runtime_error("Native BiDi DOM request exceeds 1 MiB");
  auto frame_scope=bidi_resolve_frame(rt,page,request,deadline);bootstrap(rt,page,deadline);
  const auto family=text(request,"command"),action=text(request,"action");
  if(family=="waitForFunction"||family=="waitForURL"||family=="waitLoadState"||family=="waitForSelector"||family=="waitForText")return page_wait(rt,page,request,deadline);
  if((family=="form"||family=="locator")&&action=="dragTo")return drag(rt,page,*frame_scope,request,deadline);
  if(family=="waitFor") {
    Json wait=request;wait["state"]=text(request,"state","visible");
    const auto state=wait.at("state").as_string();
    if(state!="attached"&&state!="detached"&&state!="visible"&&state!="hidden")throw std::runtime_error("Invalid native locator wait state");
    while(!operation(rt,page,"elementState",wait,deadline).as_bool())pause(deadline);
    return Json();
  }
  if(family=="queryEvaluate"||family=="snapshotReferences") {
    for(;;) {
      const auto result=operation(rt,page,family=="queryEvaluate"?"read":"observe",request,deadline);
      if(result.at("ready").as_bool()){
        Json value=result.contains("value")?result.at("value"):Json();
        return family=="snapshotReferences"?value:runtime_envelope(std::move(value));
      }
      pause(deadline);
    }
  }
  if(family!="locator"&&family!="form")throw std::runtime_error("Unsupported native BiDi DOM command: "+family);
  if(action=="setInputFiles")return upload(rt,page,request,deadline);
  const bool pointer=action=="click"||action=="tap"||action=="hover"||action=="setChecked";
  if(!pointer&&action!="fill"&&action!="clear"&&action!="type"&&action!="press"&&action!="focus"&&action!="scrollIntoViewIfNeeded"&&action!="inputValue"&&action!="selectOption")throw std::runtime_error("Unsupported native BiDi DOM action: "+action);
  ScopedModifiers modifiers(*frame_scope,page,request,deadline,pointer);
  for(;;) {
    bidi_check_active(deadline);
    const auto state=operation(rt,page,"inspect",request,deadline);
    if(!state.at("ready").as_bool()){pause(deadline);continue;}
    if(flag(state,"done"))return state.contains("value")?state.at("value"):Json();
    if(action=="inputValue")return state.at("value");
    if(action=="focus"||action=="scrollIntoViewIfNeeded")return Json();
    if(action=="fill"||action=="clear"||action=="type"||action=="press") {
      if(!frame_scope->ancestors_visible(deadline)){pause(deadline);continue;}
      if(action=="press")return frame_scope->input(Json{{"kind","keyboard"},{"action","press"},{"key",text(request,"key")},{"delay",request.contains("delay")?request.at("delay"):Json(0)}},deadline);
      const auto value=action=="clear"?std::string():text(request,"value");
      if(value.size()>65536)throw std::runtime_error("Native BiDi input text exceeds 64 KiB");
      if(value.empty()&&action!="type")return frame_scope->input(Json{{"kind","keyboard"},{"action","press"},{"key","Backspace"}},deadline);
      return frame_scope->input(Json{{"kind","keyboard"},{"action",action=="type"?"type":"insertText"},{"text",value},{"delay",request.contains("delay")?request.at("delay"):Json(0)}},deadline);
    }
    const auto point=frame_scope->map_point(state.at("x").as_double(),state.at("y").as_double(),flag(request,"force"),deadline);
    if(!point.at("ready").as_bool()){pause(deadline);continue;}
    mouse(*frame_scope,"move",state.at("x").as_double(),state.at("y").as_double(),request,deadline);pause(deadline);
    const auto after=operation(rt,page,"inspect",request,deadline);
    if(!after.at("ready").as_bool())continue;
    if(flag(after,"done"))return after.contains("value")?after.at("value"):Json();
    const auto final=frame_scope->map_point(after.at("x").as_double(),after.at("y").as_double(),flag(request,"force"),deadline);
    if(!final.at("ready").as_bool())continue;
    if(!flag(request,"force")&&(!same_position(state,after)||point.at("x")!=final.at("x")||point.at("y")!=final.at("y")))continue;
    if(action=="hover"||flag(request,"trial"))return Json();
    try{mouse(*frame_scope,"down",after.at("x").as_double(),after.at("y").as_double(),request,deadline);mouse(*frame_scope,"up",after.at("x").as_double(),after.at("y").as_double(),request,deadline);}
    catch(...){try{BidiCleanupScope cleanup;mouse(*frame_scope,"up",after.at("x").as_double(),after.at("y").as_double(),request,std::chrono::steady_clock::now()+std::chrono::milliseconds(100));}catch(...){}throw;}
    if(action=="setChecked"&&!operation(rt,page,"verifyChecked",request,deadline).as_bool())throw std::runtime_error("Click did not change checkbox to requested state");
    return Json();
  }
}
} // namespace suitecut::detail
