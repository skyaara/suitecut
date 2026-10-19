// SPDX-License-Identifier: MIT
#include "project_output_roots.h"
#include <iostream>
#include <stdexcept>
namespace sr=suitecut::runner;
unsigned checks=0;
void Check(bool value){++checks;if(!value)throw std::runtime_error("Native project output policy control failed "+std::to_string(checks));}
template<class F>void Reject(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected);}
int main(){
 sr::NativeProjectOutputRoots roots;
 Reject([&]{(void)roots.Select(0,"/fallback");});
 roots.Declare(0,2,"relative/../project-a","/actual/config/suite.mjs");
 roots.Declare(1,2,"/absolute/project-b","/actual/config/suite.mjs");
 Reject([&]{roots.Declare(1,2,"/duplicate","/actual/config/suite.mjs");});
 roots.Freeze(2);Check(roots.Select(0,"/fallback")=="/actual/config/project-a");Check(roots.Select(1,"/fallback")=="/absolute/project-b");Check(roots.Declarations().size()==2);
 Reject([&]{roots.Freeze(2);});Reject([&]{roots.Declare(0,2,"/after-freeze","/actual/config/suite.mjs");});Reject([&]{(void)roots.Select(2,"/fallback");});Reject([&]{(void)roots.Select(std::nullopt,"/fallback");});
 sr::NativeProjectOutputRoots fallback;fallback.Declare(1,2,"../outside","/actual/config/suite.mjs");fallback.Freeze(2);
 Check(fallback.Select(0,"/fallback")=="/fallback");Check(fallback.Select(1,"/fallback")=="/actual/outside");
 sr::NativeProjectOutputRoots anonymous;anonymous.Freeze(0);Check(anonymous.Select(std::nullopt,"/fallback")=="/fallback");Reject([&]{(void)anonymous.Select(0,"/fallback");});
 sr::NativeProjectOutputRoots invalid;
 Reject([&]{invalid.Declare(1,1,"/unknown","/actual/config/suite.mjs");});Reject([&]{invalid.Declare(0,0,"/unknown","/actual/config/suite.mjs");});
 Reject([&]{invalid.Declare(0,1,"/root","relative/suite.mjs");});Reject([&]{invalid.Declare(0,1,std::string("/nul\0path",9),"/actual/config/suite.mjs");});
 Reject([&]{invalid.Declare(0,1,std::string("/bad\xc0\x80"),"/actual/config/suite.mjs");});Reject([&]{invalid.Declare(0,1,std::string(7901,'x'),"/actual/config/suite.mjs");});
 invalid.Declare(0,1,"/declared","/actual/config/suite.mjs");Reject([&]{invalid.Freeze(0);});invalid.Freeze(1);Check(invalid.Select(0,"relative fallback")=="/declared");Check(invalid.RetainedBytes()<sr::NativeProjectOutputRoots::kBytes);
 sr::NativeProjectOutputRoots empty;empty.Declare(0,1,"","/actual/config/suite.mjs");empty.Freeze(1);Check(empty.Select(0,"/fallback").lexically_normal().parent_path()=="/actual/config");
 std::cout<<"Native actual-project output root policy "<<checks<<" controls PASS\n";
}
