// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include "include/cef_values.h"
namespace suitecut {
// File operations execute on a bounded native worker. Their original deadline
// and cancellation token survive queueing, ZIP decoding and atomic commit.
class NativeHARFiles {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  enum class Content { Embed, Attach, Omit };
  struct Action {
    const std::atomic_bool& canceled;
    double deadline;
    std::function<double()> now;
  };
  static constexpr size_t kJSONBytes=32*1048576,kFileBytes=128*1048576,kBodyBytes=2097152,kResources=4096;
  static bool Read(const std::string& path,const Action& action,Dict& archive,std::string& error,bool allow_empty_log=false);
  static bool Write(const std::string& path,Dict archive,Content content,bool zipped,
                    const Action& action,std::string& error);
};
}
