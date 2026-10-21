// SPDX-License-Identifier: MIT
// Included inside Source and the controlled Source-wrapper test host.
bool SnapshotReferenceTokens(Dict message,std::vector<std::string>& tokens){
  const auto query=message->GetList("query");if(!query)return true;
  struct Level{CefRefPtr<CefListValue> query;size_t depth;};std::vector<Level> pending{{query,0}};size_t work=0;
  while(!pending.empty()){
    const auto level=pending.back();pending.pop_back();if(level.depth>64||level.query->GetSize()>64)return false;
    for(size_t i=0;i<level.query->GetSize();++i){
      if(++work>4096)return false;
      const auto step=level.query->GetDictionary(i);if(!step)continue;
      const auto engine=step->GetString("engine").ToString();
      if(engine=="reference"){
        if(step->GetType("ref")!=VTYPE_STRING)return false;
        const auto token=step->GetString("ref").ToString();if(token.empty()||token.size()>192)return false;tokens.push_back(token);
      }
      const std::vector<std::string> children=engine=="filter"?std::vector<std::string>{"has","hasNot"}:
        engine=="capture"?std::vector<std::string>{"query","descendants"}:
        engine=="and"||engine=="or"||engine=="chain"?std::vector<std::string>{"query"}:std::vector<std::string>{};
      for(const auto& key:children)if(step->GetType(key)==VTYPE_LIST)pending.push_back({step->GetList(key),level.depth+1});
    }
  }
  return true;
}
bool QueryHasSnapshotReferences(Dict message){std::vector<std::string> tokens;return SnapshotReferenceTokens(message,tokens)&&!tokens.empty();}
bool PrepareSnapshotReferenceScope(int id,Dict message){
  std::vector<std::string> tokens;
  if(!SnapshotReferenceTokens(message,tokens)){Complete(id,false,"Invalid native snapshot reference query");return false;}
  if(tokens.empty())return true;
  std::optional<SnapshotReferenceDocument> wanted;
  for(const auto& token:tokens){
    SnapshotReferenceDocument document;
    if(!snapshot_references_||!snapshot_references_->DocumentFor(token,document)){Complete(id,false,"Native snapshot reference is unknown or stale");return false;}
    const auto current=ReferenceDocument(document.frame);
    if(!current||*current!=document){Complete(id,false,"Native snapshot reference document was replaced");return false;}
    if(wanted&&*wanted!=document){Complete(id,false,"Native snapshot reference query crosses different frame documents");return false;}wanted=document;
  }
  // A frame path begins in its genuine parent. Its leaf query is checked again
  // after actual iframe resolution; references must never skip that resolution.
  if(const auto path=message->GetList("framePath");path&&path->GetSize())return true;
  if(message->HasKey("frame")&&message->GetString("frame")!=wanted->frame){Complete(id,false,"Native snapshot reference belongs to a different frame");return false;}
  const auto query=message->GetList("query");const auto first=query&&query->GetSize()?query->GetDictionary(0):nullptr;
  // Only an actual unscoped reference-root lookup selects its frame implicitly.
  // A preceding CSS/custom/filter scope keeps its original main-frame root.
  const bool reference_root=first&&first->GetString("engine")=="reference"&&!message->HasKey("nativeRootHandle");
  if(!message->HasKey("frame")&&!reference_root&&wanted->frame!=main_frame_id_){Complete(id,false,"Native snapshot reference belongs to a different scoped frame");return false;}
  if(!message->HasKey("frame")&&reference_root)message->SetString("frame",wanted->frame);
  return true;
}

bool ReferencePlanCurrent(int owner,uint64_t generation,const SnapshotReferenceDocument& document){
  if(!operations_.IsCurrent(owner,generation))return false;
  // A late real ACK can arrive before the next deadline tick. Publish the
  // original timeout now, before a plan labels it a document/context change.
  if(operations_.IsExpired(owner,generation,Now())){Complete(owner,false,"Native operation timed out");return false;}
  const auto current=ReferenceDocument(document.frame);
  return current&&*current==document;
}
// Persistent installation consumes the ordinary capture object. Its temporary
// group already belongs to SelectorWaits; remove the duplicate fire-and-forget
// evaluation tracker only after every actual session was admitted there.
bool ReferenceTemporaryGroups(int owner){
  if(!selector_waits_||!selector_waits_->Contains(owner))return false;
  const auto groups=evaluation_groups_.find(owner);if(groups==evaluation_groups_.end())return true;
  const auto name="suitecut-automation-"+std::to_string(owner);
  for(const auto& session:groups->second)if(!selector_waits_->Group(owner,name,session))return false;
  evaluation_groups_.erase(groups);return true;
}
