// SPDX-License-Identifier: MIT
#pragma once
#include "multi_file_catalog.h"
#include "repeat_execution.h"
#include "repeat_group_partition.h"

namespace suitecut::runner {
// Logical planning is separate from real child/host retirement. The adapter
// may call PhysicalExit only after actual waitpid, both output EOFs and its
// genuine descendant/channel/Source fences. Neither Complete nor Stop releases
// a worker slot, file owner, fixture scope or accepted callback.
class MultiFilePlan {
 public:
  using ProjectState=SerialProjectPlan::State;
  enum class State {Pending,Reserved,Running,Passed,Failed,Interrupted,NotRun};
  enum class Outcome {Passed,Failed,Interrupted};
  struct Admission {uint64_t generation=0;size_t segment=0,project=0,file=0;std::optional<size_t> repeat_slice=std::nullopt,repeat_group=std::nullopt;bool operator==(const Admission&)const=default;};
  struct Segment {
    size_t project=0,file=0;std::vector<uint64_t> local_tests;
    State state=State::Pending;uint64_t generation=0;int64_t pid=0;
    bool physical_exit=false;std::optional<Outcome> outcome;
    unsigned unexpected=0;bool native_progress=false;std::string reason;std::optional<size_t> repeat_slice=std::nullopt,repeat_group=std::nullopt;
  };
  MultiFilePlan(const MultiFileCatalog& catalog,MultiFileSelection selection,size_t workers,unsigned max_failures=0,bool require_native_progress=false,std::shared_ptr<const NativeRepeatSelection> repeats={},std::shared_ptr<const NativeRepeatGroupPartition> groups={})
      :catalog_(catalog),selection_(std::move(selection)),limit_(workers),max_failures_(max_failures),require_native_progress_(require_native_progress),projects_(catalog.Graph().Declarations().size(),ProjectState::NotSelected),no_work_(projects_.size(),false),repeats_(std::move(repeats)),groups_(std::move(groups)){
    if(!workers||workers>8||max_failures>4096||selection_.projects.selected.size()!=projects_.size()||selection_.project_tests.size()!=projects_.size()||selection_.roles.size()!=projects_.size())throw std::invalid_argument("Native multi-file plan shape/capacity rejected");
    if(!selection_.projects.no_dependencies&&catalog.Graph().Closure(selection_.projects.selected)!=selection_.projects.selected)throw std::invalid_argument("Native multi-file plan omits project closure");
    for(size_t p=0;p<projects_.size();++p)if(selection_.projects.selected[p]){
      projects_[p]=ProjectState::Pending;bool actual_declaration=false;
      for(size_t f=0;f<catalog.Files().size();++f){
        auto tests=catalog.LocalTests(selection_,p,f);bool declared_empty=false;
        if(p==catalog.AnonymousProject()&&catalog.Files()[f].catalog.projects.empty())actual_declaration=true;
        if(p!=catalog.AnonymousProject())for(size_t local=0;local<catalog.Files()[f].catalog.projects.size();++local)if(catalog.Files()[f].catalog.projects[local].name==catalog.Graph().Declarations()[p].name)
          {actual_declaration=true;declared_empty=std::none_of(catalog.Files()[f].catalog.project_tests.begin(),catalog.Files()[f].catalog.project_tests.end(),[&](const auto& membership){return membership.second==local;});}
        if(!tests.empty()||declared_empty){Segment segment;segment.project=p;segment.file=f;segment.local_tests=std::move(tests);segments_.push_back(std::move(segment));}
      }
      if(std::none_of(segments_.begin(),segments_.end(),[&](const auto& segment){return segment.project==p;})){if(!actual_declaration)throw std::invalid_argument("Native selected project has no actual file declaration");no_work_[p]=true;}
    }
    if(groups_&&(!repeats_||&groups_->Selection()!=repeats_.get()||&groups_->Catalog()!=&catalog))throw std::invalid_argument("Native repeat group plan lost exact catalog/selection owner");
    if(repeats_){
      segments_.clear();
      const auto count=groups_?groups_->Groups().size():repeats_->Slices().size();
      for(size_t index=0;index<count;++index){
        const auto slice=groups_?groups_->Groups()[index].slice:index;
        const auto& selected=repeats_->Slices().at(slice);const auto& unit=repeats_->Catalog().Units().at(selected.unit);
        if(!unit.project||unit.project>projects_.size()||!selection_.projects.selected[unit.project-1])throw std::invalid_argument("Native repeat segment has no selected global project");
        size_t file=catalog.Files().size();for(size_t f=0;f<catalog.Files().size();++f)if(catalog.Files()[f].file.id==unit.file)file=f;
        if(file==catalog.Files().size())throw std::invalid_argument("Native repeat segment has no collected original file");
        const auto project=static_cast<size_t>(unit.project-1);const auto available=catalog.LocalTests(selection_,project,file);
        for(const auto id:*selected.local_ids)if(std::find(available.begin(),available.end(),id)==available.end())throw std::invalid_argument("Native repeat segment selects a foreign original declaration");
        const auto& role=selection_.roles[project];
        if((unit.role==NativeRepeatRole::Primary&&role!="primary")||(unit.role==NativeRepeatRole::Dependency&&role!="dependency")||(unit.role==NativeRepeatRole::Teardown&&role!="teardown"))throw std::invalid_argument("Native repeat segment role differs from original graph");
        Segment segment;segment.file=file;segment.project=project;segment.local_tests=groups_?*groups_->Groups()[index].local_ids:*selected.local_ids;segment.repeat_slice=slice;if(groups_)segment.repeat_group=index;segments_.push_back(std::move(segment));
      }
      for(size_t p=0;p<projects_.size();++p)if(selection_.projects.selected[p])no_work_[p]=std::none_of(segments_.begin(),segments_.end(),[&](const auto& segment){return segment.project==p;});
    }
    if(segments_.size()>128*128)throw std::invalid_argument("Native multi-file segment capacity exceeded");
  }
  std::optional<Admission> Next(){
    Refresh();if(stopped_||Occupied()>=limit_)return std::nullopt;
    for(size_t i=0;i<segments_.size();++i){auto& segment=segments_[i];
      if(segment.state!=State::Pending||!Ready(segment.project))continue;
      if(generation_==UINT64_MAX)throw std::overflow_error("Native multi-file admission generation exhausted");
      segment.state=State::Reserved;segment.generation=++generation_;projects_[segment.project]=ProjectState::Running;
      return Admission{segment.generation,i,segment.project,segment.file,segment.repeat_slice,segment.repeat_group};
    }
    return std::nullopt;
  }
  bool Started(const Admission& admission,int64_t pid){
    auto* segment=Exact(admission);if(!segment||segment->state!=State::Reserved||pid<=0||pids_.contains(pid))return false;
    // Allocate native PID ownership before acknowledging admission.
    pids_.insert(pid);segment->pid=pid;segment->state=State::Running;return true;
  }
  bool RefusedBeforeStart(const Admission& admission,const std::string& reason){
    auto* segment=Exact(admission);if(!segment||segment->state!=State::Reserved||reason.size()>8192)return false;
    segment->state=State::Failed;segment->reason=reason;segment->outcome=Outcome::Failed;
    // No process was accepted; this is not a fabricated actual exit receipt.
    Refresh();return true;
  }
  bool Complete(const Admission& admission,Outcome outcome,unsigned unexpected){
    auto* segment=Exact(admission);if(!segment||segment->state!=State::Running||segment->outcome||unexpected>4096)return false;
    if((outcome==Outcome::Passed&&unexpected)||(require_native_progress_&&!segment->native_progress))return false;
    if(segment->native_progress){if(unexpected!=segment->unexpected)return false;}
    else if(!UnexpectedReceipt(admission,unexpected))return false;
    segment->outcome=outcome;
    Retire(*segment);Refresh();return true;
  }
  bool UnexpectedReceipt(const Admission& admission,unsigned cumulative){
    auto* segment=Exact(admission);if(!segment||segment->state!=State::Running||segment->outcome||cumulative<segment->unexpected||cumulative>4096)return false;
    const auto delta=cumulative-segment->unexpected;if(delta>4096-failures_)return false;
    if(max_failures_&&failures_+delta>=max_failures_)Stop("Native multi-file maximum failure count reached");
    // A duplicate total is harmless but cannot add a second global failure.
    segment->native_progress=true;segment->unexpected=cumulative;failures_+=delta;return true;
  }
  bool PhysicalExit(const Admission& admission,int64_t pid){
    auto* segment=Exact(admission);if(!segment||segment->state!=State::Running||segment->pid!=pid||segment->physical_exit||!pids_.contains(pid))return false;
    segment->physical_exit=true;pids_.erase(pid);Retire(*segment);Refresh();return true;
  }
  void Stop(const std::string& reason){
    if(reason.empty()||reason.size()>8192)throw std::invalid_argument("Native multi-file stop reason rejected");
    if(stopped_)return;
    // All reason allocations precede the state transition. Running segments
    // retain their original actual receipt obligations and cleanup budgets.
    std::vector<std::string> reasons;reasons.reserve(segments_.size());for(const auto& segment:segments_)reasons.push_back(segment.state==State::Pending||segment.state==State::Reserved?reason:std::string{});
    for(size_t i=0;i<segments_.size();++i)if(segments_[i].state==State::Pending||segments_[i].state==State::Reserved){segments_[i].reason=std::move(reasons[i]);segments_[i].state=State::NotRun;}
    for(size_t p=0;p<projects_.size();++p)if(no_work_[p]&&projects_[p]==ProjectState::Pending)projects_[p]=ProjectState::NotRun;
    stopped_=true;Refresh();
  }
  bool Done(){Refresh();return Occupied()==0&&std::none_of(segments_.begin(),segments_.end(),[](const auto& segment){return segment.state==State::Pending;});}
  size_t Physical()const{return pids_.size();}
  size_t Occupied()const{return static_cast<size_t>(std::count_if(segments_.begin(),segments_.end(),[](const auto& segment){return segment.state==State::Reserved||segment.state==State::Running;}));}
  unsigned Unexpected()const{return failures_;}
  bool Stopped()const{return stopped_;}
  const std::vector<Segment>& Segments()const{return segments_;}
  const std::vector<ProjectState>& Projects()const{return projects_;}
 private:
  Segment* Exact(const Admission& admission){
    if(!admission.generation||admission.segment>=segments_.size())return nullptr;
    auto& segment=segments_[admission.segment];
    return segment.generation==admission.generation&&segment.project==admission.project&&segment.file==admission.file&&segment.repeat_slice==admission.repeat_slice&&segment.repeat_group==admission.repeat_group?&segment:nullptr;
  }
  void Retire(Segment& segment){
    if(!segment.outcome||!segment.physical_exit)return;
    segment.state=*segment.outcome==Outcome::Passed?State::Passed:*segment.outcome==Outcome::Failed?State::Failed:State::Interrupted;
  }
  bool Teardown(size_t project)const{return !selection_.projects.no_dependencies&&catalog_.Graph().IsTeardown(project);}
  bool Ready(size_t project)const{
    if(projects_[project]!=ProjectState::Pending&&projects_[project]!=ProjectState::Running)return false;
    if(Teardown(project)){
      for(size_t p=0;p<projects_.size();++p)if(!Teardown(p)&&(projects_[p]==ProjectState::Pending||projects_[p]==ProjectState::Running))return false;
    }else if(!selection_.projects.no_dependencies)for(const auto dependency:catalog_.Graph().Dependencies(project))if(projects_[dependency]!=ProjectState::Passed)return false;
    return true;
  }
  void Refresh(){
    // Project completion is derived only from every actual file segment's
    // terminal state. A successful logical report cannot open a dependency
    // while its source process, callbacks or output streams remain physical.
    for(size_t p=0;p<projects_.size();++p){if(projects_[p]==ProjectState::NotSelected||no_work_[p])continue;
      bool pending=false,running=false,failed=false,interrupted=false,not_run=false;
      for(const auto& segment:segments_)if(segment.project==p){pending|=segment.state==State::Pending;running|=segment.state==State::Reserved||segment.state==State::Running;failed|=segment.state==State::Failed;interrupted|=segment.state==State::Interrupted;not_run|=segment.state==State::NotRun;}
      if(running)projects_[p]=ProjectState::Running;
      else if(pending)projects_[p]=ProjectState::Pending;
      else projects_[p]=failed?ProjectState::Failed:interrupted?ProjectState::Interrupted:not_run?ProjectState::NotRun:ProjectState::Passed;
    }
    if(stopped_)return;
    bool changed=true;
    while(changed){changed=false;
      for(size_t p=0;p<projects_.size();++p)if(!selection_.projects.no_dependencies&&!Teardown(p)&&projects_[p]==ProjectState::Pending){
        for(const auto dependency:catalog_.Graph().Dependencies(p))if(projects_[dependency]!=ProjectState::Pending&&projects_[dependency]!=ProjectState::Running&&projects_[dependency]!=ProjectState::Passed){
          for(auto& segment:segments_)if(segment.project==p&&segment.state==State::Pending){segment.reason="Native multi-file project dependency did not pass";segment.state=State::NotRun;}
          projects_[p]=ProjectState::Blocked;changed=true;break;
        }
      }
      for(size_t p=0;p<projects_.size();++p)if(no_work_[p]&&projects_[p]==ProjectState::Pending&&Ready(p)){projects_[p]=ProjectState::Passed;changed=true;}
    }
  }
  const MultiFileCatalog& catalog_;MultiFileSelection selection_;size_t limit_;unsigned max_failures_,failures_=0;bool require_native_progress_=false;
  std::vector<ProjectState> projects_;std::vector<bool> no_work_;std::vector<Segment> segments_;std::set<int64_t> pids_;uint64_t generation_=0;bool stopped_=false;std::shared_ptr<const NativeRepeatSelection> repeats_;std::shared_ptr<const NativeRepeatGroupPartition> groups_;
};
}
