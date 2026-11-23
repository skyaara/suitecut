// SPDX-License-Identifier: MIT
#include "network/network_request_rows.h"
#include <cassert>
#include <iostream>
struct Active { std::string frame; };
struct Row { bool finished = false; std::string error; bool document = false; };
int main() {
  size_t controls = 0;
  std::map<std::string, Active> active{{"\nmain", {"F0"}}, {"A\n1", {"F1"}}, {"A\n2", {"F1"}}, {"AB\n3", {"F2"}}};
  std::map<std::string, Row> documents{{"\nmain", {}}, {"A\n1", {}}, {"A\n2", {true, "", false}}, {"A\n4", {false, "net::ERR_ABORTED", true}}, {"AB\n3", {}}};
  // Only the detached session's rows retire; a session whose id extends it stays.
  const auto frames = suitecut::RetireSessionRequests(active, documents, "A");
  assert(frames.size() == 2 && frames[0] == "F1" && active.size() == 2 && active.contains("AB\n3") && active.contains("\nmain")); ++controls;
  assert(documents["A\n1"].finished && documents["A\n1"].error == "Target closed"); ++controls;
  assert(documents["A\n2"].finished && documents["A\n2"].error.empty()); ++controls;
  assert(documents["A\n4"].finished && documents["A\n4"].error == "net::ERR_ABORTED"); ++controls;
  assert(!documents["AB\n3"].finished && !documents["\nmain"].finished); ++controls;
  // The page's own session (empty) is never retired this way.
  assert(suitecut::RetireSessionRequests(active, documents, "").empty() && active.size() == 2); ++controls;

  // Eviction prefers the oldest finished row, then non-documents, then the oldest.
  std::map<std::string, Row> table{{"a", {false, "", true}}, {"b", {false, "", false}}, {"c", {true, "", true}}, {"d", {false, "", false}}};
  std::deque<std::string> order{"a", "b", "c", "d"};
  suitecut::EvictDocument(table, order);
  assert(!table.contains("c") && order == std::deque<std::string>({"a", "b", "d"})); ++controls;
  suitecut::EvictDocument(table, order);
  assert(!table.contains("b") && order == std::deque<std::string>({"a", "d"})); ++controls;
  table["d"].document = true;
  suitecut::EvictDocument(table, order);
  assert(!table.contains("a") && order == std::deque<std::string>({"d"})); ++controls;
  // A stale order key without a row is skipped, never erased blindly.
  order.push_front("gone");
  suitecut::EvictDocument(table, order);
  assert(table.empty() && order == std::deque<std::string>({"gone"})); ++controls;
  suitecut::EvictDocument(table, order);
  assert(order.size() == 1); ++controls;
  assert(controls == 11);
  std::cout << "network request rows: " << controls << " controls\n";
}
