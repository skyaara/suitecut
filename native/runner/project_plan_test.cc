// SPDX-License-Identifier: MIT
#include "project_plan.h"
#include <cassert>
#include <iostream>
using suitecut::runner::ProjectDeclaration;
using suitecut::runner::ProjectGraph;
using Plan=suitecut::runner::SerialProjectPlan;
template<class Call>void Rejected(Call call){bool rejected=false;try{call();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
Plan::Admission Next(Plan& plan,size_t index,bool teardown=false){const auto next=plan.Next();assert(next&&next->index==index&&next->teardown==teardown);return *next;}
void Finish(Plan& plan,Plan::Admission next,Plan::Outcome outcome=Plan::Outcome::Passed){const int64_t pid=100+static_cast<int64_t>(next.index);assert(plan.WorkerStarted(next.generation,pid));assert(plan.Complete(next.generation,outcome));assert(!plan.Next());assert(plan.Physical());assert(plan.WorkerExited(next.generation,pid));assert(!plan.Physical());}
int main(){
  Rejected([]{ProjectGraph graph({});});
  Rejected([]{ProjectGraph graph(std::vector<ProjectDeclaration>(129,{"a"}));});
  for(const auto& name:std::vector<std::string>{"",std::string(257,'a'),std::string("a\0b",3)})Rejected([&]{ProjectGraph graph({{name}});});
  Rejected([]{ProjectGraph graph({{"a"},{"a"}});});
  Rejected([]{ProjectGraph graph({{"a",{"missing"}}});});
  Rejected([]{ProjectGraph graph({{"a",{"a"}}});});
  Rejected([]{ProjectGraph graph({{"a",{"b","b"}},{"b"}});});
  Rejected([]{ProjectGraph graph({{"a",{"b"}},{"b",{"c"}},{"c",{"a"}}});});
  Rejected([]{ProjectGraph graph({{"a",{},"missing"}});});
  Rejected([]{ProjectGraph graph({{"a",{},"a"}});});
  Rejected([]{ProjectGraph graph({{"a",{},"z"},{"z",{"a"}}});});
  Rejected([]{ProjectGraph graph({{"a",{},"z"},{"z",{},"zz"},{"zz"}});});
  Rejected([]{ProjectGraph graph({{"a",{},"z"},{"b",{"z"}},{"z"}});});
  {
    ProjectGraph graph({{"default"},{"disabled",{},std::nullopt,false}});
    assert((graph.Select()==std::vector<bool>{true,false}));
    assert((graph.Select({"disabled"})==std::vector<bool>{false,true}));
    Rejected([&]{graph.Select({"unknown"});});assert((graph.Select({"default","DEFAULT"})==std::vector<bool>{true,false}));
  }
  {
    ProjectGraph graph({{"body",{"setup"}},{"setup",{},"cleanup",false},{"cleanup",{},std::nullopt,false},{"unrelated",{},std::nullopt,false}});
    assert((graph.Select()==std::vector<bool>{true,true,true,false}));
    Plan plan(graph);Finish(plan,Next(plan,1));Finish(plan,Next(plan,0));Finish(plan,Next(plan,2,true));assert(plan.Done());
  }
  {
    Plan plan(ProjectGraph({{"last",{"middle"}},{"middle",{"first"}},{"first",{},"cleanup"},{"cleanup"}}),{"last"});
    Finish(plan,Next(plan,2),Plan::Outcome::Failed);
    // Reverse declaration order must propagate failure without another poll.
    Finish(plan,Next(plan,3,true));assert(plan.Done());
    assert(plan.Results()[1].state==Plan::State::Blocked&&plan.Results()[1].dependency==2);
    assert(plan.Results()[0].state==Plan::State::Blocked&&plan.Results()[0].dependency==1);
  }
  {
    Plan plan(ProjectGraph({{"setup",{},"cleanup"},{"dependent",{"setup"}},{"unrelated"},{"cleanup"}}));
    Finish(plan,Next(plan,0),Plan::Outcome::Failed);Finish(plan,Next(plan,2));Finish(plan,Next(plan,3,true));
    assert(plan.Results()[1].state==Plan::State::Blocked&&plan.Done());
  }
  {
    Plan plan(ProjectGraph({{"setup",{},"cleanup"},{"first",{"setup"}},{"second",{"setup"}},{"cleanup"}}),{"second","first"});
    Finish(plan,Next(plan,0));Finish(plan,Next(plan,1));Finish(plan,Next(plan,2));Finish(plan,Next(plan,3,true));assert(plan.Done());
  }
  {
    Plan plan(ProjectGraph({{"a",{},"cleanup"},{"b",{},"cleanup"},{"cleanup"}}));
    Finish(plan,Next(plan,0));Finish(plan,Next(plan,1));Finish(plan,Next(plan,2,true));assert(plan.Done());
  }
  {
    Plan plan(ProjectGraph({{"a",{},"cleanup"},{"cleanup"}}),{"cleanup"});Finish(plan,Next(plan,1,true));assert(plan.Done());
  }
  {
    Plan plan(ProjectGraph({{"a"},{"b"}}));const auto next=Next(plan,0);
    assert(!plan.Complete(next.generation,Plan::Outcome::Passed));assert(!plan.WorkerStarted(0,100));
    assert(!plan.WorkerStarted(next.generation,-1));assert(plan.WorkerStarted(next.generation,100));
    assert(!plan.WorkerStarted(next.generation,101));assert(!plan.WorkerExited(next.generation+1,100));
    assert(!plan.WorkerExited(next.generation,101));assert(plan.Complete(next.generation,Plan::Outcome::Failed));
    assert(!plan.Complete(next.generation,Plan::Outcome::Passed));assert(!plan.WorkerStarted(next.generation,101));
    assert(plan.Results()[0].state==Plan::State::Running&&plan.Physical()&&!plan.Next()&&!plan.Done());
    assert(plan.WorkerExited(next.generation,100));assert(!plan.WorkerExited(next.generation,100));
    assert(plan.Results()[0].state==Plan::State::Failed);Finish(plan,Next(plan,1));assert(plan.Done());
  }
  {
    Plan plan(ProjectGraph({{"retrying"},{"next"}}));const auto next=Next(plan,0);
    assert(plan.WorkerStarted(next.generation,100));assert(plan.WorkerExited(next.generation,100));assert(!plan.Next());
    assert(plan.WorkerStarted(next.generation,101));assert(!plan.WorkerExited(next.generation,100));
    assert(plan.WorkerExited(next.generation,101));assert(plan.Complete(next.generation,Plan::Outcome::Passed));
    Finish(plan,Next(plan,1));assert(plan.Done());
  }
  {
    Plan plan(ProjectGraph({{"active",{},"cleanup"},{"pending"},{"cleanup"}}));const auto next=Next(plan,0);
    assert(plan.WorkerStarted(next.generation,100));plan.Stop("Native original global deadline exceeded");plan.Stop("must not replace cause");
    assert(!plan.Next()&&!plan.Done()&&plan.Physical());assert(plan.Complete(next.generation,Plan::Outcome::Interrupted));
    assert(!plan.Done());assert(plan.WorkerExited(next.generation,100));assert(plan.Done());
    assert(plan.Results()[0].state==Plan::State::Interrupted);
    for(size_t i:{1U,2U})assert(plan.Results()[i].state==Plan::State::NotRun&&plan.Results()[i].reason=="Native original global deadline exceeded");
  }
  {
    Plan plan(ProjectGraph({{"disabled",{},std::nullopt,false}}));assert(plan.Done()&&!plan.Next());
  }
  {
    Plan plan(ProjectGraph({{"neverStarted"},{"next"}}));Next(plan,0);plan.Stop("Actual native process could not start");
    assert(plan.Done()&&!plan.Physical()&&plan.Results()[0].state==Plan::State::NotRun);
  }
  std::cout<<"Native project graph/planner controls passed\n";
}
