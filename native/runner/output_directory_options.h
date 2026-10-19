// SPDX-License-Identifier: MIT
#pragma once
#include "test_attachments.h"
#include <filesystem>
namespace suitecut::runner {
// A user root is a bounded filesystem UTF8 path. Checking this syntax performs
// no IO and makes no existence/permission claim; the owned native actor does IO.
inline std::string NativeOutputDirectoryRoot(const std::string& value){
  if(value.empty()||value.size()>7900||value.find('\0')!=std::string::npos||!std::filesystem::path(value).is_absolute())throw std::invalid_argument("Native outputDirectory must be a bounded absolute filesystem UTF8 path");
  try{(void)AttachmentPathJSON(value);}catch(const std::exception&){throw std::invalid_argument("Native outputDirectory must be a bounded absolute filesystem UTF8 path");}
  const auto normalized=std::filesystem::path(value).lexically_normal().string();
  if(normalized.empty()||normalized.size()>7900)throw std::invalid_argument("Native outputDirectory normalized path exceeds bound");
  return normalized;
}
} // namespace suitecut::runner
