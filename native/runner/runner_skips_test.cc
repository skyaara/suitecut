// SPDX-License-Identifier: MIT
#include "runner_html.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>

int main(int argc,char** argv){
  namespace sr=suitecut::runner;
  assert(argc==2);const std::filesystem::path directory=argv[1];
  std::filesystem::create_directories(directory);
  sr::RunnerReport report;
  report.skipped.push_back({UINT64_MAX,"\"Actual title <>&\\ud800\"","\"Actual reason\\u0000\\udc00\""});
  report.unrun.push_back({7,"\"Unrun\"","\"Actual earlier failure\""});
  auto summary=sr::report_detail::Validate(report);
  assert(summary.total_tests()==2&&summary.skipped==1&&summary.did_not_run==1&&summary.duration_ms==0);
  sr::WriteRunnerJson(report,directory/"result.json");
  sr::WriteRunnerJUnit(report,directory/"result.xml");
  sr::WriteRunnerHTML(report,directory/"result.html");
  const auto bytes=[](const auto& path){std::ifstream in(path);return std::string(std::istreambuf_iterator<char>(in),{});};
  const auto original=bytes(directory/"result.json");
  for(unsigned scenario=0;scenario<6;++scenario){
    auto invalid=report;
    if(scenario==0)invalid.skipped.push_back(invalid.skipped.front());
    if(scenario==1)invalid.skipped.front().test_id=7;
    if(scenario==2)invalid.skipped.front().test_id=0;
    if(scenario==3)invalid.skipped.front().reason_json="Invalid JSON";
    if(scenario==4)invalid.skipped.front().title_json="\"Unescaped\nnewline\"";
    if(scenario==5)invalid.attempts.push_back({UINT64_MAX,1,0,1,1,"\"Actual component attempt\"","\"\"","passed",true,1});
    bool rejected=false;
    try{sr::WriteRunnerJson(invalid,directory/"result.json");}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected&&bytes(directory/"result.json")==original);
  }
}
