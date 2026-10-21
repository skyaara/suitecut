// SPDX-License-Identifier: MIT
// Independent executable proof of the public native C++ browser facade.
#include <suitecut/browser.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

namespace {
using suitecut::Json;
using Clock = std::chrono::steady_clock;
double Milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::string Read(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot read " + path);
  std::ostringstream output;
  output << stream.rdbuf();
  return output.str();
}
std::string DataUrl(const std::string& html) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string output = "data:text/html;charset=utf-8,";
  output.reserve(output.size() + html.size() * 3);
  for (unsigned char value : html) {
    if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') || value == '-' || value == '_' ||
        value == '.' || value == '~') output.push_back(static_cast<char>(value));
    else { output.push_back('%'); output.push_back(hex[value >> 4]); output.push_back(hex[value & 15]); }
  }
  return output;
}
std::int64_t ProcKiB(const std::string& text, const std::string& key) {
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.rfind(key + ":", 0) != 0) continue;
    std::istringstream value(line.substr(key.size() + 1));
    std::int64_t number = 0;
    value >> number;
    return number;
  }
  return 0;
}
Json Sample(const std::string& label, Clock::time_point start) {
#ifdef __APPLE__
  proc_taskinfo info{};
  if(proc_pidinfo(getpid(),PROC_PIDTASKINFO,0,&info,sizeof(info))!=sizeof(info))
    throw std::runtime_error("Cannot inspect native client task memory");
  const int bytes=proc_pidinfo(getpid(),PROC_PIDLISTFDS,0,nullptr,0);
  std::vector<proc_fdinfo> descriptors(static_cast<size_t>(bytes)/sizeof(proc_fdinfo)+32);
  const int used=proc_pidinfo(getpid(),PROC_PIDLISTFDS,0,descriptors.data(),static_cast<int>(descriptors.size()*sizeof(proc_fdinfo)));
  rusage usage{};getrusage(RUSAGE_SELF,&usage);
  return Json{{"label",label},{"elapsedMs",Milliseconds(start)},{"pssKiB",nullptr},
              {"rssKiB",static_cast<std::int64_t>(info.pti_resident_size/1024)},
              {"peakRssKiB",static_cast<std::int64_t>(usage.ru_maxrss/1024)},
              {"fds",used/static_cast<int>(sizeof(proc_fdinfo))}};
#else
  const auto smaps = Read("/proc/self/smaps_rollup");
  const auto status = Read("/proc/self/status");
  std::int64_t fds = 0;
  for (const auto& ignored : std::filesystem::directory_iterator("/proc/self/fd")) {
    (void)ignored; ++fds;
  }
  return Json{{"label", label}, {"elapsedMs", Milliseconds(start)},
              {"pssKiB", ProcKiB(smaps, "Pss")}, {"rssKiB", ProcKiB(smaps, "Rss")},
              {"peakRssKiB", ProcKiB(status, "VmHWM")}, {"fds", fds}};
#endif
}
void Check(bool condition, const std::string& description, Json& report) {
  if (!condition) throw std::runtime_error("Check failed: " + description);
  report["checks"].as_array().push_back(description);
}
std::string FindButtonRef(const Json& nodes, const std::string& name) {
  if (!nodes.is_array()) return {};
  for (const auto& node : nodes.as_array()) {
    if (!node.is_object()) continue;
    if (node.contains("role") && node.contains("name") && node.contains("ref") &&
        node.at("role").is_string() && node.at("name").is_string() && node.at("ref").is_string() &&
        node.at("role").as_string() == "button" && node.at("name").as_string() == name)
      return node.at("ref").as_string();
    if (node.contains("children")) {
      const auto nested = FindButtonRef(node.at("children"), name);
      if (!nested.empty()) return nested;
    }
  }
  return {};
}
template<class Operation>
void Measure(Json& report, const std::string& name, int count, Operation operation) {
  Json::array samples;
  samples.reserve(static_cast<std::size_t>(count));
  std::vector<double> sorted;
  sorted.reserve(static_cast<std::size_t>(count));
  const auto cpu = std::clock();
  for (int index = 0; index < count; ++index) {
    const auto start = Clock::now();
    operation(index);
    const double elapsed = Milliseconds(start);
    samples.emplace_back(elapsed); sorted.push_back(elapsed);
  }
  std::sort(sorted.begin(), sorted.end());
  report["timings"][name] = Json{{"count", count}, {"samplesMs", std::move(samples)},
    {"p50Ms", sorted[(sorted.size() - 1) / 2]},
    {"p95Ms", sorted[static_cast<std::size_t>((sorted.size() - 1) * .95)]},
    {"clientCPUMs", 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC}};
}
}  // namespace

int main(int argc, char** argv) {
  std::map<std::string, std::string> args;
  for (int index = 1; index < argc; index += 2) {
    if (index + 1 == argc) { std::cerr << "Expected --option value\n"; return 2; }
    args[argv[index]] = argv[index + 1];
  }
  const auto option = [&](const std::string& key, const std::string& fallback) {
    const auto found = args.find(key); return found == args.end() ? fallback : found->second;
  };
  const auto output = option("--output", ".suitecut/native-client/cpp-results.json");
  const int iterations = std::stoi(option("--iterations", "30"));
  const int cycles = std::stoi(option("--cycles", "50"));
  Json report{{"client", "native-cpp"}, {"compiler", __VERSION__}, {"iterations", iterations},
    {"cycles", cycles}, {"passed", false}, {"checks", Json::array{}}, {"samples", Json::array{}},
    {"timings", Json::object{}}, {"framesPerSecond", 60},
    {"viewport", Json{{"width", 1280}, {"height", 720}}},
    {"scope", "Same CEF binary, viewport and fixture as Node client. Application CPU/RSS excludes browser processes."}};
  const auto started = Clock::now();
  const auto cpu_before = std::clock();
  std::optional<suitecut::Browser> browser;
  int result = 0;
  try {
    if (iterations < 1 || iterations > 10000 || cycles < 0 || cycles > 10000)
      throw std::invalid_argument("Invalid iteration/cycle bound");
    const auto url = DataUrl(Read(option("--fixture", "native/client/tools/fixture.html")));
    suitecut::LaunchOptions launch;
    launch.executable = std::filesystem::absolute(option("--browser", ".suitecut/engine-performance-fixes/runtime/suitecut-browser")).string();
    launch.width = 1280; launch.height = 720; launch.frames_per_second = 60; launch.headless = true;
    const auto launching = Clock::now();
    browser.emplace(suitecut::Browser::launch(launch));
    report["launchToReadyMs"] = Milliseconds(launching);
    report["ready"] = Json::parse(browser->transport()->ready().header);
    std::cout << Json{{"event", "ready"}, {"client", "native-cpp"},
      {"launchToReadyMs", report.at("launchToReadyMs")}}.dump() << std::endl;
    auto page = browser->main_page();
    page.navigate(url);
    page.wait_for_load_state();
    Check(page.wait_for_function("document.readyState==='complete'").as_bool(),"CEF native predicate wait and load-state wait",report);
    Check(page.evaluate("40+2").as_int() == 42, "small evaluation", report);
    page.get_by_role("textbox", "Name").fill("Native λ 漢字");
    Check(page.locator("#name").input_value() == "Native λ 漢字", "locator input value getter", report);
    page.locator("#name").focus();
    page.keyboard_press("ControlOrMeta+A");page.insert_text("Keyboard Ω 😀");
    Check(page.locator("#name").input_value()=="Keyboard Ω 😀","platform select-all chord and Unicode keyboard insertion",report);
    page.locator("#name").fill("Native λ 漢字");
    Check(page.locator("#name").attribute("aria-label").as_string() == "Name", "locator attribute getter", report);
    Check(page.locator("#commit").inner_text() == "Commit transaction", "locator inner text getter", report);
    page.locator("#name").press("End");
    page.get_by_role("button", "Commit transaction").click();
    Check(page.locator("#status").text_content().as_string() == "Saved Native λ 漢字", "form value and click outcome", report);
    Check(page.evaluate("window.lastTrusted").as_bool(), "genuine trusted pointer event", report);
    page.get_by_role("checkbox", "Consent").check();
    page.get_by_role("checkbox", "Consent").uncheck();
    Check(!page.evaluate("document.querySelector('#consent').checked").as_bool(), "checkbox uncheck", report);
    page.get_by_role("checkbox", "Consent").check();
    page.get_by_role("combobox", "Region").select_option("east");
    Check(page.evaluate("document.querySelector('#consent').checked&&document.querySelector('#region').value==='east'").as_bool(),
      "checkbox and native select", report);
    page.get_by_role("button", "Shadow action").click();
    Check(page.evaluate("window.shadowTrusted").as_bool(), "shadow-root role control", report);
    const auto reference = FindButtonRef(page.observe(), "Commit transaction");
    Check(!reference.empty(), "observation contains an engine-issued button ref", report);
    page.get_by_ref(reference).click();
    Check(page.evaluate("window.clicks").as_int() == 2, "engine-issued observation ref drives a genuine click", report);
    const auto image = page.screenshot();
    const std::vector<std::uint8_t> signature{137,80,78,71,13,10,26,10};
    Check(image.size() > 100 && std::equal(signature.begin(), signature.end(), image.begin()), "actual PNG screenshot bytes", report);
    report["screenshotBytes"] = static_cast<std::int64_t>(image.size());
    std::filesystem::create_directories(std::filesystem::path(output).parent_path());
    std::ofstream png(std::filesystem::path(output).parent_path() / "screenshot.png", std::ios::binary);
    png.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size())); png.close();
    page.navigate("about:blank"); page.navigate(url + "#shift");
    page.get_by_role("button", "Moving target").click();
    Check(page.evaluate("window.race.clicks===1&&window.race.traps===0&&window.race.downs===1&&window.race.trusted===true").as_bool(),
      "hover-shift final target revalidation", report);
    page.navigate("about:blank"); page.navigate(url + "#overlay");
    bool blocked = false;
    page.set_timeout(std::chrono::milliseconds{300});
    try { page.get_by_role("button", "Moving target").click(); }
    catch (const std::exception&) { blocked = true; }
    Check(blocked, "overlay locator fails before pressing", report);
    Check(page.evaluate("window.race.downs===0&&window.race.overlayDowns===0&&window.race.traps===0").as_bool(),
      "overlay blocks stale pointer down", report);
    page.set_timeout(std::chrono::milliseconds{3000});
    page.navigate("about:blank"); page.navigate(url);
    for (int index = 0; index < 5; ++index) (void)page.evaluate("40+2");
    std::int64_t events_drained = 0;
    const auto drain_events = [&] {
      while (browser->transport()->poll()) ++events_drained;
    };
    drain_events();
    report["samples"].as_array().push_back(Sample("warm", started));
    Measure(report, "evaluateSmall", iterations, [&](int) {
      if (page.evaluate("40+2").as_int() != 42) throw std::runtime_error("Small evaluation mismatch");
    });
    Measure(report, "evaluateLarge", std::max(3, iterations / 5), [&](int) {
      const auto value = page.evaluate("'x'.repeat(262144)");
      if (value.as_string().size() != 262144 || value.as_string().find_first_not_of('x') != std::string::npos)
        throw std::runtime_error("Large evaluation bytes mismatch");
    });
    Measure(report, "queryCount", iterations, [&](int) {
      if (page.locator("#noise .entry").count() != 3000) throw std::runtime_error("Dense DOM count mismatch");
    });
    Measure(report, "locatorClick", iterations, [&](int) { page.get_by_role("button", "Commit transaction").click(); });
    Check(page.evaluate("window.clicks").as_int() == iterations, "measured click count matches exactly", report);
    Measure(report, "locatorFill", iterations, [&](int index) { page.get_by_role("textbox", "Name").fill("value-" + std::to_string(index)); });
    Check(page.evaluate("document.querySelector('#name').value").as_string() == "value-" + std::to_string(iterations - 1), "measured fill outcome", report);
    for (int index = 0; index < cycles; ++index) {
      auto context = browser->new_context();
      auto isolated = context.new_page();
      isolated.navigate(url);
      isolated.get_by_role("textbox", "Name").fill("cycle-" + std::to_string(index));
      if (isolated.evaluate("document.querySelector('#name').value").as_string() != "cycle-" + std::to_string(index))
        throw std::runtime_error("Isolated page state mismatch");
      isolated.close(); context.close();
      drain_events();
      if ((index + 1) % 10 == 0 || index + 1 == cycles) {
        report["samples"].as_array().push_back(Sample("cycle-" + std::to_string(index + 1), started));
        std::cout << Json{{"event", "batch"}, {"client", "native-cpp"}, {"cycles", index + 1}}.dump() << std::endl;
      }
    }
    Check(page.evaluate("document.querySelector('#name').value").as_string() == "value-" + std::to_string(iterations - 1),
      "context lifecycle preserves main page state", report);
    drain_events();
    report["eventsDrained"] = events_drained;
    report["passed"] = true;
  } catch (const std::exception& error) { report["error"] = error.what(); result = 1; }
  const auto transport = browser ? browser->transport() : nullptr;
  const auto closing = Clock::now();
  try { if (browser) browser->close(); }
  catch (const std::exception& error) { report["closeError"] = error.what(); report["passed"] = false; result = 1; }
  report["closeMs"] = Milliseconds(closing);
  if (transport) {
    const auto stats = transport->stats();
    report["transport"] = Json{{"commands", static_cast<std::int64_t>(stats.commands)},
      {"responses", static_cast<std::int64_t>(stats.responses)}, {"pending", static_cast<std::int64_t>(stats.pending)},
      {"queuedEventBytes", static_cast<std::int64_t>(stats.queued_event_bytes)},
      {"droppedEvents", static_cast<std::int64_t>(stats.dropped_events)}};
    if (stats.pending != 0) { report["passed"] = false; result = 1; }
  }
  report["samples"].as_array().push_back(Sample("closed", started));
  report["totalMs"] = Milliseconds(started);
  report["clientCPUMs"] = 1000.0 * (std::clock() - cpu_before) / CLOCKS_PER_SEC;
  std::filesystem::create_directories(std::filesystem::path(output).parent_path());
  std::ofstream stream(output); stream << report.dump() << '\n'; stream.close();
  std::cout << Json{{"event", "done"}, {"client", "native-cpp"}, {"passed", report.at("passed")}, {"output", output}}.dump() << std::endl;
  return result;
}
