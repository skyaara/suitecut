// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <stdexcept>

namespace suitecut::runner {
// Final publication policy only. This type cannot schedule, stop, retry or
// reclassify a test. Its count always comes from the native validated summary.
struct FailOnFlakyTestsPolicy {
  bool enabled=false;
  static FailOnFlakyTestsPolicy PublicRoot(bool requested,bool internal_actor){return {requested&&!internal_actor};}
  bool Failed(size_t actual_flaky)const noexcept{return enabled&&actual_flaky!=0;}
  int FinalExit(int actual_exit,size_t actual_flaky)const noexcept{return actual_exit?actual_exit:Failed(actual_flaky)?1:0;}
  void Validate(size_t claimed_flaky,bool claimed_failed,size_t actual_flaky)const{
    if(!enabled||claimed_flaky!=actual_flaky||claimed_failed!=Failed(actual_flaky))throw std::invalid_argument("Native fail-on-flaky policy contradicts actual final summary");
  }
  template<class Sink>void Write(Sink& out,size_t actual_flaky)const{
    if(!enabled)throw std::logic_error("Absent native fail-on-flaky policy cannot be published");
    out.Put("{\"failOnFlakyTests\":true,\"flakyTests\":");out.NumberValue(actual_flaky);out.Put(",\"failed\":");out.Put(Failed(actual_flaky)?"true":"false");out.Put("}");
  }
  template<class Sink>void Properties(Sink& out,size_t actual_flaky)const{
    if(!enabled)return;
    out.Put("<property name=\"native.failOnFlakyTests\" value=\"true\"/><property name=\"native.flakyTests\" value=\"");out.NumberValue(actual_flaky);out.Put("\"/><property name=\"native.runPolicyFailed\" value=\"");out.Put(Failed(actual_flaky)?"true":"false");out.Put("\"/>");
  }
  template<class Stream>void Console(Stream& out,size_t actual_flaky)const{
    if(Failed(actual_flaky))out<<"Native run failed: --fail-on-flaky-tests ("<<actual_flaky<<(actual_flaky==1?" flaky test)\n":" flaky tests)\n");
  }
  template<class Sink>void HTML(Sink& out,size_t actual_flaky)const{
    if(!enabled)return;
    out.Put("<section><h2>Native run policy</h2><p>Fail on flaky tests: enabled; policy outcome: ");out.Put(Failed(actual_flaky)?"failed":"passed");out.Put("; actual flaky tests: ");out.NumberValue(actual_flaky);out.Put(". Authored test outcomes and attempts remain unchanged.</p></section>");
  }
};
} // namespace suitecut::runner
