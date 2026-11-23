// SPDX-License-Identifier: MIT
#include "suitecut/observation.hpp"
#include <algorithm>
#include <map>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace suitecut {
namespace {
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
std::size_t utf8_prefix(std::string_view value, std::size_t bytes) {
  auto size = std::min(value.size(), bytes);
  if (size < value.size())
    while (size && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80) --size;
  return size;
}
bool pointer(const Json& node) { return text(node, "cursor") == "pointer"; }
// A control by its role or state, leaving out the pointer cursor.
bool semantic_control(const Json& node) {
  const auto role = text(node, "role");
  for (const auto candidate : {"button", "link", "textbox", "searchbox", "combobox", "listbox",
        "checkbox", "radio", "switch", "slider", "spinbutton", "tab", "menuitem",
        "menuitemcheckbox", "menuitemradio", "option", "treeitem", "scrollbar", "password"})
    if (role == candidate) return true;
  return truth(node, "focusable") || truth(node, "editable") || truth(node, "interactive") ||
         truth(node, "clickable") || !text(node, "inputType").empty();
}
// Whether a node belongs in the controls-only view. The cursor is inherited, so an image or wrapper inside a clickable
// control shows a pointer too; clicking it clicks the control, which is listed already. The snapshot emits a pointer
// generic only where the pointer starts, so a generic inside one is there for a reason of its own (a handler, a focus
// stop, an editor) and stays.
bool listed(const Json& node, bool inside_pointer) {
  return semantic_control(node) || (pointer(node) && (!inside_pointer || text(node, "role") == "generic"));
}
// A node whose text is what a person typed or picked, not a caption: fields, pickers, and a generic emitted without a
// pointer, which the snapshot keeps only for an input, a focus stop or an editor.
bool holds_value(const Json& node) {
  if (password(node) || truth(node, "editable") || !text(node, "inputType").empty()) return true;
  const auto role = text(node, "role");
  for (const auto candidate : {"textbox", "searchbox", "combobox", "listbox", "spinbutton", "slider"})
    if (role == candidate) return true;
  return role == "generic" && !pointer(node);
}
// A nameless control's own visible text, so it can be told apart from its neighbours: a few words, read from a
// bounded walk of its subtree that never enters a field's contents.
std::string visible_text(const Json& node) {
  constexpr std::size_t kBytes = 80, kVisits = 64;
  std::string result;
  std::vector<const Json*> pending{&node};
  for (std::size_t visits = 0; !pending.empty() && visits < kVisits && result.size() < kBytes; ++visits) {
    const auto current = pending.back();
    pending.pop_back();
    if (current->is_object() && holds_value(*current)) continue;
    auto value = current->is_string() ? std::string_view(current->as_string())
                 : text(*current, "role") == "text" ? text(*current, "text") : std::string_view{};
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
    if (!value.empty()) {
      if (!result.empty()) result += ' ';
      // One byte past the cap shows whether a cut is needed; the rest of a long text is never copied.
      result += value.substr(0, kBytes + 1 - std::min(result.size(), kBytes + 1));
    }
    const auto children = field(*current, "children");
    if (children && children->is_array())
      for (auto at = children->as_array().rbegin(); at != children->as_array().rend(); ++at) pending.push_back(&*at);
  }
  if (result.size() > kBytes) result.resize(utf8_prefix(result, kBytes));
  return result;
}
struct Budget {
  std::size_t limit; // 0: no limit.
  std::size_t used = 0;
  bool limited = false;
  void copy(Json& target, std::string_view key, std::string_view value, bool indivisible = false) {
    const auto remaining = limit - used;
    const auto length = !limit ? value.size() : indivisible && value.size() > remaining ? 0 : utf8_prefix(value, remaining);
    if (length < value.size()) limited = true;
    if (length || value.empty()) target[key] = std::string(value.substr(0, length));
    used += length;
  }
};
Json make_node(const Json& source, std::size_t index, bool hidden, bool descendant_hidden, Budget& budget) {
  Json result{{"index", index}};
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
  return result;
}
bool incomplete(const Json& observation) { return truth(observation, "truncated"); }
constexpr auto kNone = static_cast<std::size_t>(-1);
std::size_t reference(const Json& value, std::size_t below, const char* what) {
  if (!value.is_integer() || value.as_int() < 0 || static_cast<std::uint64_t>(value.as_int()) >= below) throw std::invalid_argument(what);
  return static_cast<std::size_t>(value.as_int());
}
std::size_t parent(const Json& node) { const auto value = field(node, "parent"); return value ? static_cast<std::size_t>(value->as_int()) : kNone; }
// Ancestor chains are interned to small ids shared by both observations, so equal chains compare equal in O(1)
// and no per-node copy of a chain is ever built.
struct Chains {
  std::map<std::tuple<std::size_t, std::string, std::string>, std::size_t> contexts;
  std::map<std::pair<std::size_t, std::int64_t>, std::size_t> slots;
  std::size_t context(std::size_t outer, const Json& entry) {
    return contexts.emplace(std::tuple{outer, std::string(text(entry, "role")), std::string(text(entry, "name"))}, contexts.size() + 1).first->second;
  }
  std::size_t slot(std::size_t outer, const Json& entry) { return slots.emplace(std::pair{outer, entry.at("index").as_int()}, slots.size() + 1).first->second; }
};
struct Observed {
  const Json::array* nodes;
  const Json::array* ancestors;
  std::vector<std::size_t> context, slot; // Per ancestor entry; 0 is the empty chain.
  std::size_t context_of(const Json& node) const { const auto at = parent(node); return at == kNone ? 0 : context[at]; }
  std::size_t slot_of(const Json& node) const { const auto at = parent(node); return at == kNone ? 0 : slot[at]; }
};
const Json::array kNoAncestors;
Observed checked(const Json& observation, Chains& chains) {
  const auto nodes = field(observation, "nodes");
  if (!nodes || !nodes->is_array())
    throw std::invalid_argument("Expected compact observation nodes");
  const auto ancestors = field(observation, "ancestors");
  if (ancestors && !ancestors->is_array()) throw std::invalid_argument("Expected compact observation ancestors");
  Observed result{&nodes->as_array(), ancestors ? &ancestors->as_array() : &kNoAncestors, {}, {}};
  const auto& entries = *result.ancestors;
  for (std::size_t at = 0; at < entries.size(); ++at) {
    const auto& entry = entries[at];
    if (!entry.is_object() || !entry.contains("index") || !entry.at("index").is_integer())
      throw std::invalid_argument("Expected compact observation ancestor with index");
    // Entries precede their descendants, so one forward pass resolves every chain.
    const auto outer = entry.contains("parent") ? reference(entry.at("parent"), at, "Compact observation ancestor parent must precede it") : kNone;
    const auto outer_context = outer == kNone ? 0 : result.context[outer];
    result.context.push_back(entry.contains("role") || entry.contains("name") ? chains.context(outer_context, entry) : outer_context);
    result.slot.push_back(chains.slot(outer == kNone ? 0 : result.slot[outer], entry));
  }
  for (const auto& node : *result.nodes) {
    if (!node.is_object() || !node.contains("index") || !node.at("index").is_integer())
      throw std::invalid_argument("Expected compact observation node with index");
    if (node.contains("parent")) (void)reference(node.at("parent"), entries.size(), "Compact observation node parent is not an ancestor");
  }
  return result;
}
Json semantic(const Json& node, const Observed& observed) {
  Json result = node;
  for (const auto key : {"ref", "index", "parent", "box"}) result.as_object().erase(key);
  result["context"] = observed.context_of(node);
  return result;
}
std::string identity(const Json& node, const Observed& observed) {
  auto label = text(node, "name");
  // A text node is its text; a nameless control is known by the text it carries.
  if (label.empty()) label = text(node, text(node, "role") == "text" ? "text" : "label");
  return Json{{"role", std::string(text(node, "role"))}, {"name", std::string(label)}, {"context", observed.context_of(node)}}.dump();
}
std::string slot(const Json& node, const Observed& observed) {
  return Json{{"role", std::string(text(node, "role"))}, {"outer", observed.slot_of(node)}, {"index", node.at("index")}}.dump();
}
// Copies the ancestor entries a node refers to into a destination table once, rewriting parent indices.
struct Remap {
  const Json::array* source;
  Json::array* target;
  std::vector<std::size_t> moved = std::vector<std::size_t>(source->size(), kNone);
  Json operator()(Json node) {
    const auto first = parent(node);
    if (first == kNone) return node;
    std::vector<std::size_t> chain;
    for (auto at = first; moved[at] == kNone;) {
      chain.push_back(at);
      at = parent((*source)[at]);
      if (at == kNone) break;
    }
    for (auto at = chain.rbegin(); at != chain.rend(); ++at) {
      Json entry = (*source)[*at];
      if (entry.contains("parent")) entry["parent"] = moved[parent(entry)];
      moved[*at] = target->size();
      target->push_back(std::move(entry));
    }
    node["parent"] = moved[first];
    return node;
  }
};
} // namespace

Json compact_observation(const Json& forest, ObservationOptions options) {
  if (!forest.is_array()) throw std::invalid_argument("Observation must be an array forest");
  struct Frame {
    const Json::array* nodes;
    std::size_t index = 0;
    bool protected_parent = false;
    std::size_t position = 0; // The container's index among its siblings.
    bool named = false;
    std::size_t entry = kNone; // Its ancestors-table index, once an emitted descendant needs it.
    bool pointer = false; // The container shows a pointer cursor.
  };
  std::vector<Frame> frames{{&forest.as_array()}};
  Json::array ancestors, output;
  Budget budget{options.max_text_bytes};
  // Each open container is emitted into the ancestor table at most once, outermost first, so a node refers to its
  // whole chain by one index and the output stays linear in the forest at any depth.
  const auto enclosing = [&]() -> std::size_t {
    auto first = frames.size();
    while (first > 1 && frames[first - 1].entry == kNone) --first;
    for (auto at = first; at < frames.size(); ++at) {
      Json entry{{"index", frames[at].position}};
      if (at > 1) entry["parent"] = frames[at - 1].entry;
      if (frames[at].named) {
        const auto& container = frames[at - 1].nodes->at(frames[at].position);
        budget.copy(entry, "role", text(container, "role"), true);
        budget.copy(entry, "name", text(container, "name"));
      }
      frames[at].entry = ancestors.size();
      ancestors.push_back(std::move(entry));
    }
    return frames.back().entry;
  };
  std::size_t scanned = 0;
  bool node_limit = false, scan_limit = false;
  while (!frames.empty()) {
    auto& frame = frames.back();
    if (frame.index == frame.nodes->size()) { frames.pop_back(); continue; }
    if (options.max_scan_nodes && scanned >= options.max_scan_nodes) { scan_limit = true; break; }
    const auto index = frame.index++;
    const auto& node = frame.nodes->at(index);
    ++scanned;
    if (!node.is_object() && !node.is_string()) continue;
    const bool protected_parent = frame.protected_parent;
    const bool hidden = protected_parent || password(node);
    if (!options.interactive_only || listed(node, frame.pointer)) {
      if (options.max_nodes && output.size() >= options.max_nodes) { node_limit = true; break; }
      auto made = make_node(node, index, hidden, protected_parent, budget);
      // Controls are listed without the text around them, so a nameless one carries its own. Whole or not at all:
      // a cut label would name the same control differently from one observation to the next.
      if (options.interactive_only && !hidden && !made.contains("name") && !holds_value(node) && field(node, "children"))
        if (const auto label = visible_text(node); !label.empty()) budget.copy(made, "label", label, true);
      if (const auto at = enclosing(); at != kNone) made["parent"] = at;
      output.push_back(std::move(made));
    }
    const auto children = field(node, "children");
    if (!children || !children->is_array() || children->as_array().empty()) continue;
    frames.push_back({&children->as_array(), 0, hidden, index, !hidden && !text(node, "name").empty(), kNone, pointer(node)});
  }
  return Json{{"nodes", std::move(output)}, {"ancestors", std::move(ancestors)},
              {"truncated", node_limit || scan_limit || budget.limited},
              {"nodeLimit", node_limit}, {"textLimit", budget.limited}, {"scanLimit", scan_limit},
              {"scannedNodes", scanned}, {"textBytes", budget.used}};
}

Json observation_diff(const Json& previous, const Json& current, std::size_t max_changes) {
  Chains chains;
  const auto before_seen = checked(previous, chains), after_seen = checked(current, chains);
  const auto& before = *before_seen.nodes;
  const auto& after = *after_seen.nodes;
  using Groups = std::map<std::string, std::vector<std::size_t>>;
  Groups old_ids, new_ids, old_slots, new_slots;
  std::vector<std::string> old_identity, new_identity;
  for (std::size_t i = 0; i < before.size(); ++i) {
    old_identity.push_back(identity(before[i], before_seen));
    old_ids[old_identity.back()].push_back(i); old_slots[slot(before[i], before_seen)].push_back(i);
  }
  for (std::size_t i = 0; i < after.size(); ++i) {
    new_identity.push_back(identity(after[i], after_seen));
    new_ids[new_identity.back()].push_back(i); new_slots[slot(after[i], after_seen)].push_back(i);
  }
  std::vector<bool> old_used(before.size()), new_used(after.size());
  Json::array added, removed, changed, ancestors;
  // Reported nodes refer into this result's own ancestor table, holding only the chains they use.
  Remap from_before{before_seen.ancestors, &ancestors}, from_after{after_seen.ancestors, &ancestors};
  std::size_t changes = 0;
  bool limited = false, ambiguous = false;
  auto admit = [&] {
    if (max_changes && changes >= max_changes) { limited = true; return false; }
    ++changes;
    return true;
  };
  auto pair = [&](std::size_t old_index, std::size_t new_index) {
    old_used[old_index] = true; new_used[new_index] = true;
    if (semantic(before[old_index], before_seen) != semantic(after[new_index], after_seen) && admit())
      changed.push_back(Json{{"before", from_before(before[old_index])}, {"after", from_after(after[new_index])}});
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
    if (!old_used[old_index] && !new_used[new_index] && old_ids.at(old_identity[old_index]).size() == 1 &&
        new_ids.at(new_identity[new_index]).size() == 1) pair(old_index, new_index);
  }
  const bool complete = !incomplete(previous) && !incomplete(current);
  for (std::size_t i = 0; i < after.size(); ++i) if (!new_used[i] && admit()) added.push_back(from_after(after[i]));
  if (complete)
    for (std::size_t i = 0; i < before.size(); ++i) if (!old_used[i] && admit()) removed.push_back(from_before(before[i]));
  for (const auto& [key, indices] : new_ids) { (void)key; if (indices.size() > 1) ambiguous = true; }
  return Json{{"added", std::move(added)}, {"removed", std::move(removed)}, {"changed", std::move(changed)},
              {"ancestors", std::move(ancestors)}, {"ambiguous", ambiguous}, {"complete", complete && !limited},
              {"truncated", limited}, {"removalsSuppressed", !complete}};
}
} // namespace suitecut
