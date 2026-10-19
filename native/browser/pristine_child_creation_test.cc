// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "pristine_child_creation.h"
#include <cassert>
#include <cstdio>
#include <limits>
using Token=suitecut::PristineChildCreation;
int main(){int groups=0;
 {Token t(true,"frame",500);assert(t.Held()&&t.BindLease(7));t.Released();assert(!t.Held()&&t.Select("frame","actual-first",7,true,true,100)&&t.Consume("frame","actual-first",7,101));++groups;}
 {Token t(true,"frame",500);assert(t.BindLease(7));t.Released();assert(!t.Select("frame","warmed",7,true,false,100));assert(t.Select("frame","fresh",7,true,true,100));assert(!t.Select("frame","later",7,true,true,101));++groups;}
 {Token t(true,"frame",500);assert(t.BindLease(7));assert(!t.BindLease(8)&&!t.Select("other","actual",7,true,true,100)&&!t.Select("frame","actual",8,true,true,100)&&!t.Select("frame","actual",7,false,true,100));assert(t.Select("frame","actual",7,true,true,100));++groups;}
 {Token t(true,"frame",110);assert(t.BindLease(7));assert(!t.Select("frame","actual",7,true,true,110));++groups;}
 {Token t(true,"frame",110);assert(t.BindLease(7)&&t.Select("frame","actual",7,true,true,100));assert(!t.Consume("frame","actual",7,110));++groups;}
 {Token t(true,"frame",500);assert(t.BindLease(7)&&t.Select("frame","actual",7,true,true,100)&&t.Consume("frame","actual",7,101));assert(!t.Consume("frame","actual",7,102)&&!t.Select("frame","actual",7,true,true,103)&&!t.Select("frame","replay",7,true,true,103));++groups;}
 {Token t(false,"frame",500);assert(!t.Held()&&!t.BindLease(7)&&!t.Select("frame","actual",7,true,true,100)&&!t.Consume("frame","actual",7,100));++groups;}
 {Token t(true,"frame",500);assert(t.BindLease(7)&&t.Select("frame","actual",7,true,true,100));/* Same unique realm may get its initial loader metadata before capture. */assert(t.Select("frame","actual",7,true,false,101)&&t.Consume("frame","actual",7,102));++groups;}
 {Token t(true,"frame",500);assert(t.BindLease(7));assert(!t.Consume("frame","unselected",7,100));assert(t.Select("frame","actual",7,true,true,100));assert(!t.Consume("frame","different",7,101)&&t.Consume("frame","actual",7,102));++groups;}
 {Token t(true,"frame",500);assert(!t.BindLease(0));assert(t.BindLease(7));assert(!t.Select("frame",std::string(257,'x'),7,true,true,100));++groups;}
 {for(auto deadline:{0.0,-1.0,std::numeric_limits<double>::infinity()}){Token t(true,"frame",deadline);assert(!t.BindLease(7));}Token empty(true,"",500);assert(!empty.BindLease(7));Token huge(true,std::string(129,'x'),500);assert(!huge.BindLease(7));++groups;}
 std::printf("PristineChildCreation: %d controlled groups PASS\n",groups);
}
