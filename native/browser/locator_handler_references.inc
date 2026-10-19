// SPDX-License-Identifier: MIT
// Registration retains immutable actual token/document identities. The real
// query path borrows its native group for each observation; resident policy
// must not pin a retired group after navigation or snapshot replacement.
std::function<bool()> LocatorHandlerReferences(Dict target,std::string& error) {
  std::vector<std::string> tokens;
  if(!SnapshotReferenceTokens(target,tokens)){error="Invalid native locator handler reference query";return {};}
  if(tokens.empty())return []{return true;};
  std::optional<SnapshotReferenceDocument> wanted;
  for(const auto&token:tokens){
    SnapshotReferenceDocument document;
    if(!snapshot_references_||!snapshot_references_->DocumentFor(token,document)){
      error="Native locator handler snapshot reference is unknown or stale";return {};
    }
    const auto current=ReferenceDocument(document.frame);
    if(!current||*current!=document||(wanted&&*wanted!=document)){
      error="Native locator handler snapshot references cross or replaced documents";return {};
    }
    wanted=document;
  }
  const auto path=target->GetList("framePath");
  if(!path||!path->GetSize()){
    if(target->HasKey("frame")&&target->GetString("frame")!=wanted->frame){error="Native locator handler reference belongs to another frame";return {};}
    const auto query=target->GetList("query");const auto first=query&&query->GetSize()?query->GetDictionary(0):nullptr;
    const bool root=first&&first->GetString("engine")=="reference"&&!target->HasKey("nativeRootHandle");
    if(!target->HasKey("frame")&&!root&&wanted->frame!=main_frame_id_){error="Native locator handler reference belongs to another scoped frame";return {};}
    if(!target->HasKey("frame")&&root)target->SetString("frame",wanted->frame);
  }
  return [this,document=*wanted,tokens=std::move(tokens)]{
    const auto current=ReferenceDocument(document.frame);
    if(!current||*current!=document||!snapshot_references_)return false;
    for(const auto&token:tokens){SnapshotReferenceDocument actual;if(!snapshot_references_->DocumentFor(token,actual)||actual!=document)return false;}
    return true;
  };
}
