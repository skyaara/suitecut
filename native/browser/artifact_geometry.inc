// SPDX-License-Identifier: MIT
// Included inside Source. The capture controller invokes this after its
// temporary rendering styles are active, so geometry reflects the actual shot.
using ArtifactGeometryDone = std::function<void(bool, const std::string&)>;
struct ArtifactGeometryWork {
  int owner = 0; uint64_t generation = 0;
  Dict config, target;
  ArtifactGeometryDone done;
  std::string frame, session_root, node;
  std::vector<double> quad, mapped;
  double viewport_width = 0, viewport_height = 0;
};
void ArtifactGeometry(int id, Dict config, ArtifactGeometryDone done) {
  const auto operation = operations_.Get(id); if (!operation) return;
  if(config->HasKey("element")&&config->GetType("element")!=VTYPE_DICTIONARY){done(false,"Invalid native screenshot element locator");return;}
  auto target = config->GetDictionary("element");
  auto after_element = [this, id, config, done = std::move(done)](bool ok, const std::string& error) {
    if (!ok) { done(false, error); return; }
    ArtifactMasks(id, config, done);
  };
  if (!target) { after_element(true, ""); return; }
  auto work = std::make_shared<ArtifactGeometryWork>();
  work->owner = id; work->generation = operation->generation;
  work->config = config; work->target = target->Copy(false); work->done = std::move(after_element);
  ArtifactResolveElement(work);
}
void ArtifactResolveElement(std::shared_ptr<ArtifactGeometryWork> work) {
  const int id = work->owner;
  if (!operations_.IsCurrent(id, work->generation)) return;
  work->quad.clear(); work->mapped.clear();
  auto target = work->target;
  if (target->HasKey("nativeHandle")) {
    if (target->GetType("nativeHandle") != VTYPE_INT) { work->done(false, "Invalid native screenshot element handle"); return; }
    const auto handle = handles_.find(target->GetInt("nativeHandle"));
    if (handle == handles_.end()) { work->done(false, MissingHandle(target->GetInt("nativeHandle"))); return; }
    if (handle->second.node_id.empty()) { work->done(false, "Native screenshot handle is not an element"); return; }
    work->frame = handle->second.frame_id; work->node = handle->second.node_id;
    ArtifactUtilityNode(work); return;
  }
  ResolveFramePath(id, work->generation, target, 0, FrameId(target), [this, work](Dict resolved) {
    if (!operations_.IsCurrent(work->owner, work->generation)) return;
    work->frame = FrameId(resolved);
    const bool utility = !QueryNeedsMainWorld(resolved);
    WithFrame(work->owner, work->frame, [this, work, resolved](int context) {
      const int id = work->owner;
      auto params = CefDictionaryValue::Create(); params->SetInt("contextId", context);
      params->SetString("expression", "(()=>{const nodes=" + QueryExpression(resolved) +
          ";if(!nodes.length)return null;if(nodes.length>1)throw new Error('Strict screenshot locator matched multiple elements');return nodes[0]})()");
      params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
      evaluation_groups_[id].insert(operation_sessions_[id]);
      auto callback=[this, work](bool ok, const std::string& json) {
        if (!operations_.IsCurrent(work->owner, work->generation)) return;
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto node = result ? result->GetDictionary("result") : nullptr;
        if (!ok || !node || result->HasKey("exceptionDetails")) { work->done(false, "Native screenshot locator resolution failed"); return; }
        if (node->GetString("subtype") == "null") { ArtifactRetryElement(work); return; }
        if (node->GetType("objectId") != VTYPE_STRING) { work->done(false, "Native screenshot locator did not resolve to an element"); return; }
        work->node = node->GetString("objectId"); ArtifactUtilityNode(work);
      };
      if(QueryUsesMultipleWorlds(resolved))QueryInspect(id,resolved,"if(!nodes.length)return null;if(nodes.length>1)throw new Error('Strict screenshot locator matched multiple elements');return nodes[0];",true,false,std::move(callback));
      else DevTools(id,"Runtime.evaluate",params,std::move(callback),std::nullopt,false,kMaxResponseBytes,true);
    }, utility);
  });
}
void ArtifactRetryElement(std::shared_ptr<ArtifactGeometryWork> work) {
  if (!operations_.IsCurrent(work->owner, work->generation)) return;
  if(ScreenshotAssertionOwns(work->owner)){
    if(!ReferenceTemporaryGroups(work->owner)){Complete(work->owner,false,"Native screenshot temporary-group transfer rejected");return;}
    SelectorWaits().Pause(work->owner);
    ScreenshotAssertionRetryGroups(work->owner,work->generation,[this,work]{ArtifactResolveElement(work);});return;
  }
  const auto groups = evaluation_groups_.find(work->owner);
  if (groups != evaluation_groups_.end()) {
    const auto sessions = groups->second; evaluation_groups_.erase(groups);
    for (const auto& session : sessions) {
      auto params = CefDictionaryValue::Create(); params->SetString("objectGroup", "suitecut-automation-" + std::to_string(work->owner));
      SendDevTools("Runtime.releaseObjectGroup", params, session);
    }
  }
  CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), work] {
    if (self->operations_.IsCurrent(work->owner, work->generation)) self->ArtifactResolveElement(work);
  }), 20);
}
void ArtifactUtilityNode(std::shared_ptr<ArtifactGeometryWork> work) {
  const int id = work->owner;
  WithFrame(id, work->frame, [this, work](int context) {
    const int id = work->owner;
    auto params = CefDictionaryValue::Create(); params->SetString("objectId", work->node);
    DevTools(id, "DOM.describeNode", params, [this, work, context](bool ok, const std::string& json) {
      if (!operations_.IsCurrent(work->owner, work->generation)) return;
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
      auto node = result ? result->GetDictionary("node") : nullptr;
      if (!ok || !node || node->GetType("backendNodeId") != VTYPE_INT) { work->done(false, "Native screenshot node is unavailable"); return; }
      auto params = CefDictionaryValue::Create(); params->SetInt("backendNodeId", node->GetInt("backendNodeId"));
      params->SetInt("executionContextId", context); params->SetString("objectGroup", "suitecut-automation-" + std::to_string(work->owner));
      evaluation_groups_[work->owner].insert(operation_sessions_[work->owner]);
      DevTools(work->owner, "DOM.resolveNode", params, [this, work](bool success, const std::string& json) {
        if (!operations_.IsCurrent(work->owner, work->generation)) return;
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto object = result ? result->GetDictionary("object") : nullptr;
        if (!success || !object || object->GetType("objectId") != VTYPE_STRING) { work->done(false, "Native screenshot node context is unavailable"); return; }
        work->node = object->GetString("objectId"); ArtifactScrollElement(work);
      });
    });
  }, true);
}
void ArtifactScrollElement(std::shared_ptr<ArtifactGeometryWork> work) {
  if (!operations_.IsCurrent(work->owner, work->generation)) return;
  auto params = CefDictionaryValue::Create(); params->SetString("objectId", work->node);
  params->SetBool("awaitPromise", true); params->SetBool("returnByValue", true);
  params->SetString("functionDeclaration", R"JS(async function(){
    if(!this.isConnected)return 'detached';
    this.scrollIntoView({block:'center',inline:'center',behavior:'instant'});
    const a=this.getBoundingClientRect();await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));
    const b=this.getBoundingClientRect(),s=getComputedStyle(this);
    if(!this.isConnected)return 'detached';
    return b.width>0&&b.height>0&&s.visibility!=='hidden'&&s.visibility!=='collapse'&&
      ['x','y','width','height'].every(k=>Math.abs(a[k]-b[k])<0.01)?'ready':'retry';
  })JS");
  DevTools(work->owner, "Runtime.callFunctionOn", params, [this, work](bool ok, const std::string& json) {
    if (!operations_.IsCurrent(work->owner, work->generation)) return;
    auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
    auto remote = result ? result->GetDictionary("result") : nullptr;
    if (!ok || !remote || result->HasKey("exceptionDetails")) { work->done(false, "Native screenshot element scroll failed"); return; }
    if (remote->GetString("value") == "detached" && work->target->HasKey("nativeHandle")) {
      work->done(false, "Node is detached from document"); return;
    }
    if (remote->GetString("value") != "ready") { ArtifactRetryElement(work); return; }
    auto params = CefDictionaryValue::Create(); params->SetString("objectId", work->node);
    DevTools(work->owner, "DOM.getBoxModel", params, [this, work](bool success, const std::string& json) {
      if (!operations_.IsCurrent(work->owner, work->generation)) return;
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
      auto model = result ? result->GetDictionary("model") : nullptr; auto quad = model ? model->GetList("border") : nullptr;
      if (!success || !quad || quad->GetSize() != 8) { work->done(false, "Native screenshot element quad unavailable"); return; }
      for (size_t i = 0; i < 8; ++i) {
        if (quad->GetType(i) != VTYPE_DOUBLE && quad->GetType(i) != VTYPE_INT) { work->done(false, "Invalid native screenshot element quad"); return; }
        const double value = quad->GetType(i) == VTYPE_INT ? quad->GetInt(i) : quad->GetDouble(i);
        if (!std::isfinite(value) || std::abs(value)>100000000) { work->done(false, "Native screenshot element quad exceeds geometry bounds"); return; }
        work->quad.push_back(value);
      }
      work->session_root = work->frame;
      while (true) {
        const auto frame = frame_states_.find(work->session_root);
        if (frame == frame_states_.end()) { work->done(false, "Native screenshot frame detached"); return; }
        const auto parent = frame_states_.find(frame->second.parent);
        if (parent == frame_states_.end() || parent->second.session != frame->second.session) break;
        work->session_root = frame->second.parent;
      }
      Evaluate(work->owner, "({width:innerWidth,height:innerHeight})", [this, work](bool valid, const std::string& json) {
        if (!operations_.IsCurrent(work->owner, work->generation)) return;
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto remote = result ? result->GetDictionary("result") : nullptr; auto value = remote ? remote->GetDictionary("value") : nullptr;
        if (!valid || !value || result->HasKey("exceptionDetails")) { work->done(false, "Native screenshot frame viewport unavailable"); return; }
        work->viewport_width = Number(value, "width"); work->viewport_height = Number(value, "height");
        ArtifactMapCorner(work, 0);
      }, work->session_root, true);
    });
  });
}
void ArtifactMapCorner(std::shared_ptr<ArtifactGeometryWork> work, size_t corner) {
  if (!operations_.IsCurrent(work->owner, work->generation)) return;
  if (corner == 4) {
    Evaluate(work->owner, "(async()=>{await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));return {x:scrollX,y:scrollY}})()",
      [this, work](bool ok, const std::string& json) {
        if (!operations_.IsCurrent(work->owner, work->generation)) return;
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto remote = result ? result->GetDictionary("result") : nullptr; auto value = remote ? remote->GetDictionary("value") : nullptr;
        if (!ok || !value || result->HasKey("exceptionDetails")) { work->done(false, "Native screenshot root scroll unavailable"); return; }
        double left=work->mapped[0],right=left,top=work->mapped[1],bottom=top;
        for(size_t i=2;i<8;i+=2){left=std::min(left,work->mapped[i]);right=std::max(right,work->mapped[i]);top=std::min(top,work->mapped[i+1]);bottom=std::max(bottom,work->mapped[i+1]);}
        auto clip=CefDictionaryValue::Create();clip->SetDouble("x",left+Number(value,"x"));clip->SetDouble("y",top+Number(value,"y"));
        clip->SetDouble("width",right-left);clip->SetDouble("height",bottom-top);
        work->config->SetDictionary("clip",clip);work->config->SetBool("fullPage",true);
        operation_frames_[work->owner]=main_frame_id_;operation_sessions_[work->owner]="";
        work->done(true, "");
      }, main_frame_id_, true);
    return;
  }
  MapFramePoint(work->owner, work->session_root, work->quad[corner*2], work->quad[corner*2+1],
    work->viewport_width,work->viewport_height,[this,work,corner](double x,double y){
      if(!operations_.IsCurrent(work->owner,work->generation))return;
      work->mapped.push_back(x);work->mapped.push_back(y);ArtifactMapCorner(work,corner+1);
    });
}
