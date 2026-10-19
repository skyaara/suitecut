// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include <cassert>
#include <csignal>
#include <fstream>
#include <iterator>
#include <sys/resource.h>

using namespace suitecut::runner;
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
template<class Callback>void Rejected(Callback callback){bool rejected=false;try{callback();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(int argc,char** argv){
  assert(argc==2);const std::filesystem::path folder=argv[1];std::filesystem::create_directories(folder);
  RunnerReport report;const auto pid=static_cast<int64_t>(getpid());
  const std::string bytes("\0\xff<>&\r\n",7);
  report.workers={{0,pid,false,0,bytes,std::string("\xfe\0",2)},{1,pid,true,SIGTERM,"actual binary bytes are encoded, without inventing per-test attribution",""},{2,pid,false,-1,"",""}};
  report.attempts={
    {UINT64_MAX-1,1,0,0,pid,"\"retry <& \\ud83d\\ude80 \\ud800 \\u0000\"","\"first actual error <&\\nline\\t\\r\"","failed",false,1.25},
    {UINT64_MAX-1,2,1,1,pid,"\"retry <& \\ud83d\\ude80 \\ud800 \\u0000\"","\"\"","passed",true,2.75},
    {2,3,0,0,pid,"\"expected failure\"","\"expected <& failure\"","failed",true,3},
    {3,4,0,0,pid,"\"unexpected pass\"","\"expected failure did not occur\"","passed",false,4},
    {4,5,0,0,pid,"\"timeout\"","\"original deadline elapsed\"","timedOut",false,5},
    {5,6,0,0,pid,"\"skip\"","\"\"","skipped",true,0},
    {6,7,0,0,pid,"\"interrupted\"","\"worker stopped\"","interrupted",false,7},
  };
  report.global_errors_json={"\"global \\udfff \\ufffe <& \\u0001\""};
  const auto json=folder/"results.json",xml=folder/"results.xml";
  WriteRunnerJson(report,json);WriteRunnerJUnit(report,xml);const auto original=Read(json);
  assert(original.find("18446744073709551614")!=std::string::npos);
  assert(original.find("00ff3c3e260d0a")!=std::string::npos);
  auto invalid=report;invalid.attempts[0].title_json="\"x\",\"injected\":true";Rejected([&]{WriteRunnerJson(invalid,json);});assert(Read(json)==original);
  invalid=report;invalid.attempts[0].error_json="\"\xff\"";Rejected([&]{WriteRunnerJUnit(invalid,xml);});
  invalid=report;invalid.attempts[1].generation=1;Rejected([&]{WriteRunnerJson(invalid,json);});
  invalid=report;invalid.attempts[1].retry=0;Rejected([&]{WriteRunnerJson(invalid,json);});
  invalid=report;invalid.attempts[0].duration_ms=std::numeric_limits<double>::quiet_NaN();Rejected([&]{WriteRunnerJson(invalid,json);});
  invalid=report;invalid.attempts[0].status="passed\"}";Rejected([&]{WriteRunnerJson(invalid,json);});
  invalid=report;++invalid.workers[0].pid;Rejected([&]{WriteRunnerJson(invalid,json);});
  invalid=report;invalid.workers[0].stdout_bytes.assign(kRunnerReportStreamBytes+1,'a');Rejected([&]{WriteRunnerJson(invalid,json);});invalid=RunnerReport{};
  auto unrun=report;unrun.unrun={{77,"\"not started \\ud800 <&\"","\"beforeAll failed <&\""}};
  WriteRunnerJson(unrun,folder/"unrun.json");WriteRunnerJUnit(unrun,folder/"unrun.xml");
  invalid=unrun;invalid.unrun[0].test_id=2;Rejected([&]{WriteRunnerJson(invalid,json);});assert(Read(json)==original);
  invalid=unrun;invalid.unrun.push_back(invalid.unrun[0]);Rejected([&]{WriteRunnerJson(invalid,json);});assert(Read(json)==original);
  invalid=unrun;invalid.unrun[0].test_id=0;Rejected([&]{WriteRunnerJUnit(invalid,xml);});
  invalid=unrun;invalid.unrun[0].reason_json="not JSON";Rejected([&]{WriteRunnerJUnit(invalid,xml);});
  const auto blocked=folder/"destination-is-a-directory";std::filesystem::create_directories(blocked);
  const auto sentinel=folder/(blocked.filename().string()+".suitecut-report-"+std::to_string(getpid())+"-2.tmp");{std::ofstream output(sentinel);output<<"unowned sentinel";}
  Rejected([&]{WriteRunnerJson(report,blocked);});assert(Read(sentinel)=="unowned sentinel");
  struct rlimit previous{};assert(getrlimit(RLIMIT_FSIZE,&previous)==0);auto limited=previous;limited.rlim_cur=std::min<rlim_t>(previous.rlim_cur,256);
  const auto old_signal=std::signal(SIGXFSZ,SIG_IGN);assert(old_signal!=SIG_ERR);assert(setrlimit(RLIMIT_FSIZE,&limited)==0);
  bool actual_write_failed=false;try{WriteRunnerJson(report,json);}catch(const std::system_error& error){actual_write_failed=error.code().value()==EFBIG;}
  assert(setrlimit(RLIMIT_FSIZE,&previous)==0);assert(std::signal(SIGXFSZ,old_signal)!=SIG_ERR);assert(actual_write_failed);assert(Read(json)==original);
  const auto parent_file=folder/"not-a-directory";{std::ofstream output(parent_file);output<<"actual file";}Rejected([&]{WriteRunnerJson(report,parent_file/"report.json");});assert(Read(parent_file)=="actual file");
  for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.path().extension()==".tmp")assert(entry.path()==sentinel);
  std::filesystem::remove(sentinel);
  const auto decoded=report_detail::XML("\"\\ud83d\\ude80\\ud800x\\udfff\\u0000\\t\\n\\r\"");assert(decoded.sanitized==3);assert(decoded.text=="\xf0\x9f\x9a\x80\xef\xbf\xbdx\xef\xbf\xbd\xef\xbf\xbd&#9;&#10;&#13;");
  RunnerReport tiny;auto example=report.attempts[1];example.duration_ms=0.001;tiny.attempts.push_back(example);WriteRunnerJUnit(tiny,folder/"submillisecond.xml");
  return 0;
}
