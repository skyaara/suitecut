// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <optional>
#include <stdexcept>

namespace suitecut::runner {
// One synchronous native state-machine event owns one real observation. Nested
// host sends must not sample a newer time and then return to the older event.
// A Scope must end before the caller suspends a coroutine or polls again.
class FixtureTransitionTime {
 public:
  class Scope {
   public:
    Scope(FixtureTransitionTime& time, double now) : time_(time), previous_(time.current_) {
      if (!std::isfinite(now) || now < 0 || (previous_ && *previous_ != now))
        throw std::invalid_argument("Native fixture transition event time rejected");
      time_.current_ = now;
    }
    ~Scope() { time_.current_ = previous_; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
   private:
    FixtureTransitionTime& time_;
    std::optional<double> previous_;
  };
  std::optional<double> Current() const { return current_; }
 private:
  std::optional<double> current_;
};
} // namespace suitecut::runner
