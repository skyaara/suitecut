// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace suitecut::runner {
enum class SuiteHookKind { BeforeAll, BeforeEach, AfterEach, AfterAll };
struct SuiteHookRef { SuiteHookKind kind; uint64_t callback; };
struct SuiteDeclaration {
  uint64_t id, parent;
  uint32_t position;
  std::vector<SuiteHookRef> hooks;
};
struct SuiteTestDeclaration { uint64_t id, suite; uint32_t position; };
struct SuiteModifiers { bool only=false; std::optional<std::string> skip; };
// Pure native declaration and routing layer, not a host-callback ownership latch.
// The caller supplies actual HookWorker completion before retiring a suite route.
class NativeSuiteTree {
 public:
  struct Route {
    uint64_t test;
    std::vector<uint64_t> ancestors;
    std::vector<uint64_t> before_each, after_each, finish_suites;
  };
  NativeSuiteTree(std::vector<SuiteDeclaration> suites,
                  std::vector<SuiteTestDeclaration> tests) {
    if (suites.empty() || suites.size()>256 || tests.size()>8192)
      throw std::runtime_error("Native suite declaration capacity exceeded");
    std::set<uint64_t> callbacks;
    for (auto& suite:suites) {
      if (!suite.id || nodes_.contains(suite.id)) throw std::runtime_error("Duplicate or zero native suite ID");
      for (const auto& hook:suite.hooks) {
        if (!hook.callback || !callbacks.insert(hook.callback).second || callbacks.size()>8192)
          throw std::runtime_error("Duplicate or excessive native hook declaration");
        if (hook.kind!=SuiteHookKind::BeforeAll && hook.kind!=SuiteHookKind::BeforeEach &&
            hook.kind!=SuiteHookKind::AfterEach && hook.kind!=SuiteHookKind::AfterAll)
          throw std::runtime_error("Unknown native suite hook kind");
      }
      if (!suite.parent) {
        if (root_ || suite.position) throw std::runtime_error("Native suite requires one root at position zero");
        root_=suite.id;
      }
      nodes_.emplace(suite.id,std::move(suite));
    }
    if (!root_) throw std::runtime_error("Native suite root is missing");
    for (const auto& [id,node]:nodes_) {
      if (node.parent) {
        if (!nodes_.contains(node.parent)) throw std::runtime_error("Unknown native suite parent");
        children_[node.parent].push_back({true,id,node.position});
      }
      std::set<uint64_t> seen;
      auto parent=id;
      while(parent) {
        if(!nodes_.contains(parent)) throw std::runtime_error("Unknown native suite ancestor");
        if (!seen.insert(parent).second || seen.size()>64)
          throw std::runtime_error("Native suite cycle or depth limit exceeded");
        parent=nodes_.at(parent).parent;
      }
    }
    for(const auto& test:tests) {
      if (!test.id || !nodes_.contains(test.suite) || !tests_.emplace(test.id,test).second)
        throw std::runtime_error("Invalid native suite test declaration");
      children_[test.suite].push_back({false,test.id,test.position});
    }
    for(auto& [parent,entries]:children_) {
      (void)parent;
      std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.position<b.position;});
      for(size_t index=0;index<entries.size();++index)
        if(entries[index].position!=index) throw std::runtime_error("Native suite declaration positions must be contiguous and unique");
    }
    Append(root_);
    if(order_.size()!=tests_.size()) throw std::runtime_error("Native suite declarations are disconnected");
  }
  const std::vector<uint64_t>& TestOrder() const { return order_; }
  struct SelectionModifiers {
    bool has_focus=false;
    std::vector<uint64_t> focused;
    std::map<uint64_t,std::string> skipped;
  };
  SelectionModifiers ResolveModifiers(const std::map<uint64_t,SuiteModifiers>& modifiers,
                                     const std::set<uint64_t>& focused,
                                     const std::map<uint64_t,std::string>& skipped) const {
    for(const auto& [id,value]:modifiers){(void)value;if(!nodes_.contains(id))throw std::runtime_error("Unknown native suite modifier owner");}
    for(const auto id:focused)if(!tests_.contains(id))throw std::runtime_error("Unknown focused native suite test");
    for(const auto& [id,value]:skipped){(void)value;if(!tests_.contains(id))throw std::runtime_error("Unknown skipped native suite test");}
    SelectionModifiers result;result.skipped=skipped;
    // The nearest explicit reason wins. A test-level false cannot undo an
    // enclosing static skip, and an empty string is still an actual reason.
    for(const auto& [id,test]:tests_)if(!result.skipped.contains(id)){
      auto suite=test.suite;
      while(suite){const auto found=modifiers.find(suite);if(found!=modifiers.end()&&found->second.skip){result.skipped.emplace(id,*found->second.skip);break;}suite=nodes_.at(suite).parent;}
    }
    const auto only=[&](uint64_t suite){const auto found=modifiers.find(suite);return found!=modifiers.end()&&found->second.only;};
    std::map<uint64_t,bool> contains_focus;
    std::function<bool(uint64_t)> inspect=[&](uint64_t suite){
      bool found=only(suite);
      if(const auto children=children_.find(suite);children!=children_.end())
        for(const auto& child:children->second){const bool nested=child.suite?inspect(child.id):focused.contains(child.id);found=found||nested;}
      contains_focus.emplace(suite,found);return found;
    };
    result.has_focus=inspect(root_);
    if(!result.has_focus)return result;
    std::function<void(uint64_t,bool)> collect=[&](uint64_t suite,bool inherited){
      const auto children=children_.find(suite);if(children==children_.end())return;
      bool descendant_focus=false;
      for(const auto& child:children->second)
        descendant_focus|=child.suite?contains_focus.at(child.id):focused.contains(child.id);
      for(const auto& child:children->second){
        const bool selected=descendant_focus?(child.suite?contains_focus.at(child.id):focused.contains(child.id)):(inherited||only(suite));
        if(!selected)continue;
        if(child.suite)collect(child.id,inherited||only(suite));else result.focused.push_back(child.id);
      }
    };
    collect(root_,false);return result;
  }
  std::set<uint64_t> SuiteTests(uint64_t suite) const {
    if(!nodes_.contains(suite))throw std::runtime_error("Unknown native suite ID");
    std::set<uint64_t> result;
    for(const auto& [id,test]:tests_){auto parent=test.suite;while(parent){if(parent==suite){result.insert(id);break;}parent=nodes_.at(parent).parent;}}
    return result;
  }

  std::vector<uint64_t> Hooks(uint64_t suite,SuiteHookKind kind) const {
    std::vector<uint64_t> result;
    for(const auto& hook:nodes_.at(suite).hooks)if(hook.kind==kind)result.push_back(hook.callback);
    return result;
  }
  std::vector<Route> Plan(const std::set<uint64_t>& selected) const {
    if(selected.size()>8192)throw std::runtime_error("Native suite selection capacity exceeded");
    for(auto test:selected)if(!tests_.contains(test))throw std::runtime_error("Unknown selected native suite test ID");
    std::vector<Route> routes;
    std::map<uint64_t,size_t> last;
    size_t references=0;
    const auto charge=[&](size_t count){
      if(count>1048576-references)throw std::runtime_error("Native suite route reference budget exceeded");
      references+=count;
    };
    for(const auto id:order_)if(selected.contains(id)) {
      Route route{id,{},{},{},{}};
      auto suite=tests_.at(id).suite;
      while(suite){route.ancestors.push_back(suite);suite=nodes_.at(suite).parent;}
      charge(route.ancestors.size()+1);
      std::reverse(route.ancestors.begin(),route.ancestors.end());
      for(auto owner:route.ancestors){
        const auto hooks=Hooks(owner,SuiteHookKind::BeforeEach);
        charge(hooks.size());
        route.before_each.insert(route.before_each.end(),hooks.begin(),hooks.end());
        last[owner]=routes.size();
      }
      for(auto it=route.ancestors.rbegin();it!=route.ancestors.rend();++it){
        const auto hooks=Hooks(*it,SuiteHookKind::AfterEach);
        charge(hooks.size());
        route.after_each.insert(route.after_each.end(),hooks.begin(),hooks.end());
      }
      routes.push_back(std::move(route));
    }
    for(size_t index=0;index<routes.size();++index)
      for(auto it=routes[index].ancestors.rbegin();it!=routes[index].ancestors.rend();++it)
        if(last.at(*it)==index){charge(1);routes[index].finish_suites.push_back(*it);}
    return routes;
  }
 private:
  struct Child {bool suite;uint64_t id;uint32_t position;};
  void Append(uint64_t suite){for(const auto& child:children_[suite]){if(child.suite)Append(child.id);else order_.push_back(child.id);}}
  uint64_t root_=0;
  std::map<uint64_t,SuiteDeclaration> nodes_;
  std::map<uint64_t,SuiteTestDeclaration> tests_;
  std::map<uint64_t,std::vector<Child>> children_;
  std::vector<uint64_t> order_;
};
}
