// SPDX-License-Identifier: MIT
#pragma once
#include "network_body_publication.h"
#include "network_post_data.h"

namespace suitecut {
// Exact body preparation shared by Network and Fetch metadata. Source supplies
// bounded binary emission; the publication controller owns native deadlines.
class NativeNetworkBodyControls {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  enum class Prepared {Absent,Inline,Transferred,Unavailable,NeedsFetch};
  NativeNetworkBodyControls(NativeNetworkBodyPublication::Emit emit,
      NativeNetworkBodyPublication::Publish publish,
      NativeNetworkBodyPublication::Failure failure)
      :publication_(std::move(emit),std::move(publish),std::move(failure)){}
  Prepared Prepare(Dict request,double deadline,bool require_exact) {
    if(!request)return Prepared::Absent;
    if(request->GetType("postDataBodyId")==VTYPE_STRING)return Prepared::Transferred;
    const bool entries=request->GetType("postDataEntries")==VTYPE_LIST;
    if(require_exact && !entries && (request->HasKey("postData") || request->GetBool("hasPostData")))return Prepared::NeedsFetch;
    auto captured=CaptureNativePostData(request);
    request->Remove("postDataEntries");
    if(captured.kind==NativePostData::Kind::Absent)return Prepared::Absent;
    if(captured.kind==NativePostData::Kind::Unavailable){Unavailable(request);return Prepared::Unavailable;}
    if(captured.kind==NativePostData::Kind::Text) {
      const auto text=request->GetString("postData").ToString();
      if(text.size()<=524288)return Prepared::Inline;
      captured.bytes.assign(text.begin(),text.end());
    }
    const auto body=publication_.Begin(std::move(captured.bytes),deadline);
    if(!body){Unavailable(request);return Prepared::Unavailable;}
    request->Remove("postData");request->Remove("postDataBase64");request->Remove("postDataUnavailable");
    request->SetString("postDataBodyId",std::to_string(*body));return Prepared::Transferred;
  }
  // Explicit route overrides are already exact base64 bytes. Their original
  // native pause deadline is retained when projecting a fallback invocation.
  Prepared PrepareOverride(Dict request,double deadline) {
    if(!request || request->GetType("postDataBase64")!=VTYPE_STRING)return Prepare(request,deadline,false);
    const auto encoded=request->GetString("postDataBase64").ToString();
    if(encoded.size()>2796204){Unavailable(request);return Prepared::Unavailable;}
    std::vector<uint8_t> bytes;
    if(!encoded.empty()) {
      auto decoded=CefBase64Decode(encoded);
      if(!decoded || decoded->GetSize()>2097152){Unavailable(request);return Prepared::Unavailable;}
      bytes.resize(decoded->GetSize());decoded->GetData(bytes.data(),bytes.size(),0);
    }
    const auto body=publication_.Begin(std::move(bytes),deadline);
    if(!body){Unavailable(request);return Prepared::Unavailable;}
    request->Remove("postData");request->Remove("postDataBase64");request->Remove("postDataUnavailable");
    request->SetString("postDataBodyId",std::to_string(*body));return Prepared::Transferred;
  }
  NativeNetworkBodyPublication& publication() {return publication_;}
 private:
  static void Unavailable(Dict request) {
    request->Remove("postData");request->Remove("postDataBase64");request->Remove("postDataBodyId");request->Remove("postDataEntries");request->SetBool("postDataUnavailable",true);
  }
  NativeNetworkBodyPublication publication_;
};
}  // namespace suitecut
