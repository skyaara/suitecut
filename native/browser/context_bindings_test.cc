// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "context_bindings.h"
int main() {
  SuiteCutContextBindings registry, other; SuiteCutContextBindings::Entry entry, second; std::string error;
  assert(registry.Add("native_1", "callback", "function(){}", "function(){}", true, entry, error));
  assert(registry.SetBootstrap(entry.name, entry.id, "native-script"));
  assert(!registry.SetBootstrap(entry.name, entry.id, "replacement"));
  assert(other.Add("native_2", "callback", "function(){}", "function(){}", false, second, error));
  assert(entry.bridge != second.bridge && entry.state == entry.bridge + "_state");
  assert(!registry.Add("native_1", "callback", "function(){}", "function(){}", false, second, error));
  assert(!registry.Remove(entry.name, entry.id + 1));
  assert(registry.Remove(entry.name, entry.id) && registry.Bytes() == 0 && registry.Size() == 0);
  assert(!registry.Add("native_1", "", "function(){}", "function(){}", false, second, error));
  assert(!registry.Add("native_1", "x", std::string(1048576, 'x'), "x", false, second, error));
  assert(!registry.Add("native_1",std::string(257,'a'),"function(){}","function(){}",false,second,error));
  std::string unit_name;for(int i=0;i<256;++i)unit_name+="\xed\xa0\x80";
  assert(SuiteCutBindingNameUnits(unit_name)==256);assert(registry.Add("native_1",unit_name,"function(){}","function(){}",false,second,error));assert(registry.Remove(second.name,second.id));
  assert(!registry.Add("native_1",unit_name+"a","function(){}","function(){}",false,second,error));
  assert(registry.Add("native_1",std::string(1,'\0'),"function(){}","function(){}",false,second,error));assert(registry.Remove(second.name,second.id));
  assert(!registry.Add("native_1",std::string("\xc0\x80",2),"function(){}","function(){}",false,second,error));
  for (int index = 0; index < 64; ++index) assert(registry.Add("native_1", "callback" + std::to_string(index), "function(){}", "function(){}", false, second, error));
  assert(!registry.Add("native_1", "overflow", "function(){}", "function(){}", false, second, error));
  while (registry.Size()) { const auto item = registry.All().begin()->second; assert(registry.Remove(item.name, item.id)); }
  for (int index = 0; index < 100000; ++index) {
    assert(registry.Add("native_1", "callback", "function(){}", "function(){}", false, second, error));
    assert(second.id > entry.id && second.bridge != entry.bridge);
    assert(registry.SetBootstrap(second.name, second.id, "native-script"));
    assert(!registry.Remove(second.name, entry.id));
    assert(registry.Remove(second.name, second.id)); assert(registry.Bytes() == 0 && registry.Size() == 0);
  }
  std::cout << "context binding registry: native identities, stale removal, capacity, 100000 ownership cycles passed\n";
}
