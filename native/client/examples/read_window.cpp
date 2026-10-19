// SPDX-License-Identifier: MIT
#include <charconv>
#include <deque>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "suitecut/browser.hpp"

namespace {
struct PendingRead {
  std::size_t index;
  suitecut::Request request;
};

std::size_t window_size(std::string_view text) {
  std::size_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      value < 1 || value > 16)
    throw std::invalid_argument("max-in-flight must be an integer from 1 to 16");
  return value;
}

// Keep only a window of requests/results. Waiting in submission order avoids
// polling or worker threads; later requests still run while the oldest waits.
void read_window(const std::shared_ptr<suitecut::Transport>& transport, int page_id,
                 const std::vector<std::string>& expressions, std::size_t maximum) {
  std::deque<PendingRead> pending;
  std::size_t next = 0;
  try {
    while (next < expressions.size() || !pending.empty()) {
      while (next < expressions.size() && pending.size() < maximum) {
        auto request = transport->submit_json(
            suitecut::Json{{"command", "evaluate"}, {"pageId", page_id},
                           {"expression", expressions[next]}},
            suitecut::Timeout{30000});
        const int request_id = request.id;
        try {
          pending.push_back(PendingRead{next, std::move(request)});
        } catch (...) {
          try { transport->cancel(request_id); } catch (...) {}
          throw;
        }
        ++next;
      }
      auto packet = pending.front().request.result.get();
      auto result = suitecut::response_json(packet);
      // Keep the native evaluation envelope, including JS exception metadata.
      std::cout << suitecut::Json{{"index", static_cast<std::int64_t>(pending.front().index)},
                                  {"pageId", page_id}, {"nativeResult", std::move(result)}}.dump()
                << '\n';
      if (!std::cout) throw std::runtime_error("Cannot write native read result");
      pending.pop_front();
    }
  } catch (...) {
    // Dropping a future does not cancel its browser work. Preserve the original
    // error even when a transport failure also prevents remote cancellation.
    for (const auto& read : pending) {
      try { transport->cancel(read.request.id); } catch (...) {}
    }
    throw;
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: suitecut-native-read-window /absolute/suitecut-browser "
                 "[max-in-flight [independent-read-expression ...]]\n";
    return 2;
  }
  try {
    const auto maximum = argc > 2 ? window_size(argv[2]) : std::size_t{4};
    std::vector<std::string> expressions;
    for (int i = 3; i < argc; ++i) expressions.emplace_back(argv[i]);
    if (expressions.empty()) {
      expressions = {"document.title", "location.href", "document.readyState",
                     "document.documentElement.lang", "navigator.userAgent",
                     "document.querySelectorAll('a').length"};
    }
    suitecut::LaunchOptions options;
    options.executable = argv[1];
    options.max_pending = maximum;
    std::error_code identity_error;
#if defined(__linux__)
    const auto self = std::filesystem::path("/proc/self/exe");
#else
    const auto self = std::filesystem::absolute(argv[0], identity_error);
#endif
    if (!identity_error &&
        std::filesystem::equivalent(self, options.executable, identity_error))
      throw std::invalid_argument("Browser executable must not be this example program");
    auto browser = suitecut::Browser::launch(options);
    auto page = browser.main_page();
    read_window(browser.transport(), page.id(), expressions, maximum);
    browser.close();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
