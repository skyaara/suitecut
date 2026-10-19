// SPDX-License-Identifier: MIT
#include "multi_file_plan.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
sr::CollectedSuiteFile File(uint64_t id,const std::string& name,std::vector<std::string> dependencies={},std::optional<std::string> teardown=std::nullopt){
  sr::CollectedSuiteFile file;file.file={id,1,id};file.path="/controlled/file"+std::to_string(id)+".spec.mjs";file.catalog.protocol="controlled metadata";
  file.catalog.projects.push_back({name,std::move(dependencies),std::move(teardown),true});file.catalog.project_metadata.push_back("{}");
  file.catalog.tests.push_back({1,1000,0,false,"\"controlled test\"",{}});file.catalog.project_tests.emplace(1,0);return file;
}
sr::MultiFileCatalog FilteredCatalog(bool filter_bootstrap=false){
 auto bootstrap=File(1,"bootstrap"),setup=File(2,"setup",{"bootstrap"}),body=File(3,"body",{"setup"},"cleanup"),cleanup=File(4,"cleanup");
 setup.config_eligible=std::set<uint64_t>{};cleanup.config_eligible=std::set<uint64_t>{};if(filter_bootstrap)bootstrap.config_eligible=std::set<uint64_t>{};
 return sr::MultiFileCatalog({std::move(bootstrap),std::move(setup),std::move(body),std::move(cleanup)});
}

// Numeric identities below are pure policy inputs, not actual process receipts.
void FilteredSupportControls(){
 auto catalog=FilteredCatalog();const auto selection=catalog.Select({}, {"body"});
 assert(selection.projects.selected[0]&&selection.projects.selected[1]&&selection.projects.selected[2]&&selection.projects.selected[3]);
 assert(selection.project_tests[0].size()==1&&selection.project_tests[1].empty()&&selection.project_tests[2].size()==1&&selection.project_tests[3].empty());
 assert(selection.roles[1]=="dependency"&&selection.roles[3]=="teardown");
 sr::MultiFilePlan plan(catalog,selection,2);assert(plan.Segments().size()==2);
 const auto bootstrap=plan.Next();assert(bootstrap&&bootstrap->file==0&&!plan.Next());
 assert(plan.Projects()[1]==sr::SerialProjectPlan::State::Pending&&plan.Projects()[3]==sr::SerialProjectPlan::State::Pending);
 assert(plan.Started(*bootstrap,101)&&plan.Complete(*bootstrap,sr::MultiFilePlan::Outcome::Passed,0));
 assert(!plan.Next()&&plan.Projects()[1]!=sr::SerialProjectPlan::State::Passed);
 assert(plan.PhysicalExit(*bootstrap,101));const auto body=plan.Next();assert(body&&body->file==2&&!plan.Next());
 assert(plan.Projects()[1]==sr::SerialProjectPlan::State::Passed&&plan.Projects()[3]==sr::SerialProjectPlan::State::Pending);
 assert(plan.Started(*body,102)&&plan.Complete(*body,sr::MultiFilePlan::Outcome::Passed,0));
 assert(!plan.Next()&&plan.Projects()[3]==sr::SerialProjectPlan::State::Pending);
 assert(plan.PhysicalExit(*body,102)&&plan.Done()&&plan.Physical()==0&&plan.Segments().size()==2);
 assert(plan.Projects()[3]==sr::SerialProjectPlan::State::Passed);
 auto all_empty_support=FilteredCatalog(true);sr::MultiFilePlan chain(all_empty_support,all_empty_support.Select({}, {"body"}),2);
 const auto primary=chain.Next();assert(primary&&primary->file==2&&chain.Segments().size()==1);
 sr::MultiFilePlan failed(catalog,selection,2);const auto bad=failed.Next();assert(bad&&failed.Started(*bad,201));
 assert(failed.Complete(*bad,sr::MultiFilePlan::Outcome::Failed,1)&&failed.PhysicalExit(*bad,201));
 assert(!failed.Next()&&failed.Done()&&failed.Projects()[1]!=sr::SerialProjectPlan::State::Passed);
 assert(failed.Segments()[1].state==sr::MultiFilePlan::State::NotRun);
 sr::MultiFilePlan cancelled(catalog,selection,2);cancelled.Stop("controlled original cancellation");
 assert(cancelled.Done()&&!cancelled.Next()&&cancelled.Physical()==0);
 assert(cancelled.Projects()[1]==sr::SerialProjectPlan::State::NotRun&&cancelled.Projects()[3]==sr::SerialProjectPlan::State::NotRun);
 auto forged=selection;forged.projects.selected[catalog.AnonymousProject()]=true;forged.roles[catalog.AnonymousProject()]="primary";
 bool rejected=false;try{sr::MultiFilePlan absent(catalog,std::move(forged),1);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 std::cout<<"Native filtered support closure/physical gates/no invented actors policy PASS\n";
}
int main(){
  sr::MultiFileCatalog catalog({File(1,"setup"),File(2,"setup"),File(3,"browser",{"setup"},"teardown"),File(4,"teardown")});
  sr::MultiFilePlan plan(catalog,catalog.Select({}, {"browser"}),2);
  const auto first=plan.Next(),second=plan.Next();assert(first&&second&&first->file==0&&second->file==1&&!plan.Next());
  assert(plan.Started(*first,101)&&plan.Started(*second,102)&&plan.Physical()==2);
  assert(!plan.Started(*first,103)&&!plan.PhysicalExit(*first,999));
  auto forged=*first;++forged.generation;assert(!plan.Complete(forged,sr::MultiFilePlan::Outcome::Passed,0));
  assert(plan.Complete(*first,sr::MultiFilePlan::Outcome::Passed,0)&&plan.Physical()==2&&!plan.Next());
  assert(plan.PhysicalExit(*second,102)&&plan.Physical()==1&&!plan.Next());
  assert(plan.Complete(*second,sr::MultiFilePlan::Outcome::Passed,0)&&!plan.Next());
  assert(plan.PhysicalExit(*first,101)&&plan.Physical()==0);
  const auto body=plan.Next();assert(body&&body->file==2);assert(plan.Started(*body,103));
  assert(!plan.PhysicalExit(*first,101)&&!plan.Complete(*first,sr::MultiFilePlan::Outcome::Failed,1));
  assert(plan.Complete(*body,sr::MultiFilePlan::Outcome::Passed,0)&&!plan.Next());
  assert(plan.PhysicalExit(*body,103));const auto cleanup=plan.Next();assert(cleanup&&cleanup->file==3);
  assert(plan.Started(*cleanup,104)&&plan.PhysicalExit(*cleanup,104)&&!plan.Done());
  assert(plan.Complete(*cleanup,sr::MultiFilePlan::Outcome::Passed,0)&&plan.Done());
  sr::MultiFilePlan failed(catalog,catalog.Select({}, {"browser"}),2);
  const auto a=failed.Next(),b=failed.Next();assert(a&&b&&failed.Started(*a,201)&&failed.Started(*b,202));
  assert(failed.Complete(*a,sr::MultiFilePlan::Outcome::Failed,1)&&failed.PhysicalExit(*a,201));
  assert(!failed.Next());assert(failed.Complete(*b,sr::MultiFilePlan::Outcome::Passed,0)&&failed.PhysicalExit(*b,202));
  const auto after=failed.Next();assert(after&&after->file==3);assert(failed.Segments()[2].state==sr::MultiFilePlan::State::NotRun);
  sr::MultiFilePlan stopped(catalog,catalog.Select({}, {"browser"}),2,1);
  const auto old=stopped.Next();assert(old&&stopped.Started(*old,301));
  assert(stopped.Complete(*old,sr::MultiFilePlan::Outcome::Failed,1)&&stopped.Stopped()&&stopped.Physical()==1&&!stopped.Done());
  assert(stopped.PhysicalExit(*old,301)&&stopped.Done()&&!stopped.Next());
  assert(!stopped.Complete(*old,sr::MultiFilePlan::Outcome::Passed,0));
  sr::MultiFilePlan refused(catalog,catalog.Select({}, {"browser"}),1);
  const auto reservation=refused.Next();assert(reservation&&refused.RefusedBeforeStart(*reservation,"actual exec admission refused")&&refused.Physical()==0);
  assert(!refused.Started(*reservation,401));
  sr::MultiFilePlan streamed(catalog,catalog.Select({}, {"browser"}),2,1);
  const auto actual=streamed.Next();assert(actual&&streamed.Started(*actual,501));
  assert(streamed.UnexpectedReceipt(*actual,1)&&streamed.Unexpected()==1&&streamed.Stopped()&&streamed.Physical()==1&&!streamed.Next());
  assert(streamed.UnexpectedReceipt(*actual,1)&&streamed.Unexpected()==1);
  assert(!streamed.UnexpectedReceipt(*actual,0)&&!streamed.Complete(*actual,sr::MultiFilePlan::Outcome::Failed,0));
  assert(streamed.Complete(*actual,sr::MultiFilePlan::Outcome::Failed,1)&&streamed.Unexpected()==1&&streamed.Physical()==1);
  assert(streamed.PhysicalExit(*actual,501)&&streamed.Done());
  assert(!streamed.UnexpectedReceipt(*actual,2));
  sr::MultiFilePlan required(catalog,catalog.Select({}, {"browser"}),1,0,true);
  const auto authentic=required.Next();assert(authentic&&required.Started(*authentic,601));
  assert(!required.Complete(*authentic,sr::MultiFilePlan::Outcome::Passed,0));
  assert(required.UnexpectedReceipt(*authentic,0)&&required.Complete(*authentic,sr::MultiFilePlan::Outcome::Passed,0));
  assert(required.PhysicalExit(*authentic,601));
  sr::MultiFileCatalog mixed_catalog({File(1,"same"),File(2,"same")});sr::MultiFilePlan mixed(mixed_catalog,mixed_catalog.Select({}, {"same"}),2);
  const auto failed_owner=mixed.Next(),interrupted_owner=mixed.Next();assert(failed_owner&&interrupted_owner&&mixed.Started(*failed_owner,701)&&mixed.Started(*interrupted_owner,702));
  assert(mixed.Complete(*failed_owner,sr::MultiFilePlan::Outcome::Failed,1)&&mixed.Complete(*interrupted_owner,sr::MultiFilePlan::Outcome::Interrupted,0));
  assert(mixed.PhysicalExit(*failed_owner,701)&&mixed.PhysicalExit(*interrupted_owner,702)&&mixed.Done());
  assert(mixed.Projects()[0]==sr::SerialProjectPlan::State::Failed);
  FilteredSupportControls();
  std::cout<<"Native file actor/dependency/physical retirement controls passed\n";
}
