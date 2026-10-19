// SPDX-License-Identifier: MIT
#pragma once
#include "json_text.h"
#include <ostream>
#include <vector>
namespace suitecut::runner {
// Render actual unassociated native errors after report validation. JSON scalar
// diagnostics retain their original escaped UTF16 bytes, rather than inventing
// a display stack or associating a worker error with a neighboring attempt.
inline void WriteRunnerGlobalConsole(std::ostream& out,const std::vector<std::string>& errors){
  for(const auto& error:errors)if(!JsonString(error,8192))throw std::invalid_argument("Native console global error scalar rejected");
  for(const auto& error:errors)out<<"Native global error: "<<error<<"\n";
  if(!errors.empty())out<<errors.size()<<(errors.size()==1?" error was":" errors were")<<" not a part of any test, see above for details\n";
}
} // namespace suitecut::runner
