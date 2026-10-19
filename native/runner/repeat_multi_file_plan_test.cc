// SPDX-License-Identifier: MIT
#include "multi_file_plan.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
namespace {
sr::CollectedSuiteFile File(uint64_t file,const std::string& name,std::vector<std::string> dependencies={}){
 sr::CollectedSuiteFile f;f.file={file,1,file};f.path="/controlled/repeat-"+std::to_string(file)+".mjs";f.catalog.protocol="controlled native metadata";
 f.catalog.projects.push_back({name,std::move(dependencies),std::nullopt,true});f.catalog.project_metadata.push_back("{}");
 for(uint64_t id=1;id<=2;++id){f.catalog.tests.push_back({id,1000,0,false,"\"original declaration\"",{}});f.catalog.project_tests.emplace(id,0);}return f;
}
auto Tests(){return std::make_shared<const std::vector<sr::NativeRepeatTest>>(std::initializer_list<sr::NativeRepeatTest>{{1,100},{2,100}});}
auto IDs(std::initializer_list<uint64_t> ids){return std::make_shared<const std::vector<uint64_t>>(ids);}
auto Repeats(){
 auto units=std::make_shared<const sr::NativeRepeatCatalog>(std::vector<sr::NativeRepeatUnit>{{1,1,sr::NativeRepeatRole::Dependency,{1,1,1},Tests(),false,32},{2,2,sr::NativeRepeatRole::Primary,{3,2,2},Tests(),false,32}},16384,4096,0);
 sr::NativeRepeatSelectionUsage usage;
 return std::make_shared<const sr::NativeRepeatSelection>(units,std::vector<sr::NativeSelectedRepeatSlice>{{0,0,IDs({1,2})},{1,0,IDs({1})},{1,1,IDs({2})}},sr::NativeRepeatSelectionLimits{16384,128,64*1024*1024},usage);
}
}
int main(){
 // All numeric process values below are controlled pure-policy gates. They
 // make no claim of actual worker, callback, waitpid or Source retirement.
 sr::MultiFileCatalog catalog({File(1,"setup"),File(2,"body",{"setup"})});auto selection=catalog.Select({}, {"body"});const auto repeats=Repeats();
 sr::MultiFilePlan plan(catalog,selection,2,0,false,repeats);assert(plan.Segments().size()==3);
 const auto setup=plan.Next();assert(setup&&setup->file==0&&setup->repeat_slice==0&&!plan.Next());
 assert(plan.Started(*setup,101)&&plan.Complete(*setup,sr::MultiFilePlan::Outcome::Passed,0)&&!plan.Next());
 assert(plan.PhysicalExit(*setup,101));const auto first=plan.Next(),second=plan.Next();assert(first&&second&&first->repeat_slice==1&&second->repeat_slice==2&&!plan.Next());
 assert(plan.Segments()[first->segment].local_tests==std::vector<uint64_t>{1}&&plan.Segments()[second->segment].local_tests==std::vector<uint64_t>{2});
 auto forged=*first;forged.repeat_slice=second->repeat_slice;assert(!plan.Started(forged,201));
 assert(plan.Started(*first,201)&&plan.Started(*second,202));
 assert(plan.Complete(*first,sr::MultiFilePlan::Outcome::Passed,0)&&!plan.Done()&&plan.Occupied()==2);
 assert(plan.Complete(*second,sr::MultiFilePlan::Outcome::Failed,1)&&!plan.Done());
 assert(plan.PhysicalExit(*first,201)&&!plan.Done());assert(plan.PhysicalExit(*second,202)&&plan.Done());
 assert(plan.Projects()[1]==sr::SerialProjectPlan::State::Failed&&plan.Physical()==0);
 sr::MultiFilePlan stopped(catalog,selection,2,0,false,repeats);const auto held=stopped.Next();assert(held&&stopped.Started(*held,301));stopped.Stop("original suite cancellation");assert(!stopped.Done()&&stopped.Physical()==1);
 assert(stopped.Complete(*held,sr::MultiFilePlan::Outcome::Interrupted,0)&&stopped.PhysicalExit(*held,301)&&stopped.Done());
 assert(stopped.Segments()[1].state==sr::MultiFilePlan::State::NotRun&&stopped.Segments()[2].state==sr::MultiFilePlan::State::NotRun);
 auto wrong=selection;wrong.roles[1]="dependency";bool rejected=false;try{sr::MultiFilePlan bad(catalog,wrong,2,0,false,repeats);}catch(const std::exception&){rejected=true;}assert(rejected);
 std::cout<<"native repeated segment exact-owner/selection/graph/physical policy PASS\n";
}
