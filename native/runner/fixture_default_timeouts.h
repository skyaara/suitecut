// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
namespace suitecut::runner {
// Values are raw signature-pinned declarations. Only the native selected
// project admission may supply a project index; a selected test is never used
// to infer the original worker-fixture default.
class FixtureDefaultTimeouts {
 public:
  static constexpr uint64_t kDefault=30000,kMaximum=120000;
  void Root(uint64_t timeout){Validate(timeout);if(root_)throw std::invalid_argument("Duplicate native root timeout");root_=timeout;}
  void Project(size_t index,uint64_t timeout){Validate(timeout);if(index>=128||!projects_.emplace(index,timeout).second)throw std::invalid_argument("Native project timeout owner rejected");}
  uint64_t Default(std::optional<size_t> project=std::nullopt)const{
    if(project){if(*project>=128)throw std::invalid_argument("Native default timeout project rejected");const auto found=projects_.find(*project);if(found!=projects_.end())return found->second;}
    return root_.value_or(kDefault);
  }
  uint64_t Test(std::optional<uint64_t> declared,std::optional<size_t> project=std::nullopt)const{if(declared){Validate(*declared);return *declared;}return Default(project);}
 private:
  static void Validate(uint64_t timeout){if(timeout>kMaximum)throw std::invalid_argument("Native default timeout exceeds bound");}
  std::optional<uint64_t> root_;
  std::map<size_t,uint64_t> projects_;
};
} // namespace suitecut::runner
