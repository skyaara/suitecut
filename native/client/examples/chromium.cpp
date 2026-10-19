// SPDX-License-Identifier: MIT
#include "suitecut/browser.hpp"
#include <filesystem>
#include <iostream>
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: suitecut-native-chromium /absolute/path/to/chrome [url]\n";
    return 2;
  }
  try {
    suitecut::LaunchOptions options;
    options.executable = std::filesystem::canonical(argv[1]).string();
    auto browser = suitecut::Browser::launch_chromium(options);
    auto page = browser.main_page();
    page.navigate(argc == 3 ? argv[2] : "about:blank");
    std::cout << suitecut::Json{{"version", browser.version()},
        {"title", page.evaluate("document.title")}, {"observation", page.observe()}}.dump() << '\n';
    browser.close();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
