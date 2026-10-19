// SPDX-License-Identifier: MIT
#include "runner_html.h"
namespace suitecut::runner {
// Genuine atomic native emitters, separate from the parser/merger test TU.
void WriteFailOnFlakyFixture(const RunnerReport& report,const std::filesystem::path& stem){
  WriteRunnerJson(report,stem.string()+".json");WriteRunnerJUnit(report,stem.string()+".xml");WriteRunnerHTML(report,stem.string()+".html");
}
}
