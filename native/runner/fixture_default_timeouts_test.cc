// SPDX-License-Identifier: MIT
#include "fixture_default_timeouts.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>static void Reject(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){unsigned n=0;
 {FixtureDefaultTimeouts c;assert(c.Default()==30000&&c.Test(std::nullopt)==30000);++n;}
 {FixtureDefaultTimeouts c;c.Root(1000);assert(c.Default()==1000&&c.Test(100)==100);++n;}
 {FixtureDefaultTimeouts c;c.Root(1000);c.Project(0,2000);assert(c.Default(0)==2000&&c.Default(1)==1000&&c.Test(100,0)==100);++n;}
 {FixtureDefaultTimeouts c;c.Root(0);assert(c.Default()==0&&c.Test(100)==100);++n;}
 {FixtureDefaultTimeouts c;c.Root(1000);c.Project(0,0);assert(c.Default(0)==0&&c.Test(100,0)==100);++n;}
 {FixtureDefaultTimeouts c;c.Root(1000);assert(c.Test(0)==0&&c.Default()==1000);++n;}
 {FixtureDefaultTimeouts c;c.Root(0);Reject([&]{c.Root(1000);});++n;}
 {FixtureDefaultTimeouts c;c.Project(0,0);Reject([&]{c.Project(0,1000);});++n;}
 {FixtureDefaultTimeouts c;Reject([&]{c.Project(128,100);});Reject([&]{(void)c.Default(128);});++n;}
 {FixtureDefaultTimeouts c;Reject([&]{c.Root(120001);});Reject([&]{(void)c.Test(120001);});++n;}
 assert(n==10);std::cout<<"PASS "<<n<<" native fixture/project default timeout controls\n";
}
