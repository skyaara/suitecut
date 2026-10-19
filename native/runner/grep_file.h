// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
namespace suitecut::runner {
// This root changes only the metadata component of one actual SuiteFileOwner;
// it neither discovers additional files nor replaces that owner's identity.
inline std::string GrepFileComponent(const std::filesystem::path& suite,std::string_view directory){
  const auto actual=std::filesystem::canonical(suite);
  const auto root=directory.empty()?actual.parent_path():std::filesystem::canonical(std::filesystem::path(directory));
  if(!std::filesystem::is_directory(root))throw std::invalid_argument("Native grep test-directory is not an actual directory");
  const auto relative=actual.lexically_relative(root);
  if(relative.empty()||relative.is_absolute()||relative=="."||*relative.begin()=="..")
    throw std::invalid_argument("Native actual suite is outside grep test-directory");
  return relative.generic_string();
}
}
