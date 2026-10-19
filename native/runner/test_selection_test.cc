// SPDX-License-Identifier: MIT
#include "test_selection.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>

int main() {
  namespace sr = suitecut::runner;
  const std::vector<uint64_t> actual{12, 7, std::numeric_limits<uint64_t>::max(), 3, 99};
  assert((sr::SelectTestIndices(actual, {}) == std::vector<size_t>{0,1,2,3,4}));
  assert((sr::SelectTestIndices(actual, {{99,7}}) == std::vector<size_t>{1,4}));
  assert((sr::SelectTestIndices(actual, {{99,7},2,2}) == std::vector<size_t>{4}));
  assert(sr::SelectTestIndices({}, {}).empty());
  // All actual declarations appear once, in order, across disjoint balanced
  // shards, including more shards than tests and the documented upper bound.
  for (uint32_t total : {1U,2U,3U,5U,6U,31U,65536U}) {
    std::vector<size_t> joined;
    size_t largest=0,smallest=actual.size();
    for (uint32_t current=1;current<=total;++current) {
      const auto part=sr::SelectTestIndices(actual, {{},current,total});
      joined.insert(joined.end(),part.begin(),part.end());
      largest=std::max(largest,part.size()); smallest=std::min(smallest,part.size());
    }
    assert((joined==std::vector<size_t>{0,1,2,3,4})); assert(largest-smallest<=1);
  }
  for (const sr::TestSelection& bad : {sr::TestSelection{{7,7}},sr::TestSelection{{8}},
      sr::TestSelection{{0}},sr::TestSelection{{},0,1},sr::TestSelection{{},1,0},
      sr::TestSelection{{},2,1},sr::TestSelection{{},1,65537}}) {
    bool rejected=false; try {sr::SelectTestIndices(actual,bad);} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
  }
  for (const std::vector<uint64_t>& bad : {std::vector<uint64_t>{0},std::vector<uint64_t>{7,7}}) {
    bool rejected=false; try {sr::SelectTestIndices(bad,{});} catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
  }
  assert((sr::SelectTestIndices(actual, {}, {3,7})==std::vector<size_t>{1,3}));
  assert((sr::SelectTestIndices(actual, {{12,7}}, {3,7})==std::vector<size_t>{1}));
  assert(sr::SelectTestIndices(actual, {{12}}, {3,7}).empty());
  assert((sr::SelectTestIndices(actual, {{},2,2}, {3,7})==std::vector<size_t>{3}));
  for(const auto& focused : {std::vector<uint64_t>{0},std::vector<uint64_t>{8},std::vector<uint64_t>{7,7}}){
    bool rejected=false;try{sr::SelectTestIndices(actual,{},focused);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
  }
  sr::TestSelection forbidden;forbidden.forbid_only=true;
  assert((sr::SelectTestIndices(actual,forbidden)==std::vector<size_t>{0,1,2,3,4}));
  bool rejected=false;try{sr::SelectTestIndices(actual,forbidden,{7});}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
  assert(sr::SelectTestIndices(actual,{}, {},true).empty());
  rejected=false;try{sr::SelectTestIndices(actual,forbidden,{},true);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);

}
