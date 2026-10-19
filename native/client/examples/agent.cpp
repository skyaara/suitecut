// SPDX-License-Identifier: MIT
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "suitecut/browser.hpp"

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: suitecut-native-agent /absolute/suitecut-browser [screenshot.png]\n";
    return 2;
  }
  try {
    suitecut::LaunchOptions options;
    options.executable = argv[1];
    auto browser = suitecut::Browser::launch(options);
    auto context = browser.new_context();
    auto page = context.new_page();
    page.set_content(
        "<!doctype html><meta charset=utf-8><label>Name<input id=name></label><button "
        "id=save>Save</button><output "
        "id=status>Waiting</"
        "output><script>document.querySelector('#save').onclick=event=>{document.querySelector('#"
        "status').textContent=document.querySelector('#name').value;window.trusted=event.isTrusted}"
        "</script>");
    const std::string text = "Native C++ agent — café 東京 🚀";
    page.get_by_label("Name").fill(text);
    page.get_by_role("button", "Save").click();
    if (page.locator("#status").text_content().as_string() != text ||
        !page.evaluate("window.trusted").as_bool())
      throw std::runtime_error("Actual native form action did not complete");
    const auto observation = page.observe();
    if (argc == 3) {
      const auto bytes = page.screenshot();
      std::ofstream file(argv[2], std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
      if (!file) throw std::runtime_error("Cannot write screenshot");
    }
    std::cout << suitecut::Json{{"verified", true},
                                {"runtime", browser.version()},
                                {"client", "native C++20; no Node subprocess"},
                                {"observation", observation}}
                     .dump()
              << '\n';
    page.close();
    context.close();
    browser.close();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
