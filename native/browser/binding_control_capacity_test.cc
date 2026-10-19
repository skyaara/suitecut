#include "operation_registry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main() {
 SuiteCutOperationRegistry r;
 for(int i=1;i<=64;++i) assert(r.Begin(i,0,100));
 assert(!r.Begin(65,0,100));
 for(int i=101;i<=164;++i) assert(r.BeginControl(i,0,100));
 assert(r.Size()==128 && r.Controls()==64);
 assert(!r.BeginControl(165,0,100));assert(!r.BeginControl(1,0,100));
 const auto old=r.Get(101);assert(old);assert(r.Finish(101));assert(r.BeginControl(101,5,100));assert(!r.IsCurrent(101,old->generation));
 assert(r.Expired(100).size()==127);assert(r.Finish(1));assert(r.Begin(1,5,100));
 r.Clear();assert(r.Size()==0&&r.Controls()==0);assert(r.Begin(1,0,100));assert(r.BeginControl(2,0,100));
}
