// SPDX-License-Identifier: MIT
#include "suite_deadline.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
int main(){
 namespace sr=suitecut::runner;
 sr::NativeSuiteDeadline disabled(0,100);assert(!disabled.Observe(1e30)&&!disabled.Expired()&&!disabled.Time());
 sr::NativeSuiteDeadline deadline(500,100);assert(!deadline.Observe(599.999));assert(deadline.Observe(600));assert(deadline.Time()==600);assert(deadline.Observe(0));assert(deadline.Reason()=="Native suite original global timeout of 500ms exceeded");
 sr::NativeSuiteDeadline maximum(2147483647,0);assert(!maximum.Observe(2147483646));assert(maximum.Observe(2147483647));
 bool rejected=false;try{sr::NativeSuiteDeadline bad(2147483648ULL,0);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 rejected=false;try{sr::NativeSuiteDeadline bad(1,std::numeric_limits<double>::infinity());}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 rejected=false;try{disabled.Observe(std::numeric_limits<double>::quiet_NaN());}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
}
