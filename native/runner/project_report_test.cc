// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include <cassert>
using namespace suitecut::runner;
template<class Call>void Rejected(Call call){bool rejected=false;try{call();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
RunnerReport ControlledReport(){
  RunnerReport report;report.workers.push_back({1,123,false,0,"",""});
  report.attempts.push_back({1,1,0,1,123,"\"actual declared title\"","\"\"","passed",true,1});
  ReportProject project;project.id=1;project.name_json="\"project π\"";project.metadata_json="{\"nested\":[42,\"\\ud800\"]}";project.status="passed";project.workers={1};project.test_ids={1};project.selected_test_ids={1};project.selection_role="primary";report.projects.push_back(project);return report;
}
int main(){
  auto report=ControlledReport();report_detail::Validate(report);
  {auto bad=report;bad.projects[0].selected_test_ids={2};Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].selected_test_ids={1,1};Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].selection_role="invented";Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].selection_role="dependency";bad.projects[0].dependencies_ignored=true;Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].metadata_json="{}],\"forged\":true";Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].name_json="\"\"";Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].status="running";Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].workers.clear();Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].workers={2};Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].test_ids={2};Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects.push_back(bad.projects[0]);Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].dependencies={2};Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].teardown=1;Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].status="blocked";bad.projects[0].workers.clear();Rejected([&]{report_detail::Validate(bad);});}
  {auto bad=report;bad.projects[0].status="notSelected";Rejected([&]{report_detail::Validate(bad);});}
  {
    ReportProject blocked;blocked.id=2;blocked.name_json="\"blocked\"";blocked.status="blocked";blocked.dependencies={1};blocked.failed_dependency=1;blocked.test_ids={2};report.projects.push_back(blocked);
    report.unrun.push_back({2,"\"unadmitted declaration\"","\"Native dependency failed\""});report_detail::Validate(report);
  }
}
