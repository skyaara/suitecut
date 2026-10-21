// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "renderer_script_sources.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <string_view>

// Native frame/context doubles exercise the exact renderer selection helper;
// they do not model CEF bootstrap execution or claim a browser runtime proof.
struct Context {bool valid;bool IsValid()const{return valid;}};
struct Frame {
  std::shared_ptr<Frame> parent;
  std::shared_ptr<Context> context;
  auto GetParent()const{return parent;}
  auto GetV8Context()const{return context;}
};
int main(){
  using namespace suitecut;
  using std::string_view;
  int groups=0;
  const auto root=std::make_shared<Frame>();
  assert(&NativeSnapshotScriptForFrame(root)==&kNativeSnapshotScript);++groups;
  const auto child=std::make_shared<Frame>();child->parent=root;
  assert(&NativeSnapshotScriptForFrame(child)==&kNativeSnapshotScript);++groups;
  root->context=std::make_shared<Context>(Context{false});
  assert(&NativeSnapshotScriptForFrame(child)==&kNativeSnapshotScript);++groups;
  root->context->valid=true;
  assert(&NativeSnapshotScriptForFrame(child)==&kNativeLocalChildSnapshotScript);
  assert(&NativeSnapshotScriptForFrame(root)==&kNativeSnapshotScript);++groups;
  root->context.reset();
  assert(&NativeSnapshotScriptForFrame(child)==&kNativeSnapshotScript);++groups;
  assert(string_view(kNativeLocalChildSnapshotScript.source)=="void 0");
  assert(string_view(kNativeLocalChildSnapshotScript.source).find("XMLHttpRequest")==string_view::npos);
  assert(string_view(kNativeSnapshotScript.name)!=kNativeLocalChildSnapshotScript.name);
  assert(string_view(kNativeSnapshotScript.source).find("request.open('GET','suitecut-init://registry/snapshot',false)")!=string_view::npos);
  assert(string_view(kNativeSnapshotScript.source).find("request.status!==200)throw")!=string_view::npos);++groups;
  assert(string_view(kNativeSnapshotScript.source).find("var nativeSnapshot;if(typeof nativeSnapshot==='string')return nativeSnapshot;")!=string_view::npos);++groups;
  size_t ordinary=0,local=0;
  for(const auto&script:kNativeRendererScriptSources){
    ordinary+=string_view(script.name)==kNativeSnapshotScript.name;
    local+=string_view(script.name)==kNativeLocalChildSnapshotScript.name;
  }
  assert(ordinary==1&&local==1);++groups;
  std::printf("Native renderer snapshot identity: %d controlled groups PASS\n",groups);
}
