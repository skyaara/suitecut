// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "include/cef_parser.h"

namespace suitecut {
// The people who use one SuiteCut app, as in Chrome: each profile is its own
// Chromium profile directory under the shared root ("Default", "Profile 2",
// ...) with separate cookies, history, bookmarks and settings. This registry
// only names them; Chromium owns everything inside each directory.
class BrowserProfiles {
 public:
  struct Profile {
    std::string dir, name;
  };
  static constexpr size_t kProfiles = 64, kName = 64;
  static constexpr const char* kFile = "SuiteCut Profiles.json";

  explicit BrowserProfiles(std::filesystem::path root) : root_(std::move(root)) {}

  // A missing or unreadable registry starts over with the default profile;
  // existing profile directories are never removed or renamed.
  void Load() {
    profiles_.clear();
    last_.clear();
    std::ifstream input(root_ / kFile);
    std::stringstream text;
    text << input.rdbuf();
    const auto parsed = input ? CefParseJSON(text.str(), JSON_PARSER_RFC) : nullptr;
    const auto data = parsed ? parsed->GetDictionary() : nullptr;
    const auto list = data ? data->GetList("profiles") : nullptr;
    for (size_t i = 0; list && i < list->GetSize() && profiles_.size() < kProfiles; ++i) {
      const auto item = list->GetDictionary(i);
      if (!item) continue;
      const auto dir = item->GetString("dir").ToString();
      auto name = Clean(item->GetString("name").ToString());
      if (!ValidDir(dir) || Find(dir)) continue;
      profiles_.push_back({dir, name.empty() ? dir : name});
    }
    if (!Find("Default")) profiles_.insert(profiles_.begin(), {"Default", "Person 1"});
    const auto last = data ? data->GetString("last").ToString() : "";
    last_ = Find(last) ? last : "Default";
  }

  bool Save() const {
    auto list = CefListValue::Create();
    for (size_t i = 0; i < profiles_.size(); ++i) {
      auto item = CefDictionaryValue::Create();
      item->SetString("dir", profiles_[i].dir);
      item->SetString("name", profiles_[i].name);
      list->SetDictionary(i, item);
    }
    auto data = CefDictionaryValue::Create();
    data->SetList("profiles", list);
    data->SetString("last", last_);
    auto value = CefValue::Create();
    value->SetDictionary(data);
    const auto text = CefWriteJSON(value, JSON_WRITER_PRETTY_PRINT).ToString();
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    const auto temporary = root_ / (std::string(kFile) + ".tmp");
    {
      std::ofstream output(temporary, std::ios::trunc);
      output << text;
      if (!output.flush()) return false;
    }
    std::filesystem::rename(temporary, root_ / kFile, error);
    return !error;
  }

  const std::vector<Profile>& profiles() const { return profiles_; }
  const std::string& last() const { return last_; }
  void SetLast(const std::string& dir) {
    if (Find(dir)) last_ = dir;
  }
  const Profile* Find(const std::string& dir) const {
    for (const auto& profile : profiles_)
      if (profile.dir == dir) return &profile;
    return nullptr;
  }
  std::filesystem::path PathFor(const std::string& dir) const { return root_ / dir; }

  // Chromium's own numbering: the next unused "Profile N" directory, N >= 2,
  // skipping any directory already on disk so an old profile is never reused.
  std::optional<Profile> Add(const std::string& requested) {
    if (profiles_.size() >= kProfiles) return std::nullopt;
    for (int n = 2; n < 10000; ++n) {
      const auto dir = "Profile " + std::to_string(n);
      std::error_code error;
      if (Find(dir) || std::filesystem::exists(root_ / dir, error)) continue;
      auto name = Clean(requested);
      if (name.empty()) name = "Person " + std::to_string(profiles_.size() + 1);
      profiles_.push_back({dir, name});
      return profiles_.back();
    }
    return std::nullopt;
  }

  bool Rename(const std::string& dir, const std::string& requested) {
    const auto name = Clean(requested);
    for (auto& profile : profiles_)
      if (profile.dir == dir && !name.empty()) {
        profile.name = name;
        return true;
      }
    return false;
  }

 private:
  static bool ValidDir(const std::string& dir) {
    if (dir == "Default") return true;
    if (!dir.starts_with("Profile ") || dir.size() > 13) return false;
    const auto number = dir.substr(8);
    return !number.empty() && number.front() != '0' &&
           number.find_first_not_of("0123456789") == std::string::npos;
  }
  // Names are labels in menus: one line, trimmed, bounded, never a path.
  static std::string Clean(const std::string& value) {
    std::string out;
    for (const char c : value) out += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    out = out.substr(first, out.find_last_not_of(' ') - first + 1);
    if (out.size() > kName) {
      size_t cut = kName;
      while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
      out.resize(cut);
    }
    return out;
  }

  std::filesystem::path root_;
  std::vector<Profile> profiles_;
  std::string last_;
};
}  // namespace suitecut
