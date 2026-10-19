// SPDX-License-Identifier: MIT
#include "runner_global_console.h"
#include "fixture_result_duration.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <sstream>
using namespace suitecut::runner;
int main(){unsigned n=0;
 {std::ostringstream out;WriteRunnerGlobalConsole(out,{});assert(out.str().empty());++n;}
 {std::ostringstream out;WriteRunnerGlobalConsole(out,{"\"actual global\""});assert(out.str()=="Native global error: \"actual global\"\n1 error was not a part of any test, see above for details\n");++n;}
 {std::ostringstream out;WriteRunnerGlobalConsole(out,{"\"a\"","\"b\"","\"c\""});assert(out.str().find("3 errors were not a part of any test, see above for details\n")!=std::string::npos);++n;}
 {std::ostringstream out;WriteRunnerGlobalConsole(out,{"\"same\"","\"same\""});assert(out.str()=="Native global error: \"same\"\nNative global error: \"same\"\n2 errors were not a part of any test, see above for details\n");++n;}
 {std::ostringstream out;const std::string raw="\"raw \\ud800 \\n \\\"stack\\\"\"";WriteRunnerGlobalConsole(out,{raw});assert(out.str().find(raw)!=std::string::npos);++n;}
 {std::ostringstream out;bool rejected=false;try{WriteRunnerGlobalConsole(out,{"\"valid\"","forged scalar"});}catch(const std::invalid_argument&){rejected=true;}assert(rejected&&out.str().empty());++n;}
 {assert(FixtureResultDuration(nullptr,123456)==0);++n;}
 {bool rejected=false;try{(void)FixtureResultDuration(nullptr,std::numeric_limits<double>::quiet_NaN());}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++n;}
 {const FixtureAttemptClockOwner owner{1,2,3,123};FixtureAttemptClocks timing(owner,100,10,5);assert(FixtureResultDuration(&timing,20)==15);auto charged=timing.Active(owner);assert(charged&&charged->Exclude(17,20));assert(FixtureResultDuration(&timing,300)==15);assert(charged->Resume(17,300));assert(timing.BeginCleanup(owner,310));assert(FixtureResultDuration(&timing,320)==35);assert(timing.Finish(owner,320));assert(FixtureResultDuration(&timing,1000)==35);++n;}
 {const FixtureAttemptClockOwner owner{1,2,3,123};FixtureAttemptClocks timing(owner,100,10);(void)FixtureResultDuration(&timing,20);bool rejected=false;try{(void)FixtureResultDuration(&timing,19);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++n;}
 assert(n==10);std::cout<<"PASS "<<n<<" controlled native actual-global console and admitted-Test result duration cases; no author/process claim\n";
}
