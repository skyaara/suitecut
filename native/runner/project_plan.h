// SPDX-License-Identifier: MIT
#pragma once
#include "project_pattern.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace suitecut::runner {
struct ProjectDeclaration {
  std::string name;
  std::vector<std::string> dependencies{};
  std::optional<std::string> teardown=std::nullopt;
  bool default_enabled=true;
};
// Declarative graph only. Callback/fixture admission, clocks and OS process
// ownership stay native; the callback host never computes this order.
class ProjectGraph {
 public:
  static constexpr size_t kProjects=128,kEdges=4096;
  explicit ProjectGraph(std::vector<ProjectDeclaration> declarations):declarations_(std::move(declarations)) {
    if(declarations_.empty()||declarations_.size()>kProjects)throw std::invalid_argument("Native projects require1..128 declarations");
    for(size_t i=0;i<declarations_.size();++i){const auto& name=declarations_[i].name;
      if(name.empty()||name.size()>256||name.find('\0')!=std::string::npos||!names_.emplace(name,i).second)
        throw std::invalid_argument("Native project names must be unique nonempty strings up to256 bytes");
    }
    dependencies_.resize(declarations_.size());teardown_.resize(declarations_.size());size_t edges=0;
    for(size_t i=0;i<declarations_.size();++i){std::set<size_t> seen;
      for(const auto& name:declarations_[i].dependencies){const auto target=Index(name);
        if(target==i||!seen.insert(target).second||++edges>kEdges)throw std::invalid_argument("Native project dependency identity/capacity rejected");
        dependencies_[i].push_back(target);
      }
      if(declarations_[i].teardown){const auto target=Index(*declarations_[i].teardown);
        if(target==i)throw std::invalid_argument("Native project cannot tear itself down");
        teardown_[i]=target;teardowns_.insert(target);
      }
    }
    for(const auto target:teardowns_)
      if(!dependencies_[target].empty()||teardown_[target])throw std::invalid_argument("Native teardown projects cannot declare dependencies or another teardown in this serial slice");
    for(const auto& dependencies:dependencies_)for(const auto target:dependencies)
      if(teardowns_.contains(target))throw std::invalid_argument("Native projects cannot depend on a teardown project");
    std::vector<unsigned> colors(declarations_.size());
    std::function<void(size_t)> visit=[&](size_t index){if(colors[index]==1)throw std::invalid_argument("Native project dependency cycle");if(colors[index]==2)return;colors[index]=1;for(const auto dep:dependencies_[index])visit(dep);colors[index]=2;};
    for(size_t i=0;i<declarations_.size();++i)visit(i);
  }
  size_t Index(const std::string& name)const {const auto found=names_.find(name);if(found==names_.end())throw std::invalid_argument("Unknown native project: "+name);return found->second;}
  const std::vector<ProjectDeclaration>& Declarations()const{return declarations_;}
  const std::vector<size_t>& Dependencies(size_t index)const{return dependencies_.at(index);}
  bool IsTeardown(size_t index)const{return teardowns_.contains(index);}
  std::optional<size_t> Teardown(size_t index)const{return teardown_.at(index);}
  std::vector<bool> Roots(const std::vector<std::string>& patterns={})const {
    if(patterns.size()>kProjects)throw std::invalid_argument("Native project selector capacity128 exceeded");
    std::vector<bool> roots(declarations_.size());
    if(patterns.empty()){for(size_t i=0;i<declarations_.size();++i)roots[i]=declarations_[i].default_enabled&&!IsTeardown(i);return roots;}
    bool any=false;
    for(const auto& pattern:patterns){ProjectPattern matcher(pattern);bool matched=false;
      for(size_t i=0;i<declarations_.size();++i)if(matcher.Matches(declarations_[i].name)){roots[i]=true;matched=true;any=true;}
      if(!matched&&!matcher.Wildcard())throw std::invalid_argument(Available("Native project selector not found: "+pattern+"."));
    }
    if(!any)throw std::invalid_argument(Available("No native projects matched."));
    return roots;
  }
  std::vector<bool> Closure(const std::vector<bool>& roots,bool no_dependencies=false)const {
    if(roots.size()!=declarations_.size())throw std::invalid_argument("Native project selection mask shape rejected");
    std::vector<bool> selected(declarations_.size());
    std::function<void(size_t)> include=[&](size_t index){if(selected[index])return;selected[index]=true;if(no_dependencies)return;for(const auto dep:dependencies_[index])include(dep);if(teardown_[index])include(*teardown_[index]);};
    for(size_t i=0;i<roots.size();++i)if(roots[i])include(i);
    return selected;
  }
  std::vector<bool> Select(const std::vector<std::string>& patterns={},bool no_dependencies=false)const {return Closure(Roots(patterns),no_dependencies);}

 private:
  std::string Available(std::string message)const {
    message+=" Available projects: ";bool first=true;
    for(const auto& declaration:declarations_){if(message.size()+declaration.name.size()+4>7000){message+="... (available project list truncated)";break;}if(!first)message+=", ";first=false;message+='"'+declaration.name+'"';}return message;
  }
  std::vector<ProjectDeclaration> declarations_;std::map<std::string,size_t> names_;
  std::vector<std::vector<size_t>> dependencies_;std::vector<std::optional<size_t>> teardown_;std::set<size_t> teardowns_;
};
struct ProjectPlanSelection {std::vector<bool> selected;bool no_dependencies=false;};
class SerialProjectPlan {
 public:
  enum class State {NotSelected,Pending,Running,Passed,Failed,Blocked,Interrupted,NotRun};
  enum class Outcome {Passed,Failed,Interrupted};
  struct Result {State state=State::NotSelected;std::optional<size_t> dependency=std::nullopt;std::string reason{};};
  struct Admission {size_t index;uint64_t generation;bool teardown;};
  explicit SerialProjectPlan(ProjectGraph graph,const std::vector<std::string>& names={},bool no_dependencies=false):graph_(std::move(graph)),results_(graph_.Declarations().size()),no_dependencies_(no_dependencies) {
    Initialize(graph_.Select(names,no_dependencies));
  }
  SerialProjectPlan(ProjectGraph graph,ProjectPlanSelection selection):graph_(std::move(graph)),results_(graph_.Declarations().size()),no_dependencies_(selection.no_dependencies) {
    Initialize(selection.selected);
  }
  std::optional<Admission> Next(){
    if(active_||stopped_)return std::nullopt;
    // Stable declaration order among ready ordinary projects. Failed ancestors
    // block declarations without inventing attempts, callbacks or worker PIDs.
    bool blocked=false;
    for(size_t index=0;index<results_.size();++index){if(results_[index].state!=State::Pending||IsTeardown(index))continue;
      bool waiting=false;std::optional<size_t> failed;
      if(!no_dependencies_)for(const auto dep:graph_.Dependencies(index)){const auto state=results_[dep].state;
        if(state==State::Pending||state==State::Running)waiting=true;
        else if(state!=State::Passed&&!failed)failed=dep;
      }
      if(failed){results_[index]={State::Blocked,failed,"Native project dependency did not pass"};blocked=true;continue;}
      if(!waiting)return Admit(index);
    }
    if(blocked)return Next();
    // This conservative serial slice runs teardown after ALL selected ordinary
    // projects have settled, which includes every selected transitive dependent.
    for(size_t index=0;index<results_.size();++index)
      if(!IsTeardown(index)&&(results_[index].state==State::Pending||results_[index].state==State::Running))return std::nullopt;
    for(size_t index=0;index<results_.size();++index)if(IsTeardown(index)&&results_[index].state==State::Pending)return Admit(index);
    return std::nullopt;
  }
  bool WorkerStarted(uint64_t generation,int64_t pid){
    if(!Current(generation)||pid<=0||active_->worker||active_->outcome)return false;
    active_->worker=pid;active_->started=true;return true;
  }
  bool WorkerExited(uint64_t generation,int64_t pid){
    if(!Current(generation)||!active_->worker||*active_->worker!=pid)return false;
    active_->worker.reset();Retire();return true;
  }
  bool Complete(uint64_t generation,Outcome outcome){
    if(!Current(generation)||active_->outcome||!active_->started)return false;
    active_->outcome=outcome;Retire();return true;
  }
  // Global deadline, failure-limit and interruption policies stop new author
  // admissions, including teardown. They never release a live physical worker.
  void Stop(const std::string& reason){
    if(reason.size()>8192)throw std::invalid_argument("Native project stop reason capacity exceeded");
    if(stopped_)return;
    stopped_=true;
    if(active_&&!active_->started){results_[active_->admission.index]={State::NotRun,std::nullopt,reason};active_.reset();}
    for(auto& result:results_)if(result.state==State::Pending)result={State::NotRun,std::nullopt,reason};
  }
  bool Done()const {return !active_&&std::none_of(results_.begin(),results_.end(),[](const auto& result){return result.state==State::Pending||result.state==State::Running;});}
  bool Physical()const{return active_&&active_->worker.has_value();}
  const std::vector<Result>& Results()const{return results_;}
  const ProjectGraph& Graph()const{return graph_;}
 private:
  bool IsTeardown(size_t index)const{return !no_dependencies_&&graph_.IsTeardown(index);}
  void Initialize(const std::vector<bool>& selected){
    if(selected.size()!=results_.size())throw std::invalid_argument("Native project plan selection shape rejected");
    if(!no_dependencies_&&graph_.Closure(selected)!=selected)throw std::invalid_argument("Native project plan selection omits dependency/teardown closure");
    for(size_t i=0;i<selected.size();++i)if(selected[i])results_[i].state=State::Pending;
  }
  struct Active {Admission admission;std::optional<int64_t> worker=std::nullopt;std::optional<Outcome> outcome=std::nullopt;bool started=false;};
  bool Current(uint64_t generation)const{return active_&&generation&&active_->admission.generation==generation;}
  Admission Admit(size_t index){if(nonce_==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Native project generation exhausted");const Admission admission{index,++nonce_,IsTeardown(index)};active_=Active{admission};results_[index].state=State::Running;return admission;}
  void Retire(){if(!active_||active_->worker||!active_->outcome)return;const auto outcome=*active_->outcome;results_[active_->admission.index].state=outcome==Outcome::Passed?State::Passed:outcome==Outcome::Failed?State::Failed:State::Interrupted;active_.reset();}
  ProjectGraph graph_;std::vector<Result> results_;std::optional<Active> active_;uint64_t nonce_=0;bool stopped_=false,no_dependencies_=false;
};
}
