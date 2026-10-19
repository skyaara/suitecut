// SPDX-License-Identifier: MIT
#pragma once
#include "suite_file_owner.h"
#include "test_group_tree.h"
namespace suitecut::runner {
// This identity is an actual native file receipt, never a project index.
class FileWorkGroups {
 public:
  FileWorkGroups(SuiteFileToken file,const NativeTestGroupTree& tree,
      const std::vector<uint64_t>& selected,bool fully_parallel)
      :file_(file),groups_(tree.Resolve(selected,fully_parallel)){
    if(!file.id)throw std::invalid_argument("Native file work-group owner rejected");
  }
  const SuiteFileToken& File()const{return file_;}
  const std::vector<SelectedTestGroup>& Groups()const{return groups_;}
 private:
  SuiteFileToken file_;std::vector<SelectedTestGroup> groups_;
};
}
