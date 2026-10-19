// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
namespace suitecut::runner {
struct OutputDirectoryOwner {
  uint64_t test_id=0,generation=0;unsigned worker_index=0;int64_t pid=0;
  auto Key()const{return std::tuple(test_id,generation,worker_index,pid);}
  bool operator==(const OutputDirectoryOwner&)const=default;
};
enum class OutputDirectoryStatus {Preparing,Ready,Failed,TimedOut,Interrupted};
struct OutputDirectoryRecord {
  uint64_t id=0;OutputDirectoryOwner owner;std::string path;
  double started=0,deadline=0;std::optional<double> created,writer_received,received,assigned;
  uint64_t device=0,inode=0,parent_device=0,parent_inode=0;int64_t writer_pid=0,rollback_pid=0;
  bool writer_signaled=false,rollback_signaled=false;int writer_exit=-1,rollback_exit=-1;
  bool ready_receipt=false,rollback_receipt=false,foreign_preserved=false;
  OutputDirectoryStatus status=OutputDirectoryStatus::Preparing;std::string error,cleanup_error;
};
} // namespace suitecut::runner
