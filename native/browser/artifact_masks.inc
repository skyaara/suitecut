// SPDX-License-Identifier: MIT
// Included inside Source after artifact_geometry.inc. Masks use all real DOM
// matches, adopted into the utility world before reading their frame-local rect.
struct ArtifactMaskWork {
  int owner = 0; uint64_t generation = 0;
  Dict config; ArtifactGeometryDone done;
  CefRefPtr<CefListValue> targets, boxes;
  size_t target = 0, node = 0, total = 0;
  std::string frame;
  std::vector<std::string> nodes;
  std::vector<double> local, mapped;
  double viewport_width = 0, viewport_height = 0;
};
void ArtifactMasks(int id, Dict config, ArtifactGeometryDone done) {
  const auto operation = operations_.Get(id); if (!operation) return;
  if(config->HasKey("masks")&&config->GetType("masks")!=VTYPE_LIST){done(false,"Invalid native screenshot mask list");return;}
  auto targets = config->GetList("masks");
  if (!targets || !targets->GetSize()) { done(true, ""); return; }
  if (targets->GetSize() > 1024) { done(false, "Screenshot mask locator limit exceeded"); return; }
  auto work = std::make_shared<ArtifactMaskWork>(); work->owner=id;work->generation=operation->generation;
  work->config=config;work->done=std::move(done);work->targets=targets->Copy();work->boxes=CefListValue::Create();
  ArtifactMaskTarget(work);
}
void ArtifactMaskTarget(std::shared_ptr<ArtifactMaskWork> work) {
  if (!operations_.IsCurrent(work->owner,work->generation)) return;
  work->nodes.clear();work->node=0;
  if (work->target==work->targets->GetSize()) { ArtifactInstallMasks(work);return; }
  auto target=work->targets->GetDictionary(work->target++);
  if(!target){work->done(false,"Invalid native screenshot mask locator");return;}
  ResolveFramePath(work->owner,work->generation,target,0,FrameId(target),[this,work](Dict resolved){
    if(!operations_.IsCurrent(work->owner,work->generation))return;
    work->frame=FrameId(resolved);
    WithFrame(work->owner,work->frame,[this,work,resolved](int context){
      auto params=CefDictionaryValue::Create();params->SetInt("contextId",context);
      params->SetString("expression","(()=>{const a="+QueryExpression(resolved)+";if(a.length>1024)throw new Error('Screenshot mask node limit exceeded');return a})()");
      params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
      evaluation_groups_[work->owner].insert(operation_sessions_[work->owner]);
      auto callback=[this,work](bool ok,const std::string& json){
        if(!operations_.IsCurrent(work->owner,work->generation))return;
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        auto object=result?result->GetDictionary("result"):nullptr;
        if(!ok||!object||result->HasKey("exceptionDetails")||object->GetType("objectId")!=VTYPE_STRING){work->done(false,"Native screenshot mask query failed");return;}
        auto params=CefDictionaryValue::Create();params->SetString("objectId",object->GetString("objectId"));params->SetBool("ownProperties",true);
        DevTools(work->owner,"Runtime.getProperties",params,[this,work](bool valid,const std::string& json){
          if(!operations_.IsCurrent(work->owner,work->generation))return;
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
          auto properties=result?result->GetList("result"):nullptr;
          if(!valid||!properties||properties->GetSize()>1026){work->done(false,"Native screenshot mask result limit exceeded");return;}
          for(size_t i=0;i<properties->GetSize();++i){
            auto property=properties->GetDictionary(i);if(!property)continue;
            const auto name=property->GetString("name").ToString();
            if(name.empty()||!std::all_of(name.begin(),name.end(),[](unsigned char c){return c>='0'&&c<='9';}))continue;
            auto node=property->GetDictionary("value");
            if(!node||node->GetString("subtype")!="node"||node->GetType("objectId")!=VTYPE_STRING){work->done(false,"Screenshot mask query returned a non-element");return;}
            if(++work->total>1024){work->done(false,"Screenshot mask node limit exceeded");return;}
            work->nodes.push_back(node->GetString("objectId"));
          }
          ArtifactMaskNode(work);
        },std::nullopt,false,kMaxResponseBytes,true);
      };
      if(QueryUsesMultipleWorlds(resolved))QueryInspect(work->owner,resolved,"if(nodes.length>1024)throw new Error('Screenshot mask node limit exceeded');return nodes;",true,false,std::move(callback));
      else DevTools(work->owner,"Runtime.evaluate",params,std::move(callback));
    },!QueryNeedsMainWorld(resolved));
  });
}
void ArtifactMaskNode(std::shared_ptr<ArtifactMaskWork> work) {
  if(!operations_.IsCurrent(work->owner,work->generation))return;
  if(work->node==work->nodes.size()){ArtifactMaskTarget(work);return;}
  const auto node=work->nodes[work->node++];work->local.clear();work->mapped.clear();
  WithFrame(work->owner,work->frame,[this,work,node](int context){
    auto params=CefDictionaryValue::Create();params->SetString("objectId",node);
    DevTools(work->owner,"DOM.describeNode",params,[this,work,context](bool ok,const std::string& json){
      if(!operations_.IsCurrent(work->owner,work->generation))return;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      auto node=result?result->GetDictionary("node"):nullptr;
      if(!ok||!node||node->GetType("backendNodeId")!=VTYPE_INT){work->done(false,"Native screenshot mask node unavailable");return;}
      auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",node->GetInt("backendNodeId"));params->SetInt("executionContextId",context);
      params->SetString("objectGroup","suitecut-automation-"+std::to_string(work->owner));
      evaluation_groups_[work->owner].insert(operation_sessions_[work->owner]);
      DevTools(work->owner,"DOM.resolveNode",params,[this,work](bool valid,const std::string& json){
        if(!operations_.IsCurrent(work->owner,work->generation))return;
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        auto object=result?result->GetDictionary("object"):nullptr;
        if(!valid||!object||object->GetType("objectId")!=VTYPE_STRING){work->done(false,"Native screenshot mask utility node unavailable");return;}
        auto params=CefDictionaryValue::Create();params->SetString("objectId",object->GetString("objectId"));params->SetBool("returnByValue",true);
        params->SetString("functionDeclaration","function(){if(!this.isConnected)return null;const r=this.getBoundingClientRect();return r.width>0&&r.height>0?[r.left,r.top,r.right,r.bottom,innerWidth,innerHeight]:null}");
        DevTools(work->owner,"Runtime.callFunctionOn",params,[this,work](bool ready,const std::string& json){
          if(!operations_.IsCurrent(work->owner,work->generation))return;
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
          auto remote=result?result->GetDictionary("result"):nullptr;
          if(!ready||!remote||result->HasKey("exceptionDetails")){work->done(false,"Native screenshot mask geometry failed");return;}
          if(remote->GetString("subtype")=="null"){ArtifactMaskNode(work);return;}
          auto rect=remote->GetList("value");if(!rect||rect->GetSize()!=6){work->done(false,"Invalid native screenshot mask geometry");return;}
          for(size_t i=0;i<6;++i){
            if(rect->GetType(i)!=VTYPE_DOUBLE&&rect->GetType(i)!=VTYPE_INT){work->done(false,"Invalid native screenshot mask coordinates");return;}
            const double value=rect->GetType(i)==VTYPE_INT?rect->GetInt(i):rect->GetDouble(i);
            if(!std::isfinite(value)||std::abs(value)>100000000){work->done(false,"Screenshot mask coordinates exceed geometry bounds");return;}
            work->local.push_back(value);
          }
          work->viewport_width=work->local[4];work->viewport_height=work->local[5];
          ArtifactMapMaskCorner(work,0);
        });
      });
    });
  },true);
}
void ArtifactMapMaskCorner(std::shared_ptr<ArtifactMaskWork> work,size_t corner) {
  if(!operations_.IsCurrent(work->owner,work->generation))return;
  if(corner==4){
    double left=work->mapped[0],right=left,top=work->mapped[1],bottom=top;
    for(size_t i=2;i<8;i+=2){left=std::min(left,work->mapped[i]);right=std::max(right,work->mapped[i]);top=std::min(top,work->mapped[i+1]);bottom=std::max(bottom,work->mapped[i+1]);}
    auto box=CefDictionaryValue::Create();box->SetDouble("x",left);box->SetDouble("y",top);box->SetDouble("width",right-left);box->SetDouble("height",bottom-top);
    work->boxes->SetDictionary(work->boxes->GetSize(),box);ArtifactMaskNode(work);return;
  }
  const double x=work->local[(corner==0||corner==3)?0:2],y=work->local[corner<2?1:3];
  MapFramePoint(work->owner,work->frame,x,y,work->viewport_width,work->viewport_height,[this,work,corner](double x,double y){
    if(!operations_.IsCurrent(work->owner,work->generation))return;
    if(!std::isfinite(x)||!std::isfinite(y)){work->done(false,"Invalid transformed screenshot mask coordinates");return;}
    work->mapped.push_back(x);work->mapped.push_back(y);ArtifactMapMaskCorner(work,corner+1);
  });
}
void ArtifactInstallMasks(std::shared_ptr<ArtifactMaskWork> work) {
  auto value=CefValue::Create();value->SetList(work->boxes);const auto boxes=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();
  if(work->config->HasKey("maskColor")&&(work->config->GetType("maskColor")!=VTYPE_STRING||work->config->GetString("maskColor").length()>4096)){work->done(false,"Invalid native screenshot mask color");return;}
  auto color=CefValue::Create();color->SetString(work->config->HasKey("maskColor")?work->config->GetString("maskColor"):CefString("#FF00FF"));const auto quoted=CefWriteJSON(color,JSON_WRITER_DEFAULT).ToString();
  Evaluate(work->owner,"(async()=>{const color="+quoted+";if(!CSS.supports('color',color))throw new Error('Invalid screenshot maskColor');"
    "const m=globalThis.__suitecutArtifactStyles,resources=m?.get("+std::to_string(work->owner)+");if(!resources)throw new Error('Screenshot mask resources unavailable');"
    "for(const b of "+boxes+"){const e=document.createElement('div');e.style.cssText='all:initial!important;position:absolute!important;display:block!important;pointer-events:none!important;z-index:2147483647!important;';"
    "for(const [k,v] of Object.entries({left:b.x+scrollX,top:b.y+scrollY,width:b.width,height:b.height}))e.style.setProperty(k,v+'px','important');"
    "e.style.setProperty('background-color',color,'important');resources.masks.push(e);document.documentElement.append(e);}"
    "await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));return true})()",[this,work](bool ok,const std::string& json){
      if(!operations_.IsCurrent(work->owner,work->generation))return;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
      if(!ok||!result||result->HasKey("exceptionDetails")){work->done(false,"Native screenshot mask installation failed");return;}
      operation_frames_[work->owner]=main_frame_id_;operation_sessions_[work->owner]="";
      work->done(true,"");
    },main_frame_id_,true);
}
