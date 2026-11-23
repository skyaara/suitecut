// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

struct SuiteCutKey {
  std::string key, code, text, unmodified_text;
  int virtual_key = 0, modifier = 0, location = 0;
};

// Authored US keyboard descriptions. Public key names are input data; this
// table supplies native protocol fields rather than dispatching DOM events.
inline std::optional<SuiteCutKey> SuiteCutDescribeKey(std::string name, int modifiers) {
  if (name == "ControlOrMeta") {
#if defined(__APPLE__)
    name = "Meta";
#else
    name = "Control";
#endif
  }
  if (name == "\n" || name == "\r") name = "Enter";
  struct Named { const char* name; const char* code; int key, modifier, location; const char* text; };
  static constexpr Named named[] = {
    {"Backspace","Backspace",8,0,0,""}, {"Tab","Tab",9,0,0,""}, {"Enter","Enter",13,0,0,"\r"},
    {"Escape","Escape",27,0,0,""}, {"Esc","Escape",27,0,0,""},
    {"Delete","Delete",46,0,0,""}, {"Insert","Insert",45,0,0,""},
    {"Home","Home",36,0,0,""}, {"End","End",35,0,0,""},
    {"PageUp","PageUp",33,0,0,""}, {"PageDown","PageDown",34,0,0,""},
    {"ArrowLeft","ArrowLeft",37,0,0,""}, {"ArrowUp","ArrowUp",38,0,0,""},
    {"ArrowRight","ArrowRight",39,0,0,""}, {"ArrowDown","ArrowDown",40,0,0,""},
    {"Shift","ShiftLeft",16,8,1,""}, {"ShiftLeft","ShiftLeft",16,8,1,""}, {"ShiftRight","ShiftRight",16,8,2,""},
    {"Control","ControlLeft",17,2,1,""}, {"ControlLeft","ControlLeft",17,2,1,""}, {"ControlRight","ControlRight",17,2,2,""},
    {"Alt","AltLeft",18,1,1,""}, {"AltLeft","AltLeft",18,1,1,""}, {"AltRight","AltRight",18,1,2,""},
    {"Meta","MetaLeft",91,4,1,""}, {"MetaLeft","MetaLeft",91,4,1,""}, {"MetaRight","MetaRight",92,4,2,""},
    {"CapsLock","CapsLock",20,0,0,""}, {"NumLock","NumLock",144,0,3,""}, {"ScrollLock","ScrollLock",145,0,0,""},
    {"Pause","Pause",19,0,0,""}, {"PrintScreen","PrintScreen",44,0,0,""}, {"ContextMenu","ContextMenu",93,0,0,""},
    {"AudioVolumeMute","AudioVolumeMute",173,0,0,""}, {"AudioVolumeDown","AudioVolumeDown",174,0,0,""},
    {"AudioVolumeUp","AudioVolumeUp",175,0,0,""}, {"MediaTrackNext","MediaTrackNext",176,0,0,""},
    {"MediaTrackPrevious","MediaTrackPrevious",177,0,0,""}, {"MediaStop","MediaStop",178,0,0,""},
    {"MediaPlayPause","MediaPlayPause",179,0,0,""},
    {"NumpadEnter","NumpadEnter",13,0,3,"\r"}, {"NumpadAdd","NumpadAdd",107,0,3,"+"},
    {"NumpadSubtract","NumpadSubtract",109,0,3,"-"}, {"NumpadMultiply","NumpadMultiply",106,0,3,"*"},
    {"NumpadDivide","NumpadDivide",111,0,3,"/"}, {"NumpadDecimal","NumpadDecimal",110,0,3,"."}
  };
  if (name == "NumpadDecimal") {
    const bool numeric = modifiers & 8;
    return SuiteCutKey{numeric ? "." : "Delete", name, numeric && !(modifiers & 7) ? "." : "", numeric ? "." : "", numeric ? 110 : 46, 0, 3};
  }
  for (const auto& value : named) if (name == value.name) {
    SuiteCutKey key{name, value.code, value.text, value.text, value.key, value.modifier, value.location};
    if (key.modifier == 8) key.key = "Shift";
    if (key.modifier == 2) key.key = "Control";
    if (key.modifier == 1) key.key = "Alt";
    if (key.modifier == 4) key.key = "Meta";
    if (name == "Esc") key.key = "Escape";
    if (name == "NumpadEnter") key.key = "Enter";
    else if (key.location == 3 && !key.text.empty()) key.key = key.text;
    if (modifiers & 7) key.text.clear();
    return key;
  }
  if (name.size() > 1 && name[0] == 'F') {
    int number = 0; const auto result = std::from_chars(name.data() + 1, name.data() + name.size(), number);
    if (result.ec == std::errc{} && result.ptr == name.data() + name.size() && number >= 1 && number <= 24)
      return SuiteCutKey{name, name, "", "", 111 + number, 0, 0};
  }
  if (name.size() == 7 && name.starts_with("Numpad") && name[6] >= '0' && name[6] <= '9') {
    static constexpr const char* navigation[] = {"Insert", "End", "ArrowDown", "PageDown", "ArrowLeft", "Clear", "ArrowRight", "Home", "ArrowUp", "PageUp"};
    static constexpr int codes[] = {45, 35, 40, 34, 37, 12, 39, 36, 38, 33};
    const int index = name[6] - '0'; const bool numeric = modifiers & 8;
    return SuiteCutKey{numeric ? name.substr(6) : navigation[index], name, numeric && !(modifiers & 7) ? name.substr(6) : "", numeric ? name.substr(6) : "", numeric ? 96 + index : codes[index], 0, 3};
  }
  if (name == "Space" || name == "Spacebar") name = " ";
  if (name.size() == 4 && name.starts_with("Key") && name[3] >= 'A' && name[3] <= 'Z') name = std::string(1, name[3] + 32);
  if (name.size() == 6 && name.starts_with("Digit") && name[5] >= '0' && name[5] <= '9') name = name.substr(5);
  struct Punctuation { const char* code; char plain, shifted; int key; };
  static constexpr Punctuation punctuation[] = {
    {"Backquote",'`','~',192},{"Minus",'-','_',189},{"Equal",'=','+',187},
    {"BracketLeft",'[','{',219},{"BracketRight",']','}',221},{"Backslash",'\\','|',220},
    {"Semicolon",';',':',186},{"Quote",'\'','"',222},{"Comma",',','<',188},{"Period",'.','>',190},{"Slash",'/','?',191}
  };
  for (const auto& value : punctuation) if (name == value.code) name = std::string(1, value.plain);
  if (name.size() != 1 || static_cast<unsigned char>(name[0]) < 32 || static_cast<unsigned char>(name[0]) > 126) return std::nullopt;
  const char character = name[0]; char output = character; SuiteCutKey key;
  if (std::isalpha(static_cast<unsigned char>(character))) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    key.code = "Key" + std::string(1, upper); key.virtual_key = upper;
    if (modifiers & 8) output = upper;
    key.unmodified_text = std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  } else if (character == ' ') { key.code = "Space"; key.virtual_key = 32; key.unmodified_text = " "; }
  else {
    static constexpr std::string_view plain = "0123456789", shifted = ")!@#$%^&*(";
    const auto digit = plain.find(character); const auto symbol = shifted.find(character);
    if (digit != std::string_view::npos || symbol != std::string_view::npos) {
      const auto index = digit != std::string_view::npos ? digit : symbol;
      key.code = "Digit" + std::string(1, plain[index]); key.virtual_key = plain[index];
      key.unmodified_text = std::string(1, plain[index]);
      if (modifiers & 8) output = shifted[index];
    } else for (const auto& value : punctuation) if (character == value.plain || character == value.shifted) {
      key.code = value.code; key.virtual_key = value.key; key.unmodified_text = std::string(1, value.plain);
      if (modifiers & 8) output = value.shifted;
      break;
    }
  }
  if (key.code.empty()) return std::nullopt;
  key.key = std::string(1, output); key.text = modifiers & 7 ? "" : key.key;
  return key;
}
