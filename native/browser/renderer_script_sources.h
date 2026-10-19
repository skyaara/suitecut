// SPDX-License-Identifier: MIT
#pragma once
#include <array>
namespace suitecut {
struct NativeRendererScriptSource {const char* name;const char* source;};
inline constexpr NativeRendererScriptSource kNativeIdentityScript{"suitecut:initial-document-identity","(()=>{const r=new XMLHttpRequest();r.open('GET','suitecut-init://registry/settings-bootstrap',false);r.send();if(r.status!==200)throw Error('Native initial identity unavailable');return r.responseText==='true';})()"};
inline constexpr NativeRendererScriptSource kNativeEvidenceScript{"suitecut:initial-document-evidence","({url:location.href,state:document.readyState,length:history.length})"};
inline constexpr NativeRendererScriptSource kNativeSnapshotScript{"suitecut:init-snapshot","(()=>{const request=new XMLHttpRequest();request.open('GET','suitecut-init://registry/snapshot',false);request.send();if(request.status!==200)throw Error('Native init snapshot request failed: '+request.status);return request.responseText;})()"};
// A debugger pause in one renderer freezes synchronous loads in its other
// frames. Local children must still fetch/apply the actual snapshot before
// author code, without entering the main-factory breakpoint in that renderer.
inline constexpr NativeRendererScriptSource kNativeLocalChildSnapshotScript{"suitecut:init-snapshot-local-child",kNativeSnapshotScript.source};
template<class Frame>
const NativeRendererScriptSource& NativeSnapshotScriptForFrame(const Frame& frame){
  const auto parent=frame->GetParent();
  const auto parent_context=parent?parent->GetV8Context():nullptr;
  return parent_context&&parent_context->IsValid()?kNativeLocalChildSnapshotScript:kNativeSnapshotScript;
}
inline constexpr NativeRendererScriptSource kNativeCredentialScript{"suitecut:credential-readiness","(()=>{const r=new XMLHttpRequest();r.open('GET','suitecut-init://registry/credentials-ready',false);r.send();if(r.status!==200)throw Error('Native credential readiness failed: '+r.status);})()"};
inline constexpr NativeRendererScriptSource kNativeEmptyScript{"suitecut:initial-empty-context","void 0"};
inline constexpr std::array<NativeRendererScriptSource,6> kNativeRendererScriptSources{kNativeIdentityScript,kNativeEvidenceScript,kNativeSnapshotScript,kNativeLocalChildSnapshotScript,kNativeCredentialScript,kNativeEmptyScript};
} // namespace suitecut
