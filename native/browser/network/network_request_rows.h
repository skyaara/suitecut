// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <vector>
namespace suitecut {

// Requests are keyed "<session>\n<requestId>". A detached target never
// finishes its requests: drop them from network activity and close their
// retained responses, so idle waits and the bounded table don't wait on them.
// Returns the frames whose activity ended.
template <class Active, class Documents>
std::vector<std::string> RetireSessionRequests(Active& active, Documents& documents,
                                               const std::string& session) {
  std::vector<std::string> frames;
  if (session.empty()) return frames;
  const auto prefix = session + "\n";
  for (auto it = active.lower_bound(prefix); it != active.end() && it->first.starts_with(prefix);) {
    frames.push_back(it->second.frame);
    it = active.erase(it);
  }
  for (auto it = documents.lower_bound(prefix);
       it != documents.end() && it->first.starts_with(prefix); ++it)
    if (!it->second.finished) {
      it->second.finished = true;
      if (it->second.error.empty()) it->second.error = "Target closed";
    }
  return frames;
}

// Make room in the bounded response table: the oldest finished response,
// else the oldest request that isn't a document, else the oldest of all.
// A page full of requests that never end must not close the browser.
template <class Documents>
void EvictDocument(Documents& documents, std::deque<std::string>& order) {
  const auto oldest = [&](auto keep) {
    return std::find_if(order.begin(), order.end(), [&](const std::string& key) {
      const auto found = documents.find(key);
      return found != documents.end() && keep(found->second);
    });
  };
  auto victim = oldest([](const auto& row) { return row.finished; });
  if (victim == order.end()) victim = oldest([](const auto& row) { return !row.document; });
  if (victim == order.end()) victim = oldest([](const auto&) { return true; });
  if (victim == order.end()) return;
  documents.erase(*victim);
  order.erase(victim);
}

}  // namespace suitecut
