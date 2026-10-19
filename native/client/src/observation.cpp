// SPDX-License-Identifier: MIT
#include "suitecut/observation.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace suitecut {
namespace {
constexpr std::size_t kMaxNodes = 4096;
constexpr std::size_t kMaxText = 1024 * 1024;
constexpr std::size_t kMaxScan = 65536;
const Json* field(const Json& node, std::string_view key) {
  if (!node.is_object()) return nullptr;
  const auto found = node.as_object().find(key);
  return found == node.as_object().end() ? nullptr : &found->second;
}
std::string_view text(const Json& node, std::string_view key) {
  const auto value = field(node, key);
  return value && value->is_string() ? value->as_string() : std::string_view{};
}
bool truth(const Json& node, std::string_view key) {
  const auto value = field(node, key);
  return value && value->is_bool() && value->as_bool();
}
bool password(const Json& node) {
  const auto states = field(node, "states");
  return text(node, "type") == "password" || text(node, "inputType") == "password" ||
         text(node, "role") == "password" || truth(node, "password") ||
         truth(node, "protected") || (states && truth(*states, "protected"));
}
bool interactive(const Json& node) {
  const auto role = text(node, "role");
  for (const auto candidate : {"button", "link", "textbox", "searchbox", "combobox", "listbox",
        "checkbox", "radio", "switch", "slider", "spinbutton", "tab", "menuitem",
        "menuitemcheckbox", "menuitemradio", "option", "treeitem", "scrollbar", "password"})
    if (role == candidate) return true;
  return truth(node, "focusable") || truth(node, "editable") || truth(node, "interactive") ||
         truth(node, "clickable") || text(node, "cursor") == "pointer" ||
         !text(node, "inputType").empty();
}
std::size_t utf8_prefix(std::string_view value, std::size_t bytes) {
  auto size = std::min(value.size(), bytes);
  if (size < value.size())
    while (size && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80) --size;
  return size;
}
struct Budget {
  std::size_t remaining;
  bool limited = false;
  void copy(Json& target, std::string_view key, std::string_view value, bool indivisible = false) {
    const auto length = indivisible && value.size() > remaining ? 0 : utf8_prefix(value, remaining);
    if (length < value.size()) limited = true;
    if (length || value.empty()) target[key] = std::string(value.substr(0, length));
    remaining -= length;
  }
};
using Context = std::vector<const Json*>;
Json make_node(const Json& source, const Json::array& path, const Context& context,
               bool hidden, bool descendant_hidden, Budget& budget) {
  Json result{{"path", path}};
  budget.copy(result, "role", source.is_string() ? "text" : text(source, "role"), true);
  if (!text(source, "ref").empty()) budget.copy(result, "ref", text(source, "ref"), true);
  for (const auto key : {"name", "text", "value", "valueText", "url", "placeholder", "type", "inputType", "cursor"}) {
    if ((hidden && (std::string_view(key) == "value" || std::string_view(key) == "valueText" ||
                    std::string_view(key) == "text")) || descendant_hidden) continue;
    const auto value = field(source, key);
    if (value && value->is_string()) budget.copy(result, key, value->as_string());
    else if (value && (value->is_bool() || value->is_number())) result[key] = *value;
  }
  if (source.is_string() && !hidden) budget.copy(result, "text", source.as_string());
  for (const auto key : {"checked", "disabled", "expanded", "active", "invalid", "level", "pressed",
                         "selected", "focused", "focusable", "editable", "interactive", "clickable", "required", "readonly", "readOnly",
                         "multiline", "valuemin", "valuemax", "valuenow"}) {
    if (hidden && std::string_view(key) == "valuenow") continue;
    auto value = field(source, key);
    if (!value) { const auto states = field(source, "states"); if (states) value = field(*states, key); }
    if (value && (value->is_bool() || value->is_number())) result[key] = *value;
    else if (value && value->is_string()) budget.copy(result, key, value->as_string());
  }
  if (hidden) result["protected"] = true;
  const auto box = field(source, "box");
  if (box && box->is_object()) {
    Json bounded_box(Json::object{});
    for (const auto key : {"x", "y", "width", "height"}) {
      const auto value = field(*box, key);
      if (value && value->is_number()) bounded_box[key] = *value;
    }
    if (!bounded_box.as_object().empty()) result["box"] = std::move(bounded_box);
  } else if (box && box->is_array() && box->as_array().size() == 4 &&
             std::all_of(box->as_array().begin(), box->as_array().end(), [](const Json& value) { return value.is_number(); })) {
    result["box"] = *box;
  }
  Json::array ancestors;
  const auto first = context.size() > 4 ? context.size() - 4 : 0;
  for (std::size_t index = first; index < context.size(); ++index) {
    Json ancestor(Json::object{});
    budget.copy(ancestor, "role", text(*context[index], "role"), true);
    budget.copy(ancestor, "name", text(*context[index], "name"));
    ancestors.push_back(std::move(ancestor));
  }
  if (!ancestors.empty()) result["context"] = std::move(ancestors);
  return result;
}
bool incomplete(const Json& observation) { return truth(observation, "truncated"); }
Json semantic(const Json& node) {
  Json result = node;
  for (const auto key : {"ref", "path", "box"}) result.as_object().erase(key);
  return result;
}
std::string identity(const Json& node) {
  auto label = text(node, "name");
  if (label.empty() && text(node, "role") == "text") label = text(node, "text");
  Json value{{"role", std::string(text(node, "role"))}, {"name", std::string(label)}};
  if (node.contains("context")) value["context"] = node.at("context");
  return value.dump();
}
std::string slot(const Json& node) {
  return Json{{"role", std::string(text(node, "role"))}, {"path", node.at("path")}}.dump();
}
const Json::array& checked_nodes(const Json& observation) {
  const auto nodes = field(observation, "nodes");
  if (!nodes || !nodes->is_array() || nodes->as_array().size() > kMaxNodes)
    throw std::invalid_argument("Expected bounded compact observation nodes");
  std::size_t bytes = 0;
  for (const auto& node : nodes->as_array()) {
    if (!node.is_object() || !node.contains("path") || !node.at("path").is_array() ||
        node.at("path").as_array().size() > 65)
      throw std::invalid_argument("Expected compact observation node with path");
    bytes += node.dump({4 * kMaxText, 16, 4096, kMaxText}).size();
    if (bytes > 4 * kMaxText) throw std::invalid_argument("Compact observation exceeds diff bounds");
  }
  return nodes->as_array();
}
} // namespace

Json compact_observation(const Json& forest, ObservationOptions options) {
  if (!forest.is_array()) throw std::invalid_argument("Observation must be an array forest");
  if (options.max_nodes > kMaxNodes || options.max_text_bytes > kMaxText || options.max_scan_nodes > kMaxScan)
    throw std::invalid_argument("Observation options exceed hard bounds");
  struct Frame {
    const Json::array* nodes;
    std::size_t index = 0;
    bool protected_parent = false;
    bool pushed_context = false;
  };
  std::vector<Frame> frames{{&forest.as_array()}};
  Context context;
  Json::array path, output;
  Budget budget{options.max_text_bytes};
  std::size_t scanned = 0;
  bool node_limit = false, scan_limit = false, depth_limit = false;
  while (!frames.empty()) {
    auto& frame = frames.back();
    if (frame.index == frame.nodes->size()) {
      const bool pop_context = frame.pushed_context;
      frames.pop_back();
      if (pop_context) context.pop_back();
      if (!path.empty()) path.pop_back();
      continue;
    }
    if (scanned >= options.max_scan_nodes) { scan_limit = true; break; }
    const auto index = frame.index++;
    const auto& node = frame.nodes->at(index);
    ++scanned;
    if (!node.is_object() && !node.is_string()) continue;
    const bool protected_parent = frame.protected_parent;
    const bool hidden = protected_parent || password(node);
    if (!options.interactive_only || interactive(node)) {
      if (output.size() >= options.max_nodes) { node_limit = true; break; }
      path.emplace_back(index);
      output.push_back(make_node(node, path, context, hidden, protected_parent, budget));
      path.pop_back();
    }
    const auto children = field(node, "children");
    if (!children || !children->is_array() || children->as_array().empty()) continue;
    if (frames.size() >= 65) { depth_limit = true; continue; }
    path.emplace_back(index);
    const bool push_context = !hidden && !text(node, "name").empty();
    if (push_context) context.push_back(&node);
    frames.push_back({&children->as_array(), 0, hidden, push_context});
  }
  return Json{{"nodes", std::move(output)}, {"truncated", node_limit || scan_limit || depth_limit || budget.limited},
              {"nodeLimit", node_limit}, {"textLimit", budget.limited}, {"scanLimit", scan_limit},
              {"depthLimit", depth_limit}, {"scannedNodes", scanned},
              {"textBytes", options.max_text_bytes - budget.remaining}};
}

Json observation_diff(const Json& previous, const Json& current, std::size_t max_changes) {
  if (max_changes > kMaxNodes) throw std::invalid_argument("Diff change limit exceeds hard bound");
  const auto& before = checked_nodes(previous);
  const auto& after = checked_nodes(current);
  using Groups = std::map<std::string, std::vector<std::size_t>>;
  Groups old_ids, new_ids, old_slots, new_slots;
  for (std::size_t i = 0; i < before.size(); ++i) { old_ids[identity(before[i])].push_back(i); old_slots[slot(before[i])].push_back(i); }
  for (std::size_t i = 0; i < after.size(); ++i) { new_ids[identity(after[i])].push_back(i); new_slots[slot(after[i])].push_back(i); }
  std::vector<bool> old_used(before.size()), new_used(after.size());
  Json::array added, removed, changed;
  std::size_t changes = 0;
  bool limited = false, ambiguous = false;
  auto append = [&](Json::array& destination, Json node) {
    if (changes >= max_changes) { limited = true; return; }
    ++changes;
    destination.push_back(std::move(node));
  };
  auto pair = [&](std::size_t old_index, std::size_t new_index) {
    old_used[old_index] = true; new_used[new_index] = true;
    if (semantic(before[old_index]) != semantic(after[new_index]))
      append(changed, Json{{"before", before[old_index]}, {"after", after[new_index]}});
  };
  for (const auto& [key, indices] : old_ids) {
    const auto found = new_ids.find(key);
    if (indices.size() > 1 || (found != new_ids.end() && found->second.size() > 1)) ambiguous = true;
    if (indices.size() == 1 && found != new_ids.end() && found->second.size() == 1)
      pair(indices.front(), found->second.front());
  }
  for (const auto& [key, indices] : old_slots) {
    const auto found = new_slots.find(key);
    if (indices.size() != 1 || found == new_slots.end() || found->second.size() != 1) continue;
    const auto old_index = indices.front(), new_index = found->second.front();
    if (!old_used[old_index] && !new_used[new_index] && old_ids.at(identity(before[old_index])).size() == 1 &&
        new_ids.at(identity(after[new_index])).size() == 1) pair(old_index, new_index);
  }
  const bool complete = !incomplete(previous) && !incomplete(current);
  for (std::size_t i = 0; i < after.size(); ++i) if (!new_used[i]) append(added, after[i]);
  if (complete)
    for (std::size_t i = 0; i < before.size(); ++i) if (!old_used[i]) append(removed, before[i]);
  for (const auto& [key, indices] : new_ids) { (void)key; if (indices.size() > 1) ambiguous = true; }
  return Json{{"added", std::move(added)}, {"removed", std::move(removed)}, {"changed", std::move(changed)},
              {"ambiguous", ambiguous}, {"complete", complete && !limited}, {"truncated", limited},
              {"removalsSuppressed", !complete}};
}
} // namespace suitecut
