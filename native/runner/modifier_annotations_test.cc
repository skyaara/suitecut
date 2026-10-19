// SPDX-License-Identifier: MIT
#include "modifier_annotations.h"
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Invalid(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(int argc,char**argv){
 const auto missing=ModifierAnnotations::Prepare("skip","-",false,"-",0,0);assert(ModifierAnnotationJSON(missing)=="{\"type\":\"skip\"}");
 const auto empty=ModifierAnnotations::Prepare("skip","-",true,"-",0,0);assert(ModifierAnnotationJSON(empty)=="{\"type\":\"skip\",\"description\":\"\"}");
 const auto actual=ModifierAnnotations::Prepare("fail","00d80000",true,"2f006100",7,14);assert(ModifierAnnotationJSON(actual)=="{\"type\":\"fail\",\"description\":\"\\ud800\\u0000\",\"location\":{\"file\":\"\\u002f\\u0061\",\"line\":7,\"column\":14}}");
 Invalid([&]{ModifierAnnotations::Prepare("guessed","-",false,"-",0,0);});Invalid([&]{ModifierAnnotations::Prepare("skip","6100",false,"-",0,0);});
 Invalid([&]{ModifierAnnotations::Prepare("skip","-",false,"-",7,14);});Invalid([&]{ModifierAnnotations::Prepare("skip","-",false,"2f006100",0,14);});Invalid([&]{ModifierAnnotations::Prepare("skip","-",false,"2f006100",1,2147483648ULL);});
 ModifierAnnotations ledger;assert(ledger.Snapshot()=="0");ledger.Append(actual);assert(ledger.Values().size()==1&&ledger.Snapshot()=="1 fail 1 00d80000 2f006100 7 14");ledger.Clear();assert(ledger.Values().empty()&&ledger.Snapshot()=="0");
 for(size_t n=0;n<ModifierAnnotations::kCount;++n){ledger.Append(missing);}
 assert(!ledger.CanAppend(missing));Invalid([&]{ledger.Append(missing);});ledger.Clear();
 std::string huge;for(size_t n=0;n<8192;++n)huge+="00d8";const auto large=ModifierAnnotations::Prepare("skip",huge,true,"2f006100",1,1);assert(ledger.CanAppend(large));ledger.Append(large);assert(!ledger.CanAppend(large));assert(ledger.Snapshot().size()<65536);Invalid([&]{ModifierAnnotations::Prepare("skip",huge+"00d8",true,"-",0,0);});
 RunnerReport r;r.workers.push_back({1,getpid(),false,0,{},{}});ReportAttempt attempt{1,1,0,1,getpid(),"\"actual\"","\"\"","failed",true,1,std::nullopt,"failed"};attempt.annotations.push_back(actual);r.attempts.push_back(attempt);assert(report_detail::Validate(r).expected==1);
 {auto bad=r;bad.attempts[0].annotations[0].reason_json="\"invented\"";Invalid([&]{report_detail::Validate(bad);});}
 {auto bad=r;for(size_t n=0;n<257;++n)bad.attempts[0].annotations.push_back(missing);Invalid([&]{report_detail::Validate(bad);});}
 {
  const auto skip=ModifierAnnotations::Prepare("skip","7200",true,"2f006100",5,14);RunnerReport skipped=r;auto& a=skipped.attempts[0];a.status="skipped";a.expected_status="skipped";a.skip_reason_json=skip.reason_json;a.annotations={skip};
  skipped.skip_causes.push_back({90,1,7,8,1,1,2,1,getpid(),skip.reason_json,"skipped",skip});skipped.runtime_skipped.push_back({2,90,"\"native unadmitted declaration\""});assert(report_detail::Validate(skipped).skipped==2);
  {auto bad=skipped;bad.skip_causes[0].annotation->location->column=15;Invalid([&]{report_detail::Validate(bad);});}
  {auto bad=skipped;bad.skip_causes[0].annotation->type="fail";Invalid([&]{report_detail::Validate(bad);});}
  if(argc>1){const auto directory=std::filesystem::path(argv[1]);WriteRunnerJson(r,directory/"annotations.json");WriteRunnerJUnit(r,directory/"annotations.xml");WriteRunnerHTML(r,directory/"annotations.html");WriteRunnerJson(skipped,directory/"beforeall-annotations.json");WriteRunnerJUnit(skipped,directory/"beforeall-annotations.xml");}
 }
 std::cout<<"PASS14 native runtime modifier annotation bounds/provenance groups\n";
}
