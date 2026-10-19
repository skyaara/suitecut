// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "coverage_ranges.h"
int main(){
 using namespace suitecut;std::vector<CSSCoverageRange> output;std::string error;
 assert(CSSCoverageRanges({{0,100,true},{20,80,false},{30,40,true},{60,70,true}},output,error));assert((output==std::vector<CSSCoverageRange>{{0,20},{30,40},{60,70},{80,100}}));
 assert(CSSCoverageRanges({{0,10,false},{0,10,true},{10,20,true},{5,5,false}},output,error));assert((output==std::vector<CSSCoverageRange>{{0,20}}));
 assert(CSSCoverageRanges({{0,100,true},{0,50,false},{0,20,true}},output,error));assert((output==std::vector<CSSCoverageRange>{{0,20},{50,100}}));
 assert(CSSCoverageRanges({{1,22,true}},output,error));assert((output==std::vector<CSSCoverageRange>{{1,22}}));
 assert(!CSSCoverageRanges({{-1,4,true}},output,error));assert(!CSSCoverageRanges({{10,4,true}},output,error));assert(!CSSCoverageRanges(std::vector<CSSCoverageRule>(16385),output,error));
}
