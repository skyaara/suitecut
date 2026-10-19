// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iterator>
#include "runner_html.h"
using namespace suitecut::runner;
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::filesystem::path directory(argv[1]);
  std::filesystem::create_directories(directory);
  RunnerReport report;
  report.workers.push_back({0, 101, false, -1, std::string("\0\xff<",3), ""});
  report.workers.push_back({1, 102, true, 9, "", std::string("\xfe\r\n",3)});
  report.attempts = {
    {1, 1, 0, 0, 101, "\"actual title </summary><script>alert(1)</script>\"", "\"first & failure\"", "failed", false, 3},
    {1, 2, 1, 1, 102, "\"actual title </summary><script>alert(1)</script>\"", "\"\"", "passed", true, 2},
    {2, 3, 0, 1, 102, "\"unexpected pass\"", "\"assertion expected to fail\"", "passed", false, 1},
    {3, 4, 0, 1, 102, "\"expected failure\"", "\"expected error\"", "failed", true, 0.000001},
    {4, 5, 0, 1, 102, "\"original skip\"", "\"\"", "skipped", true, 0},
    {UINT64_MAX, UINT64_MAX, 0, 1, 102, "\"\\ud800\\u0000</script>\\ud83d\\ude80\"", "\"real cancellation\\r\\n\"", "interrupted", false, 4},
  };
  report.global_errors_json = {"\"global <img src=x onerror=alert(1)> & failure\""};
  report.unrun={{6,"\"unrun <script>\"","\"real group failed\""},{7,"\"not invoked \\udfff\"","\"actual beforeAll failed\""}};
  WriteRunnerHTML(report, directory/"report.html");
  WriteRunnerJson(report, directory/"report.json");
  const auto read = [](const auto& path) { std::ifstream stream(path, std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream), {}); };
  const auto content=read(directory/"report.html");
  assert(content.find("<script>")==std::string::npos);
  assert(content.find("<img src=")==std::string::npos);
  assert(content.find("data-test-id=\"18446744073709551615\"")!=std::string::npos);
  assert(content.find("Unknown")!=std::string::npos&&content.find("Signal 9")!=std::string::npos);
  assert(content.find("00ff3c")!=std::string::npos&&content.find("fe0d0a")!=std::string::npos);
  assert(content.find("Display replacements: 2")!=std::string::npos);
  auto invalid=report;invalid.attempts[0].error_json="\"\\ud800";
  bool rejected=false;try { WriteRunnerHTML(invalid,directory/"report.html"); } catch(const std::invalid_argument&) { rejected=true; }
  assert(rejected&&read(directory/"report.html")==content);
  const auto blocked=directory/"blocked";{std::ofstream stream(blocked);stream<<"keep";}
  bool failed=false;try { WriteRunnerHTML(report,blocked/"report.html"); } catch(const std::filesystem::filesystem_error&) { failed=true; }
  assert(failed&&read(blocked)=="keep");
}
