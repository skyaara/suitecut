// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_graph.h"
namespace suitecut::runner {
// Raw declaration records. IDs are callback declaration positions, not runtime
// callback leases; FixtureOwners supplies real tokens/attempt/worker ownership.
struct FixtureDeclaration {
  uint64_t id=0;std::string name;std::vector<std::string> dependencies;
  std::optional<Scope> scope=std::nullopt;std::optional<bool> automatic=std::nullopt;
};
class FixtureCatalog {
 public:
  static constexpr size_t kLayers=32,kDeclarations=256,kEdges=4096,kMetadata=2*1024*1024;
  using Layer=std::vector<FixtureDeclaration>;
  struct Binding {std::string name,key;uint64_t declaration;bool operator==(const Binding&)const=default;};
  struct Version {
    uint64_t id=0,layer=0,previous=0;std::string name,key;Scope scope=Scope::Test;bool automatic=false,active=false;
    std::vector<std::string> dependencies;std::vector<Binding> bindings;
  };
  explicit FixtureCatalog(std::vector<Layer> layers){
    if(layers.empty()||layers.size()>kLayers)throw std::invalid_argument("Native fixture layer capacity rejected");
    size_t edges=0,bytes=0;uint64_t expected=0;
    for(size_t index=0;index<layers.size();++index){std::set<std::string> names;const auto previous=effective_;
      for(auto& input:layers[index]){
        if(versions_.size()>=kDeclarations||input.id!=++expected||!Name(input.name)||!names.insert(input.name).second||(input.scope&&*input.scope!=Scope::Test&&*input.scope!=Scope::Worker))throw std::invalid_argument("Native fixture declaration identity/name/scope rejected");
        if(input.dependencies.size()>kDeclarations||input.dependencies.size()>kEdges-edges)throw std::invalid_argument("Native fixture dependency edge capacity exceeded");
        edges+=input.dependencies.size();
        Charge(bytes,input.name.size()+128);for(const auto& dependency:input.dependencies){if(!Name(dependency))throw std::invalid_argument("Native fixture dependency name rejected");Charge(bytes,dependency.size()+16);}
        Version version;version.id=input.id;version.layer=index+1;version.name=input.name;version.key="nativeFixture"+std::to_string(input.id);version.dependencies=std::move(input.dependencies);
        if(const auto found=previous.find(input.name);found!=previous.end()){
          const auto& base=Get(found->second);version.previous=base.id;version.scope=base.scope;version.automatic=base.automatic;
          if(input.scope&&*input.scope!=base.scope)throw std::invalid_argument("Native fixture override changes declared scope: "+input.name);
        }else visible_order_.push_back(input.name);
        if(input.scope)version.scope=*input.scope;
        if(input.automatic)version.automatic=*input.automatic;
        versions_.push_back(std::move(version));effective_[input.name]=input.id;
      }
    }
    // Resolve only genuinely effective roots and their actual previous versions.
    // A fully shadowed callback is not turned into a synthetic execution node.
    std::map<uint64_t,unsigned> seen;for(const auto&[name,id]:effective_){(void)name;Resolve(id,seen);}
    // Overriding a fixture preserves its author-name slot among independent
    // automatic fixtures. Previous versions remain dependency-only nodes.
    auto append=[this](const auto& version,bool automatic){std::vector<std::string> dependencies;for(const auto& binding:version.bindings)dependencies.push_back(binding.key);definitions_.push_back({version.key,version.scope,std::move(dependencies),automatic});};
    for(const auto& name:visible_order_){const auto& version=Get(effective_.at(name));append(version,version.automatic);}
    for(const auto& version:versions_)if(version.active&&effective_.at(version.name)!=version.id)append(version,false);
    (void)FixtureGraph(definitions_); // Validate the exact native scheduling graph.
  }
  const Version& Get(uint64_t id)const{if(!id||id>versions_.size()||versions_[id-1].id!=id)throw std::invalid_argument("Unknown native fixture declaration identity");return versions_[id-1];}
  const Version& Effective(const std::string& name)const{const auto found=effective_.find(name);if(found==effective_.end())throw std::invalid_argument("Unknown native fixture: "+name);return Get(found->second);}
  const std::vector<Version>& Versions()const{return versions_;}
  const std::map<std::string,uint64_t>& Visible()const{return effective_;}
  const std::vector<Fixture>& Definitions()const{return definitions_;}
  std::vector<std::string> Roots(const std::vector<std::string>& names)const{if(names.size()>kDeclarations)throw std::invalid_argument("Native requested fixture capacity exceeded");std::vector<std::string> roots;roots.reserve(names.size());for(const auto& name:names)roots.push_back(Effective(name).key);return roots;}
  static bool Name(const std::string& name){if(name.empty()||name.size()>256)return false;for(size_t i=0;i<name.size();++i){const unsigned char c=name[i];if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||(i&&c>='0'&&c<='9'))continue;return false;}return true;}
 private:
  static void Charge(size_t& bytes,size_t add){if(add>kMetadata-bytes)throw std::length_error("Native fixture declaration metadata capacity exceeded");bytes+=add;}
  void Resolve(uint64_t id,std::map<uint64_t,unsigned>& seen){
    if(seen[id]==2)return;
    if(seen[id]==1)throw std::invalid_argument("Native fixture dependency cycle at: "+Get(id).name);
    seen[id]=1;
    auto& version=versions_.at(id-1);version.active=true;std::set<std::string> bound;
    for(const auto& name:version.dependencies){if(!bound.insert(name).second)continue;
      uint64_t target=0;if(name==version.name){if(!version.previous)throw std::invalid_argument("Native fixture self dependency has no previous declaration: "+name);target=version.previous;}
      else{const auto found=effective_.find(name);if(found==effective_.end())throw std::invalid_argument("Unknown native fixture dependency: "+name);target=found->second;}
      const auto& dependency=Get(target);if(version.scope==Scope::Worker&&dependency.scope==Scope::Test)throw std::invalid_argument("Native worker fixture "+version.name+" cannot depend on test fixture "+name);
      version.bindings.push_back({name,dependency.key,target});Resolve(target,seen);
    }
    seen[id]=2;
  }
  std::vector<Version> versions_;std::map<std::string,uint64_t> effective_;std::vector<std::string> visible_order_;std::vector<Fixture> definitions_;
};
}
