// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include "query_node_transfer.h"

// Native scheduler for selector plans whose custom engines require different
// execution worlds. The renderer receives one atomic authored query step at a
// time; C++ retains scopes, combines nodes, transfers backend identities, and
// owns every asynchronous continuation through the original operation.
class SuiteCutQueryWorldPlan : public std::enable_shared_from_this<SuiteCutQueryWorldPlan> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using List=CefRefPtr<CefListValue>;
  using Send=SuiteCutQueryNodeTransfer::Send;
  using Current=SuiteCutQueryNodeTransfer::Current;
  using Fail=SuiteCutQueryNodeTransfer::Fail;
  struct Nodes {std::string object;int context=0;std::shared_ptr<void> lease;};
  using Done=std::function<void(Nodes)>;
  using Reference=std::function<void(const std::string&,Nodes,std::function<void(std::string)>)>;
  using Isolated=std::function<bool(const std::string&)>;
  static void Run(Send send,Current current,Fail fail,int main,int utility,
                  std::string group,std::string helper,std::string options,
                  List plan,Isolated isolated,Done done,bool grouped=false,std::string root={},Reference reference={}){
    // CEF child list views are owned by their response dictionary. Retain an
    // independent native plan before the first asynchronous continuation.
    plan=plan?plan->Copy():nullptr;
    auto work=std::shared_ptr<SuiteCutQueryWorldPlan>(new SuiteCutQueryWorldPlan);
    work->send_=std::move(send);work->current_=std::move(current);work->fail_=std::move(fail);
    work->grouped_=grouped;work->main_=main;work->utility_=utility;work->group_=std::move(group);
    work->helper_=std::move(helper);work->options_=std::move(options);work->isolated_=std::move(isolated);work->reference_=std::move(reference);
    auto params=CefDictionaryValue::Create();const auto method=root.empty()?"Runtime.evaluate":"Runtime.callFunctionOn";
    if(root.empty()){params->SetString("expression","[document]");params->SetInt("contextId",utility);}
    else{params->SetString("objectId",root);params->SetString("functionDeclaration","function(){return [this];}");}
    params->SetString("objectGroup",work->group_);params->SetBool("returnByValue",false);
    work->Request(method,params,[work,plan,done=std::move(done)](Dict result)mutable{
      auto remote=result->GetDictionary("result");
      if(!remote||remote->GetType("objectId")!=VTYPE_STRING){work->Error("Native mixed-world selector root unavailable");return;}
      work->Sequence(plan,work->Track(remote->GetString("objectId").ToString(),work->utility_),0,std::move(done));
    });
  }
 private:
  using Response=std::function<void(Dict)>;
  using NumberDone=std::function<void(int)>;
  struct SequenceState {List plan;Nodes scope,nodes;size_t index=0;int depth=0;Done done;};
  struct RootsState {Nodes source,output;int length=0,index=0,depth=0;List query,descendants;std::string mode;Done done;};
  bool Valid(){if(failed_)return false;if(!current_()){Error("Native query document changed during mixed-world execution");return false;}return true;}
  void Error(const std::string& message){if(failed_)return;failed_=true;fail_(message);}
  static Dict Parse(const std::string& json){auto value=CefParseJSON(json,JSON_PARSER_RFC);return value?value->GetDictionary():nullptr;}
  static std::string Json(List value){auto wrapper=CefValue::Create();wrapper->SetList(value);return CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString();}
  Nodes Track(std::string object,int context){
    auto self=shared_from_this();auto lease=std::shared_ptr<void>(new std::string(object),[self](void* value){
      std::unique_ptr<std::string> object(static_cast<std::string*>(value));
      if(self->grouped_||self->failed_||!self->current_())return;
      auto params=CefDictionaryValue::Create();params->SetString("objectId",*object);
      self->send_("Runtime.releaseObject",params,[](bool,const std::string&){});
    });
    return {std::move(object),context,std::move(lease)};
  }
  void RawRequest(const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result done){
    if(!Valid())return;
    if(++requests_>100000){Error("Native mixed-world selector work limit exceeded");return;}
    send_(method,params,std::move(done));
  }
  void Request(const std::string& method,Dict params,Response done){
    auto self=shared_from_this();RawRequest(method,params,[self,done=std::move(done)](bool ok,const std::string& json)mutable{
      if(!self->Valid())return;
      auto result=Parse(json);
      if(!ok||!result||result->HasKey("exceptionDetails")){
        auto details=result?result->GetDictionary("exceptionDetails"):nullptr;auto exception=details?details->GetDictionary("exception"):nullptr;
        self->Error(exception&&exception->GetType("description")==VTYPE_STRING?exception->GetString("description").ToString():"Native mixed-world selector command failed");return;
      }
      done(result);
    });
  }
  void Call(Nodes receiver,const std::string& function,List args,bool by_value,Response done){
    auto params=CefDictionaryValue::Create();params->SetString("objectId",receiver.object);params->SetString("functionDeclaration",function);
    params->SetBool("returnByValue",by_value);params->SetString("objectGroup",group_);
    if(args)params->SetList("arguments",args);
    Request("Runtime.callFunctionOn",params,std::move(done));
  }
  void Array(Nodes receiver,const std::string& function,List args,Done done){
    auto self=shared_from_this();Call(receiver,function,args,false,[self,context=receiver.context,done=std::move(done)](Dict result)mutable{
      auto array=result->GetDictionary("result");
      if(!array||array->GetString("subtype")!="array"||array->GetType("objectId")!=VTYPE_STRING){self->Error("Native mixed-world selector array unavailable");return;}
      done(self->Track(array->GetString("objectId").ToString(),context));
    });
  }
  void Length(Nodes nodes,NumberDone done){
    auto self=shared_from_this();Call(nodes,"function(){return this.length;}",nullptr,true,[self,done=std::move(done)](Dict result)mutable{
      auto value=result->GetDictionary("result");
      if(!value||value->GetType("value")!=VTYPE_INT||value->GetInt("value")<0||value->GetInt("value")>100000){self->Error("Native mixed-world selector node limit exceeded");return;}
      done(value->GetInt("value"));
    });
  }
  void World(Nodes nodes,int destination,Done done){
    if(!Valid())return;
    if(nodes.context==destination){done(nodes);return;}
    auto self=shared_from_this();SuiteCutQueryNodeTransfer::Transfer(
      [self](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result result){self->RawRequest(method,params,std::move(result));},
      [self]{return self->Valid();},[self](const std::string& error){self->Error(error);},nodes.object,destination,group_,
      [self,destination,done=std::move(done)](const std::string& object)mutable{done(self->Track(object,destination));});
  }
  void Sequence(List plan,Nodes scope,int depth,Done done){
    if(!Valid())return;
    if(depth>64||!plan||plan->GetSize()>65){Error("Native locator query nesting limit exceeded");return;}
    auto work=std::make_shared<SequenceState>();work->plan=plan;work->scope=work->nodes=scope;work->depth=depth;work->done=std::move(done);Next(work);
  }
  void Next(const std::shared_ptr<SequenceState>& work){
    if(!Valid())return;
    if(work->index==work->plan->GetSize()){work->done(work->nodes);return;}
    auto step=work->plan->GetDictionary(work->index++);
    if(!step||step->GetType("engine")!=VTYPE_STRING){Error("Invalid native locator query step");return;}
    const auto engine=step->GetString("engine").ToString();auto self=shared_from_this();
    Done next=[self,work](Nodes nodes){work->nodes=std::move(nodes);self->Next(work);};
    if(engine=="and"||engine=="or"){
      Sequence(step->GetList("query"),work->scope,work->depth+1,[self,left=work->nodes,engine,next=std::move(next)](Nodes right)mutable{
        self->Combine(left,right,engine,std::move(next));
      });return;
    }
    if(engine=="chain"||engine=="capture"){
      PerRoot(work->nodes,step->GetList("query"),engine=="capture"?step->GetList("descendants"):nullptr,
              work->depth+1,engine,std::move(next));return;
    }
    if(engine=="filter"&&(step->HasKey("has")||step->HasKey("hasNot"))){
      auto simple=step->Copy(false);simple->Remove("has");simple->Remove("hasNot");
      Atomic(simple,work->nodes,[self,step,depth=work->depth,next=std::move(next)](Nodes filtered)mutable{
        Done has_not=[self,step,depth,next=std::move(next)](Nodes filtered)mutable{
          if(step->HasKey("hasNot"))self->PerRoot(filtered,step->GetList("hasNot"),nullptr,depth+1,"hasNot",std::move(next));else next(filtered);
        };
        if(step->HasKey("has"))self->PerRoot(filtered,step->GetList("has"),nullptr,depth+1,"has",std::move(has_not));else has_not(filtered);
      });return;
    }
    Atomic(step,work->nodes,std::move(next));
  }
  void Atomic(Dict step,Nodes input,Done done){
    const auto engine=step->GetString("engine").ToString();auto self=shared_from_this();
    if(engine=="reference"){
      if(!reference_||step->GetType("ref")!=VTYPE_STRING){Error("Native snapshot reference resolver unavailable");return;}
      const auto token=step->GetString("ref").ToString();
      World(input,utility_,[self,token,done=std::move(done)](Nodes scope)mutable{
        self->reference_(token,scope,[self,done=std::move(done)](std::string object)mutable{
          if(!self->Valid())return;
          if(object.empty()){self->Error("Native snapshot reference resolved without actual nodes");return;}
          done(self->Track(std::move(object),self->utility_));
        });
      });return;
    }
    if(engine=="selector-wait"){
      const auto state=step->GetString("state").ToString();
      if((state!="attached"&&state!="visible")||step->GetType("strict")!=VTYPE_BOOL){Error("Invalid native selector-wait plan");return;}
      const auto guard=step->GetBool("strict")?"if(this.length>1)throw new Error('strict mode violation: locator resolved to '+this.length+' elements');":"";
      Array(input,"function(){"+std::string(guard)+"return this.length?[this[0]]:[];}",nullptr,
        [self,state,done=std::move(done)](Nodes selected)mutable{
          if(state=="attached"){done(selected);return;}
          auto filter=CefDictionaryValue::Create();filter->SetString("engine","filter");filter->SetBool("visible",true);
          self->Atomic(filter,selected,std::move(done));
        });return;
    }
    if(engine=="nth"){
      if(step->GetType("index")!=VTYPE_INT){Error("Invalid native locator index");return;}
      Array(input,"function(){const index="+std::to_string(step->GetInt("index"))+";const at=index<0?this.length+index:index;return at>=0&&at<this.length?[this[at]]:[];}",nullptr,std::move(done));return;
    }
    const int realm=engine=="custom"&&!isolated_(step->GetString("name").ToString())?main_:utility_;
    World(input,realm,[self,step,done=std::move(done)](Nodes nodes)mutable{
      auto list=CefListValue::Create();list->SetDictionary(0,step);
      self->Array(nodes,"function(){const out=new Set();for(let i=0;i<this.length;i++){const matches=("+self->helper_+")("+Json(list)+",this[i],"+self->options_+");for(const node of matches){out.add(node);if(out.size>100000)throw new Error('Native DOM query node limit exceeded');}}return [...out];}",nullptr,std::move(done));
    });
  }
  void Combine(Nodes left,Nodes right,const std::string& mode,Done done){
    auto self=shared_from_this();World(right,left.context,[self,left,mode,done=std::move(done)](Nodes right)mutable{
      auto args=CefListValue::Create();auto argument=CefDictionaryValue::Create();argument->SetString("objectId",right.object);args->SetDictionary(0,argument);
      const auto body=mode=="and"?"const members=new Set(other);const out=[];for(const node of this)if(members.has(node))out[out.length]=node;":"const found=new Set(this);for(const node of other)found.add(node);const out=[...found];";
      self->Array(left,"function(other){"+std::string(body)+"if(out.length>100000)throw new Error('Native DOM query node limit exceeded');out.sort((a,b)=>a===b?0:a.compareDocumentPosition(b)&Node.DOCUMENT_POSITION_PRECEDING?1:-1);return out;}",args,std::move(done));
    });
  }
  void Append(Nodes output,Nodes input,Done done){
    auto self=shared_from_this();World(input,output.context,[self,output,done=std::move(done)](Nodes input)mutable{
      auto args=CefListValue::Create();auto argument=CefDictionaryValue::Create();argument->SetString("objectId",input.object);args->SetDictionary(0,argument);
      self->Call(output,"function(other){const seen=new Set(this);for(const node of other)if(!seen.has(node)){seen.add(node);this[this.length]=node;if(this.length>100000)throw new Error('Native DOM query node limit exceeded');}}",args,true,[output,done=std::move(done)](Dict)mutable{done(output);});
    });
  }
  void PerRoot(Nodes input,List query,List descendants,int depth,std::string mode,Done done){
    auto self=shared_from_this();Length(input,[self,input,query,descendants,depth,mode=std::move(mode),done=std::move(done)](int length)mutable{
      self->Array(input,"function(){return [];}",nullptr,[self,input,query,descendants,depth,length,mode=std::move(mode),done=std::move(done)](Nodes output)mutable{
        auto work=std::make_shared<RootsState>();work->source=input;work->output=output;work->query=query;work->descendants=descendants;
        work->depth=depth;work->length=length;work->mode=std::move(mode);work->done=std::move(done);self->RootNext(work);
      });
    });
  }
  void RootNext(const std::shared_ptr<RootsState>& work){
    if(!Valid())return;
    if(work->index==work->length){work->done(work->output);return;}
    auto self=shared_from_this();Array(work->source,"function(){return [this["+std::to_string(work->index++)+"]];}",nullptr,[self,work](Nodes root){
      self->Sequence(work->query,root,work->depth,[self,work,root](Nodes matches){
        if(work->mode=="chain"){self->Append(work->output,matches,[self,work](Nodes output){work->output=output;self->RootNext(work);});return;}
        if(work->mode=="capture"){
          if(!work->descendants||!work->descendants->GetSize()){self->Append(work->output,matches,[self,work](Nodes output){work->output=output;self->RootNext(work);});return;}
          self->PerRoot(matches,work->descendants,nullptr,work->depth+1,"has",[self,work](Nodes captured){self->Append(work->output,captured,[self,work](Nodes output){work->output=output;self->RootNext(work);});});return;
        }
        self->Length(matches,[self,work,root](int size){
          const bool include=work->mode=="hasNot"?size==0:size>0;
          if(include)self->Append(work->output,root,[self,work](Nodes output){work->output=output;self->RootNext(work);});else self->RootNext(work);
        });
      });
    });
  }
  Reference reference_;
  Send send_;Current current_;Fail fail_;Isolated isolated_;int main_=0,utility_=0;size_t requests_=0;bool failed_=false,grouped_=false;
  std::string group_,helper_,options_;
};
