// SPDX-License-Identifier: MIT
#include "cdp_frame.hpp"
#include "cdp_network.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <vector>
namespace suitecut::detail {
namespace {
std::string text(const Json& value,const char* key) {
  return value.contains(key)?value.at(key).as_string():std::string{};
}
CdpPage dispatch_view(const CdpPage& page) {
  CdpPage view;view.id=page.id;view.context=page.context;view.target=page.target;view.session=page.session;
  view.frame_id=page.frame_id;view.isolated_context=page.isolated_context;
  view.isolated_loader=page.isolated_loader;view.isolated_frame=page.isolated_frame;
  return view;
}
const Json* find_frame(const Json& tree,const std::string& id) {
  if(tree.at("frame").at("id").as_string()==id)return &tree;
  if(tree.contains("childFrames"))for(const auto& child:tree.at("childFrames").as_array())if(auto found=find_frame(child,id))return found;
  return nullptr;
}
bool find_path(const Json& tree,const std::string& id,std::vector<std::string>& path) {
  path.push_back(tree.at("frame").at("id").as_string());
  if(path.back()==id)return true;
  if(tree.contains("childFrames"))for(const auto& child:tree.at("childFrames").as_array())if(find_path(child,id,path))return true;
  path.pop_back();return false;
}
Json result_value(const Json& result) {
  if(result.contains("exceptionDetails"))throw std::runtime_error("Native frame DOM evaluation failed: "+result.at("exceptionDetails").dump());
  const auto& remote=result.at("result");
  if(!remote.contains("value"))throw std::runtime_error("Native frame DOM evaluation has no value");
  return remote.at("value");
}
}
struct CdpFrameScope::Impl {
  CdpRuntime& rt;CdpPage& page;Deadline deadline;
  std::string original_session,original_frame,original_loader,original_isolated_frame;
  int original_context;
  struct Ancestor {CdpPage parent;std::string owner;};
  std::vector<Ancestor> ancestors;
  Impl(CdpRuntime& runtime,CdpPage& target,Deadline limit):rt(runtime),page(target),deadline(limit),
    original_session(target.session),original_frame(target.frame_id),original_loader(target.isolated_loader),
    original_isolated_frame(target.isolated_frame),original_context(target.isolated_context){}
  static bool transition(const std::exception& error) {
    const std::string message=error.what(),prefix="CDP error: ";
    if(message=="Targeted Chromium frame is detached")return true;
    if(!message.starts_with(prefix))return false;
    try {
      const auto detail=Json::parse(message.substr(prefix.size()));const auto reason=detail.at("message").as_string();
      const auto code=detail.at("code").as_int();
      return (code==-32000||code==-32602)&&(reason=="Cannot find context with specified id"||reason=="No target with given id found"||reason=="No frame for given id found"||reason=="No frame with given id found"||reason=="Session with given id not found."||reason=="Execution context was destroyed."||reason=="Inspected target navigated or closed");
    }catch(...){return false;}
  }
  void pause(Deadline limit) {
    cdp_check_active(limit);
    std::this_thread::sleep_until(std::min(limit,std::chrono::steady_clock::now()+std::chrono::milliseconds(20)));
    cdp_check_active(limit);
  }
  void world(const std::string& session,const std::string& frame) {
    page.session=session;page.frame_id=frame;page.isolated_context=0;page.isolated_loader.clear();page.isolated_frame.clear();
  }
  void discard(const std::string& frame,const std::string& session) {
    CdpPage root=dispatch_view(page);root.session=original_session;
    try {CdpCleanupScope cleanup;rt.call(root,"Target.detachFromTarget",Json{{"sessionId",session}},std::chrono::steady_clock::now()+std::chrono::milliseconds(100));}catch(...){}
    if(rt.network)rt.network->forget(session);
    if(auto entry=page.frame_sessions.find(frame);entry!=page.frame_sessions.end()&&entry->second==session)page.frame_sessions.erase(entry);
  }
  struct Status {std::string frame,url;bool pending=false;};
  Status status(const std::string& hint,CdpPage* owner_parent,const std::string& owner,Deadline limit) {
    Status value{hint,{}};
    if(!owner_parent)return value;
    const auto described=rt.call(*owner_parent,"DOM.describeNode",Json{{"objectId",owner},{"depth",1},{"pierce",true}},limit);
    value.frame=text(described.at("node"),"frameId");
    if(value.frame.empty()&&described.at("node").contains("contentDocument"))value.frame=text(described.at("node").at("contentDocument"),"frameId");
    const auto metadata=result_value(rt.call(*owner_parent,"Runtime.callFunctionOn",Json{{"objectId",owner},{"returnByValue",true},
      {"functionDeclaration","function(){return {connected:this.isConnected,src:this.getAttribute('src')?this.src:'',srcdoc:this.hasAttribute('srcdoc')};}"}},limit));
    if(!metadata.at("connected").as_bool())throw std::runtime_error("Native iframe owner is detached");
    const auto requested=text(metadata,"src");
    const auto tree=rt.call(*owner_parent,"Page.getFrameTree",Json::object{},limit);
    const auto selected=value.frame.empty()?nullptr:find_frame(tree.at("frameTree"),value.frame);
    value.url=selected?text(selected->at("frame"),"url"):requested;
    value.pending=value.frame.empty()||(selected&&(value.url.empty()||value.url=="about:blank")&&!requested.empty()&&requested!="about:blank"&&!metadata.at("srcdoc").as_bool());
    return value;
  }
  void target(std::string frame,Deadline limit,CdpPage* owner_parent=nullptr,const std::string& owner={}) {
    const auto parent_session=owner_parent?owner_parent->session:page.session;
    CdpPage root=dispatch_view(page);root.session=original_session;
    // A frame owner can begin in the parent renderer and commit into a new
    // target. Discover the current owner/token/target on every readiness pass;
    // none of these probes dispatches DOM mutation or trusted input.
    for(;;) {
      cdp_check_active(limit);
      const auto current=status(frame,owner_parent,owner,limit);frame=current.frame;
      if(current.pending){world(parent_session,frame);pause(limit);continue;}
      if(auto cached=page.frame_sessions.find(frame);cached!=page.frame_sessions.end()) {
        const auto session=cached->second;world(session,frame);
        try {
          const auto tree=rt.call(page,"Page.getFrameTree",Json::object{},limit);
          if(tree.at("frameTree").at("frame").at("id").as_string()!=frame)throw std::runtime_error("Targeted Chromium frame is detached");
          rt.value(page,"0",limit,true);return;
        }catch(const std::exception& error) {
          if(!transition(error))throw;
          discard(frame,session);
        }
      }
      world(parent_session,frame);
      try {rt.value(page,"0",limit,true);return;}catch(const std::exception& error){if(!transition(error))throw;}
      const auto targets=rt.call(root,"Target.getTargets",Json::object{},limit);
      const auto& infos=targets.at("targetInfos").as_array();
      if(infos.size()>4096)throw std::runtime_error("Native frame target discovery limit exceeded");
      std::vector<std::string> candidates;
      for(const auto& info:infos) {
        const auto id=text(info,"targetId");
        if(id!=frame&&text(info,"type")!="iframe")continue;
        if(page.context&&rt.contexts.contains(page.context)&&info.contains("browserContextId")&&info.at("browserContextId").as_string()!=rt.contexts.at(page.context))continue;
        if(id==frame)candidates.insert(candidates.begin(),id);
        else if(!current.url.empty()&&text(info,"url")==current.url)candidates.push_back(id);
        if(candidates.size()>128)throw std::runtime_error("Native matching iframe target limit exceeded");
      }
      for(const auto& candidate:candidates) {
        std::string session;
        try {
          // Attach only to an enumerated live target. Verify the actual root
          // frame identity rather than assuming targetId equals frameId.
          session=rt.call(root,"Target.attachToTarget",Json{{"targetId",candidate},{"flatten",true}},limit).at("sessionId").as_string();
          world(session,frame);
          const auto tree=rt.call(page,"Page.getFrameTree",Json::object{},limit);
          if(tree.at("frameTree").at("frame").at("id").as_string()!=frame){discard(frame,session);continue;}
          if(page.frame_sessions.size()>=64) {
            const auto oldest=std::find_if(page.frame_sessions.begin(),page.frame_sessions.end(),[&](const auto& entry){
              if(entry.second==parent_session||entry.second==original_session)return false;
              for(const auto& ancestor:ancestors)if(entry.second==ancestor.parent.session)return false;
              return true;
            });
            if(oldest==page.frame_sessions.end())throw std::runtime_error("Native active iframe session limit exceeded");
            const auto old_frame=oldest->first,old_session=oldest->second;discard(old_frame,old_session);
          }
          if(rt.network)rt.network->setup(rt,page,limit);
          rt.call(page,"Page.enable",Json::object{},limit);
          rt.call(page,"Runtime.enable",Json::object{},limit);
          rt.value(page,"0",limit,true);
          page.frame_sessions.insert_or_assign(frame,session);return;
        }catch(const std::exception& error) {
          if(!session.empty())discard(frame,session);
          if(!transition(error))throw;
        }
      }
      world(parent_session,frame);pause(limit);
    }
  }
};
CdpFrameScope::CdpFrameScope(CdpRuntime& rt,CdpPage& page,Deadline deadline):impl_(std::make_unique<Impl>(rt,page,deadline)){}
CdpFrameScope::~CdpFrameScope() {
  auto& p=*impl_;
  CdpCleanupScope cleanup_scope;
  const auto cleanup=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
  for(auto it=p.ancestors.rbegin();it!=p.ancestors.rend();++it)try {
    p.rt.call(it->parent,"Runtime.releaseObject",Json{{"objectId",it->owner}},cleanup);
  }catch(...){}
  if(!p.ancestors.empty()) {
    p.page.session=p.original_session;p.page.frame_id=p.original_frame;
    p.page.isolated_loader=p.original_loader;p.page.isolated_frame=p.original_isolated_frame;
    p.page.isolated_context=p.original_context;
  }
}
void CdpFrameScope::enter_owner(const std::string& object_id,Deadline deadline) {
  auto& p=*impl_;
  if(p.ancestors.size()>=64)throw std::runtime_error("Native frame path depth exceeds 64");
  struct ReleaseUnadopted {
    CdpRuntime& rt;CdpPage parent;std::string object;bool adopted=false;
    ~ReleaseUnadopted(){if(!adopted)try{CdpCleanupScope cleanup;rt.call(parent,"Runtime.releaseObject",Json{{"objectId",object}},std::chrono::steady_clock::now()+std::chrono::milliseconds(100));}catch(...){}}
  } release{p.rt,dispatch_view(p.page),object_id};
  const auto described=p.rt.call(p.page,"DOM.describeNode",Json{{"objectId",object_id},{"depth",1},{"pierce",true}},deadline);
  const auto& node=described.at("node");
  const auto name=node.at("nodeName").as_string();
  if(name!="IFRAME"&&name!="FRAME")throw std::runtime_error("Native frame locator target is not an iframe or frame");
  auto frame=text(node,"frameId");
  if(frame.empty()&&node.contains("contentDocument"))frame=text(node.at("contentDocument"),"frameId");
  if(frame.empty()) {
    const auto tree=p.rt.call(p.page,"Page.getFrameTree",Json::object{},deadline);
    const auto parent=p.page.frame_id.empty()?&tree.at("frameTree"):find_frame(tree.at("frameTree"),p.page.frame_id);
    if(parent&&parent->contains("childFrames"))for(const auto& child:parent->at("childFrames").as_array()) {
      const auto candidate=child.at("frame").at("id").as_string();
      const auto owner=p.rt.call(p.page,"DOM.getFrameOwner",Json{{"frameId",candidate}},deadline);
      if(owner.at("backendNodeId")==node.at("backendNodeId")){frame=candidate;break;}
    }
  }
  auto parent=dispatch_view(p.page);
  try {p.target(frame,deadline,&parent,object_id);}catch(...) {
    p.page.session=parent.session;p.page.frame_id=parent.frame_id;p.page.isolated_context=parent.isolated_context;
    p.page.isolated_loader=parent.isolated_loader;p.page.isolated_frame=parent.isolated_frame;
    throw;
  }
  p.ancestors.push_back({std::move(parent),object_id});release.adopted=true;
}
void CdpFrameScope::enter_frame(const std::string& frame_id,Deadline deadline) {
  auto& p=*impl_;
  if(frame_id.empty())return;
  const auto tree=p.rt.call(p.page,"Page.getFrameTree",Json::object{},deadline);
  std::vector<std::string> path;
  if(!find_path(tree.at("frameTree"),frame_id,path))throw std::runtime_error("Native frame identity is detached or unavailable");
  // Resolve owners through CDP, so an author origin boundary cannot hide them.
  for(std::size_t i=1;i<path.size();++i) {
    p.rt.value(p.page,"0",deadline,true);
    const auto owner=p.rt.call(p.page,"DOM.getFrameOwner",Json{{"frameId",path[i]}},deadline);
    const auto resolved=p.rt.call(p.page,"DOM.resolveNode",Json{{"backendNodeId",owner.at("backendNodeId")},{"executionContextId",p.page.isolated_context}},deadline);
    enter_owner(resolved.at("object").at("objectId").as_string(),deadline);
  }
}
Json CdpFrameScope::map_point(double x,double y,bool force,Deadline deadline) {
  auto& p=*impl_;
  // DOM rectangles are measured inside each parent world. Unlike CDP's box
  // quads, these are unambiguously local even for in-process nested frames.
  for(auto it=p.ancestors.rbegin();it!=p.ancestors.rend();++it) {
    const auto result=p.rt.call(it->parent,"Runtime.callFunctionOn",Json{
      {"objectId",it->owner},{"returnByValue",true},
      {"functionDeclaration",R"JS(function(x,y,force){
        if(!this.isConnected)throw new Error('Native iframe owner is detached');
        this.scrollIntoView({block:'center',inline:'center',behavior:'instant'});
        const r=this.getBoundingClientRect(),s=getComputedStyle(this);
        let transformDepth=0;for(let n=this;n;n=n.parentElement||n.getRootNode().host||null){if(++transformDepth>64)throw new Error('Native iframe transform ancestor depth limit exceeded');const style=getComputedStyle(n),matrix=style.transform==='none'?null:new DOMMatrixReadOnly(style.transform);if(style.perspective!=='none'||(matrix&&(Math.abs(matrix.b)>1e-8||Math.abs(matrix.c)>1e-8||matrix.a<=0||matrix.d<=0||!matrix.is2D)))throw new Error('Native pointer targeting does not support rotated, skewed or perspective iframe owners');}
        const sx=this.offsetWidth?r.width/this.offsetWidth:0,sy=this.offsetHeight?r.height/this.offsetHeight:0;
        if(!sx||!sy||s.visibility==='hidden'||s.display==='none')return {ready:false};
        if(x<0||y<0||x>=this.clientWidth||y>=this.clientHeight)return {ready:false};
        const px=r.left+(this.clientLeft+x)*sx,py=r.top+(this.clientTop+y)*sy;
        if(px<0||py<0||px>=innerWidth||py>=innerHeight)return {ready:false};
        if(!force){let hit=document.elementFromPoint(px,py);for(let depth=0;hit?.shadowRoot&&depth<64;depth++){const next=hit.shadowRoot.elementFromPoint(px,py);if(!next||next===hit)break;hit=next;}let receives=false;for(let n=hit;n;n=n.assignedSlot||n.parentElement||n.getRootNode().host||null)if(n===this){receives=true;break;}if(!receives)return {ready:false};}
        return {ready:true,x:px,y:py};
      })JS"},
      {"arguments",Json::array{Json{{"value",x}},Json{{"value",y}},Json{{"value",force}}}}},deadline);
    const auto point=result_value(result);
    if(!point.at("ready").as_bool())return Json{{"ready",false}};
    x=point.at("x").as_double();y=point.at("y").as_double();
  }
  return Json{{"ready",true},{"x",x},{"y",y}};
}
Json CdpFrameScope::input(const Json& request,Deadline deadline) {
  auto& p=*impl_;const auto selected=p.page.session;p.page.session=p.original_session;
  try {auto result=cdp_input_command(p.rt,p.page,request,deadline);p.page.session=selected;return result;}catch(...){p.page.session=selected;throw;}
}
Json CdpFrameScope::call_root(const std::string& method,Json params,Deadline deadline) {
  auto& p=*impl_;const auto selected=p.page.session;p.page.session=p.original_session;
  try {auto result=p.rt.call(p.page,method,std::move(params),deadline);p.page.session=selected;return result;}catch(...){p.page.session=selected;throw;}
}
void CdpFrameScope::insert_text(const std::string& text,Deadline deadline) {
  auto& p=*impl_;const auto selected=p.page.session;p.page.session=p.original_session;
  try {p.rt.call(p.page,"Input.insertText",Json{{"text",text}},deadline);p.page.session=selected;}catch(...){p.page.session=selected;throw;}
}
bool CdpFrameScope::selected()const{return !impl_->ancestors.empty();}
void CdpFrameScope::refresh(Deadline deadline) {
  auto& p=*impl_;const auto frame=p.page.frame_id;
  if(frame.empty()){p.page.isolated_context=0;p.page.isolated_loader.clear();p.page.isolated_frame.clear();return;}
  // A newly appended about:blank frame can move to an OOPIF target after its
  // first world was selected. Re-resolve only before an expression is admitted.
  p.page.session=p.ancestors.empty()?p.original_session:p.ancestors.back().parent.session;
  if(p.ancestors.empty())p.target(frame,deadline);
  else p.target(frame,deadline,&p.ancestors.back().parent,p.ancestors.back().owner);
}
void CdpFrameScope::restore_root() {
  auto& p=*impl_;p.page.session=p.original_session;p.page.frame_id=p.original_frame;
  p.page.isolated_loader=p.original_loader;p.page.isolated_frame=p.original_isolated_frame;p.page.isolated_context=p.original_context;
}
std::string CdpFrameScope::root_session()const{return impl_->original_session;}
} // namespace suitecut::detail
