// SPDX-License-Identifier: MIT
#include "console_array_preview.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <random>

int main() {
  assert(SuiteCutFormatConsoleArray("Array(101)",{{"1","1"},{"10","2"},{"100","3"}},false)=="[empty, 1, empty x 8, 2, empty x 89, 3]");
  assert(SuiteCutFormatConsoleArray("Array(3)",{{"0","a"},{"1","b"},{"2","c"}},false)=="[a, b, c]");
  assert(SuiteCutFormatConsoleArray("Array(4)",{{"0","a"},{"name","value"}},false)=="[a, name: value, empty x 3]");
  assert(SuiteCutFormatConsoleArray("Array(101)",{{"1","1"}},true)=="[empty, 1, …]");
  assert(SuiteCutFormatConsoleArray("Array(0)",{},false)=="[]");
  assert(SuiteCutFormatConsoleArray("Array(4294967295)",{},false)=="[empty x 4294967295]");
  assert(!SuiteCutConsoleArrayIndex("4294967295") && !SuiteCutConsoleArrayIndex("01") && !SuiteCutConsoleArrayIndex("-1"));
  assert(!SuiteCutConsoleArrayLength("Array(999999999999999999999999)") && !SuiteCutConsoleArrayLength("Array(01)") && !SuiteCutConsoleArrayLength("Array(1x)"));
  assert(SuiteCutFormatConsoleArray("Array(2)",{{"1","b"},{"0","a"}},false)=="[b, a]");
  assert(SuiteCutFormatConsoleArray("Array(1)",{{"0",std::string(1000000,'x')}},false).size()<=65536);
  std::mt19937_64 random(0xc0750ae);
  for(int i=0;i<1000000;++i) {
    const auto first=random()%1000000, gap=random()%1000000;
    const auto result=SuiteCutFormatConsoleArray("Array("+std::to_string(first+gap+3)+")",{{std::to_string(first),"one"},{std::to_string(first+gap+1),"two"}},random()%2);
    assert(result.size()<=65536 && result.front()=='[' && result.back()==']');
  }
}
