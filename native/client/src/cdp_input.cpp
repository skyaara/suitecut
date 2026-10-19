// SPDX-License-Identifier: MIT
#include "cdp_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace suitecut::detail {
namespace {
using namespace std::chrono_literals;
void check_time(Deadline deadline) {
  cdp_check_active(deadline);
}
int integer(const Json& input, const char* name, int fallback, int low, int high) {
  if (!input.contains(name)) return fallback;
  const auto& value = input.at(name);
  if (!value.is_integer() || value.as_int() < low || value.as_int() > high)
    throw std::invalid_argument(std::string("Invalid input ") + name);
  return static_cast<int>(value.as_int());
}
double coordinate(const Json& input, const char* name, double fallback, bool required = false) {
  if (!input.contains(name)) {
    if (required) throw std::invalid_argument(std::string("Missing input ") + name);
    return fallback;
  }
  const double value = input.at(name).as_double();
  if (!std::isfinite(value) || std::abs(value) > 1000000)
    throw std::invalid_argument(std::string("Invalid input ") + name);
  return value;
}
void budget(Deadline deadline, int delay, std::size_t count = 1) {
  check_time(deadline);
  const auto remaining = std::chrono::duration<long double, std::milli>(
      deadline - std::chrono::steady_clock::now()).count();
  if (static_cast<long double>(delay) * count >= remaining)
    throw std::runtime_error("Input delay exceeds command deadline");
}
void pause(int delay, Deadline deadline) {
  budget(deadline, delay);
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
  while (std::chrono::steady_clock::now() < end) {
    cdp_check_active(deadline);
    std::this_thread::sleep_until(std::min(end, std::chrono::steady_clock::now() + 10ms));
    cdp_check_active(deadline);
  }
  check_time(deadline);
}
// Decode complete scalars, never a UTF-8 byte or half of a surrogate pair.
std::vector<std::string> scalars(const std::string& text) {
  std::vector<std::string> result;
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    const std::size_t length = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
        first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!length || length > text.size() - i) throw std::invalid_argument("Input text is not valid UTF-8");
    std::uint32_t codepoint = first & (length == 1 ? 0x7f : length == 2 ? 0x1f : length == 3 ? 0x0f : 7);
    for (std::size_t j = 1; j < length; ++j) {
      const auto next = static_cast<unsigned char>(text[i + j]);
      if ((next & 0xc0) != 0x80) throw std::invalid_argument("Input text is not valid UTF-8");
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
        (length == 4 && codepoint < 0x10000) || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff))
      throw std::invalid_argument("Input text is not valid UTF-8");
    result.push_back(text.substr(i, length));
    i += length;
  }
  return result;
}
struct Key {
  std::string key, code, text, unmodified;
  int vk = 0, bit = 0, location = 0;
};
std::string normalize(std::string key) {
  if (key == "ControlOrMeta") {
#if defined(__APPLE__)
    return "Meta";
#else
    return "Control";
#endif
  }
  if (key == "Ctrl") return "Control";
  if (key == "Cmd" || key == "Command") return "Meta";
  if (key == "Esc") return "Escape";
  return key;
}
Key key_for(std::string name, int modifiers) {
  name = normalize(std::move(name));
  if (name == "Alt" || name == "AltLeft") return {"Alt", "AltLeft", "", "", 18, 1, 1};
  if (name == "Control" || name == "ControlLeft") return {"Control", "ControlLeft", "", "", 17, 2, 1};
  if (name == "Meta" || name == "MetaLeft") return {"Meta", "MetaLeft", "", "", 91, 4, 1};
  if (name == "Shift" || name == "ShiftLeft") return {"Shift", "ShiftLeft", "", "", 16, 8, 1};
  if (name == "AltRight") return {"Alt", name, "", "", 18, 1, 2};
  if (name == "ControlRight") return {"Control", name, "", "", 17, 2, 2};
  if (name == "MetaRight") return {"Meta", name, "", "", 92, 4, 2};
  if (name == "ShiftRight") return {"Shift", name, "", "", 16, 8, 2};
  struct Special { const char* name; const char* code; int vk; const char* text; };
  static const Special special[] = {
      {"Enter", "Enter", 13, "\r"}, {"Tab", "Tab", 9, ""},
      {"Backspace", "Backspace", 8, ""}, {"Delete", "Delete", 46, ""},
      {"Escape", "Escape", 27, ""}, {"ArrowLeft", "ArrowLeft", 37, ""},
      {"ArrowUp", "ArrowUp", 38, ""}, {"ArrowRight", "ArrowRight", 39, ""},
      {"ArrowDown", "ArrowDown", 40, ""}, {"Home", "Home", 36, ""},
      {"End", "End", 35, ""}, {"PageUp", "PageUp", 33, ""},
      {"PageDown", "PageDown", 34, ""}, {"Insert", "Insert", 45, ""},
      {"CapsLock", "CapsLock", 20, ""}, {"NumLock", "NumLock", 144, ""},
      {"ScrollLock", "ScrollLock", 145, ""}, {"Pause", "Pause", 19, ""},
      {"PrintScreen", "PrintScreen", 44, ""}};
  if (name == "Return" || name == "\n" || name == "\r") name = "Enter";
  if (name == "\t") name = "Tab";
  for (const auto& value : special)
    if (name == value.name) return {value.name, value.code, value.text, value.text, value.vk};
  if (name.size() >= 2 && name[0] == 'F') {
    for (int number = 1; number <= 24; ++number)
      if (name == "F" + std::to_string(number)) return {name, name, "", "", 111 + number};
  }
  if (name == "Space" || name == "Spacebar") name = " ";
  if (scalars(name).size() != 1) throw std::invalid_argument("Unsupported keyboard key: " + name);
  Key value{name, "", name, name};
  if (name.size() != 1) return value;
  const unsigned char character = static_cast<unsigned char>(name[0]);
  if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) {
    const char upper = character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character;
    value.code = std::string("Key") + upper;
    value.vk = upper;
    if (modifiers & 8) value.key = value.text = std::string(1, upper);
  } else if (character >= '0' && character <= '9') {
    value.code = "Digit" + name;
    value.vk = character;
    if (modifiers & 8) value.key = value.text = std::string(1, ")!@#$%^&*("[character - '0']);
  } else {
    struct Punctuation { char plain, shifted; const char* code; int vk; };
    static const Punctuation punctuation[] = {
        {' ', ' ', "Space", 32}, {';', ':', "Semicolon", 186}, {'=', '+', "Equal", 187},
        {',', '<', "Comma", 188}, {'-', '_', "Minus", 189}, {'.', '>', "Period", 190},
        {'/', '?', "Slash", 191}, {'`', '~', "Backquote", 192}, {'[', '{', "BracketLeft", 219},
        {'\\', '|', "Backslash", 220}, {']', '}', "BracketRight", 221}, {'\'', '"', "Quote", 222}};
    for (const auto& entry : punctuation) {
      if (character != entry.plain && character != entry.shifted) continue;
      value.code = entry.code;
      value.vk = entry.vk;
      if (modifiers & 8) value.key = value.text = std::string(1, entry.shifted);
      return value;
    }
    const std::string shifted = ")!@#$%^&*(";
    const auto digit = shifted.find(character);
    if (digit != std::string::npos) { value.code = "Digit" + std::to_string(digit); value.vk = '0' + digit; }
  }
  return value;
}
std::vector<std::string> combination(const std::string& key) {
  if (key.empty() || key.size() > 128) throw std::invalid_argument("Keyboard key must contain 1 to 128 bytes");
  if (key == "+") return {key};
  std::vector<std::string> result;
  std::size_t offset = 0;
  while (true) {
    if (key.substr(offset) == "+") { result.push_back("+"); break; }
    const auto end = key.find('+', offset);
    result.push_back(key.substr(offset, end == std::string::npos ? end : end - offset));
    if (result.back().empty()) throw std::invalid_argument("Invalid keyboard combination");
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  return result;
}
void dispatch_key(CdpRuntime& runtime, CdpPage& page, const Key& key, bool down, Deadline deadline) {
  check_time(deadline);
  const bool text = down && !key.text.empty() && !(page.modifiers & (1 | 2 | 4));
  Json params{{"type", down ? (text ? "keyDown" : "rawKeyDown") : "keyUp"},
      {"key", key.key}, {"code", key.code}, {"windowsVirtualKeyCode", key.vk},
      {"nativeVirtualKeyCode", key.vk}, {"modifiers", page.modifiers}, {"location", key.location}};
  if (text) { params["text"] = key.text; params["unmodifiedText"] = key.unmodified; }
  runtime.call(page, "Input.dispatchKeyEvent", std::move(params), deadline);
}
void keyboard_key(CdpRuntime& runtime, CdpPage& page, const Json& input, Deadline deadline) {
  const auto& action = input.at("action").as_string();
  const auto parts = combination(input.at("key").as_string());
  std::vector<Key> prefixes;
  int mask = page.modifiers;
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    Key key = key_for(parts[i], mask);
    if (!key.bit) throw std::invalid_argument("Unsupported keyboard modifier: " + parts[i]);
    prefixes.push_back(key);
    mask |= key.bit;
  }
  Key key = key_for(parts.back(), mask);
  const int delay = integer(input, "delay", 0, 0, 120000);
  budget(deadline, action == "press" ? delay : 0);
  if (action == "up") {
    try {
      page.modifiers &= ~key.bit;
      dispatch_key(runtime, page, key, false, deadline);
      for (auto it = prefixes.rbegin(); it != prefixes.rend(); ++it) {
        page.modifiers &= ~it->bit;
        dispatch_key(runtime, page, *it, false, deadline);
      }
    } catch (...) {
      CdpCleanupScope cleanup_scope;
      const auto cleanup = std::chrono::steady_clock::now() + 150ms;
      page.modifiers &= ~key.bit;
      try { dispatch_key(runtime, page, key, false, cleanup); } catch (...) {}
      for (auto it = prefixes.rbegin(); it != prefixes.rend(); ++it) {
        page.modifiers &= ~it->bit;
        try { dispatch_key(runtime, page, *it, false, cleanup); } catch (...) {}
      }
      throw;
    }
    return;
  }
  std::vector<Key> held;
  const int original = page.modifiers;
  bool key_held = false;
  auto release = [&](Deadline end, bool best_effort) {
    auto release_one = [&](const Key& entry) {
      page.modifiers &= ~entry.bit;
      if (best_effort) { try { dispatch_key(runtime, page, entry, false, end); } catch (...) {} }
      else dispatch_key(runtime, page, entry, false, end);
    };
    if (key_held) { release_one(key); key_held = false; }
    for (auto it = held.rbegin(); it != held.rend(); ++it) release_one(*it);
    held.clear();
    page.modifiers = original;
  };
  try {
    for (const auto& entry : prefixes) {
      if (page.modifiers & entry.bit) continue;
      page.modifiers |= entry.bit;
      held.push_back(entry);
      dispatch_key(runtime, page, entry, true, deadline);
    }
    // Pressing an already held modifier must preserve the held state.
    if (!key.bit || !(page.modifiers & key.bit)) {
      page.modifiers |= key.bit;
      key_held = true;
      dispatch_key(runtime, page, key, true, deadline);
    }
    if (action == "down") return;
    pause(delay, deadline);
    release(deadline, false);
  } catch (...) {
    CdpCleanupScope cleanup_scope;
    release(std::chrono::steady_clock::now() + 150ms, true);
    throw;
  }
}
int button_mask(const std::string& button) {
  if (button == "left") return 1;
  if (button == "right") return 2;
  if (button == "middle") return 4;
  throw std::invalid_argument("Unsupported mouse button: " + button);
}
void dispatch_mouse(CdpRuntime& runtime, CdpPage& page, const char* type,
                    const std::string& button, int count, Deadline deadline,
                    double dx = 0, double dy = 0) {
  check_time(deadline);
  Json params{{"type", type}, {"x", page.mouse_x}, {"y", page.mouse_y},
      {"button", button}, {"buttons", page.mouse_buttons}, {"modifiers", page.modifiers}};
  if (count) params["clickCount"] = count;
  if (std::string(type) == "mouseWheel") { params["deltaX"] = dx; params["deltaY"] = dy; }
  runtime.call(page, "Input.dispatchMouseEvent", std::move(params), deadline);
}
void mouse(CdpRuntime& runtime, CdpPage& page, const Json& input, Deadline deadline) {
  const auto& action = input.at("action").as_string();
  if (action != "move" && action != "down" && action != "up" && action != "click" &&
      action != "dblclick" && action != "wheel") throw std::invalid_argument("Unsupported mouse action: " + action);
  if (input.contains("backend") && input.at("backend").as_string() != "devtools")
    throw std::invalid_argument("External Chrome input requires the devtools backend");
  const auto button = input.contains("button") ? input.at("button").as_string() : "left";
  const int bit = button_mask(button);
  const int count = integer(input, "clickCount", action == "dblclick" ? 2 : 1, 1, 1000);
  const int delay = integer(input, "delay", 0, 0, 120000);
  const int steps = integer(input, "steps", 1, 1, 10000);
  const double x = coordinate(input, "x", page.mouse_x, action == "move" || action == "click" || action == "dblclick");
  const double y = coordinate(input, "y", page.mouse_y, action == "move" || action == "click" || action == "dblclick");
  const double dx = coordinate(input, "deltaX", 0, action == "wheel");
  const double dy = coordinate(input, "deltaY", 0, action == "wheel");
  budget(deadline, action == "click" || action == "dblclick" ? delay : 0, count);
  if (action == "move") {
    const double start_x = page.mouse_x, start_y = page.mouse_y;
    for (int step = 1; step <= steps; ++step) {
      page.mouse_x = start_x + (x - start_x) * step / steps;
      page.mouse_y = start_y + (y - start_y) * step / steps;
      // CDP needs the held button identity as well as the buttons bitmask
      // during a move, so Chromium can admit its native drag gesture.
      const auto held_button = page.mouse_buttons & 1 ? "left" : page.mouse_buttons & 2 ? "right" : page.mouse_buttons & 4 ? "middle" : "none";
      dispatch_mouse(runtime, page, "mouseMoved", held_button, 0, deadline);
    }
    return;
  }
  page.mouse_x = x; page.mouse_y = y;
  if (action == "wheel") { dispatch_mouse(runtime, page, "mouseWheel", "none", 0, deadline, dx, dy); return; }
  if (action == "down" || action == "up") {
    const int previous_buttons = page.mouse_buttons;
    if (action == "down") page.mouse_buttons |= bit;
    else page.mouse_buttons &= ~bit;
    try {
      dispatch_mouse(runtime, page, action == "down" ? "mousePressed" : "mouseReleased", button, count, deadline);
    } catch (...) {
      CdpCleanupScope cleanup_scope;
      if (action == "up" || !(previous_buttons & bit)) {
        page.mouse_buttons &= ~bit;
        try { dispatch_mouse(runtime, page, "mouseReleased", button, count,
                             std::chrono::steady_clock::now() + 150ms); } catch (...) {}
      }
      throw;
    }
    return;
  }
  if (page.mouse_buttons & bit) throw std::invalid_argument("Cannot click an already held mouse button");
  dispatch_mouse(runtime, page, "mouseMoved", "none", 0, deadline);
  bool held = false;
  int current_click = 1;
  try {
    for (int click = 1; click <= count; ++click) {
      current_click = click;
      held = true;
      page.mouse_buttons |= bit;
      dispatch_mouse(runtime, page, "mousePressed", button, click, deadline);
      pause(delay, deadline);
      page.mouse_buttons &= ~bit;
      dispatch_mouse(runtime, page, "mouseReleased", button, click, deadline);
      held = false;
    }
  } catch (...) {
    CdpCleanupScope cleanup_scope;
    page.mouse_buttons &= ~bit;
    if (held) {
      try { dispatch_mouse(runtime, page, "mouseReleased", button, current_click,
                           std::chrono::steady_clock::now() + 150ms); } catch (...) {}
    }
    throw;
  }
}
} // namespace

Json cdp_input_command(CdpRuntime& runtime, CdpPage& page, const Json& input, Deadline deadline) {
  check_time(deadline);
  const auto& kind = input.at("kind").as_string();
  const auto& action = input.at("action").as_string();
  if (kind == "mouse") mouse(runtime, page, input, deadline);
  else if (kind == "keyboard") {
    if (action == "press" || action == "down" || action == "up") keyboard_key(runtime, page, input, deadline);
    else if (action == "insertText" || action == "type") {
      const auto& text = input.at("text").as_string();
      const auto characters = scalars(text);
      const int delay = integer(input, "delay", 0, 0, 120000);
      budget(deadline, action == "type" ? delay : 0, characters.size());
      if (action == "insertText") runtime.call(page, "Input.insertText", Json{{"text", text}}, deadline);
      else {
        for (const auto& character : characters) {
          keyboard_key(runtime, page, Json{{"action", "press"}, {"key", character}, {"delay", delay}}, deadline);
        }
      }
    } else throw std::invalid_argument("Unsupported keyboard action: " + action);
  } else throw std::invalid_argument("Unsupported input kind: " + kind);
  return Json::object{};
}
} // namespace suitecut::detail
