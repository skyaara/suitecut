// SPDX-License-Identifier: MIT
#include "runner_report.h"
namespace suitecut::runner {
// Keep the genuine native writer in a separate translation unit so the focused
// control does not optimize the full writer and merger in one compiler process.
void WriteRepeatReportFixture(const RunnerReport& report,const std::filesystem::path& path){
  WriteRunnerJson(report,path);
}
}
