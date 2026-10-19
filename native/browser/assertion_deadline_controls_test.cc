// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "assertion_deadline_controls.h"
#include <cassert>
#include <iostream>
using Controls=suitecut::AssertionDeadlineControls;
int main(){
  {Controls c;assert(!c.Admit(0,1,40,"toHaveText")&&!c.Admit(1,0,40,"toHaveText")&&!c.Admit(1,1,0,"toHaveText")&&!c.Admit(1,1,40,"toHaveJSProperty"));assert(c.Admit(1,1,40,"toHaveText")&&!c.Admit(1,1,40,"toHaveText"));}
  {Controls c;assert(c.Admit(1,1,40,"toHaveText"));assert(!c.Freeze(1,1,39)&&!c.Freeze(1,2,40)&&c.Freeze(1,1,40));assert(c.Get(1)->deadline==40&&c.Get(1)->cleanup_deadline==1040);assert(c.Freeze(1,1,100)&&c.Get(1)->cleanup_deadline==1040);}
  {Controls c;assert(c.Admit(1,1,40,"toHaveText")&&c.Freeze(1,1,40));assert(c.Ready(1,1,true)==Controls::Verdict::HardFailure);assert(c.Validated(1,1));assert(c.Ready(1,1,false)==Controls::Verdict::Hold&&c.Ready(1,1,true)==Controls::Verdict::Deadline);}
  {Controls c;assert(c.Admit(1,1,40,"toHaveText")&&c.Validated(1,1)&&c.Fail(1,1,"Actual protocol rejection",25));assert(c.Ready(1,1,true)==Controls::Verdict::HardFailure);assert(c.Fail(1,1,"later error",26)&&c.Get(1)->error=="Actual protocol rejection");}
  {Controls c;assert(c.Admit(1,1,40,"toHaveText")&&c.Freeze(1,1,40));assert(!c.Watchdog(1,1,1039)&&c.Watchdog(1,1,1040)&&!c.Watchdog(1,1,1041));assert(c.Size()==1&&!c.Cancel(1,2)&&c.Cancel(1,1)&&!c.NeedsTick());}
  {Controls c;for(int i=1;i<=64;++i)assert(c.Admit(i,1,40,"toBeVisible"));assert(!c.Admit(65,1,40,"toBeVisible")&&c.Cancel(1,1)&&c.Admit(65,1,40,"toBeVisible"));assert(c.Owners().size()==64);}
  {Controls c;assert(c.Admit(1,1,40,"toHaveText")&&c.Cancel(1,1)&&c.Admit(1,2,80,"toHaveText"));assert(!c.Validated(1,1)&&!c.Fail(1,1,"stale reply",90)&&!c.Freeze(1,1,90)&&c.Get(1)->generation==2);}
  {Controls c;assert(c.Admit(1,1,40,"toHaveText")&&c.Validated(1,1));assert(c.Ready(1,1,true)==Controls::Verdict::Hold);assert(!c.Freeze(1,1,std::nan(""))&&!c.Fail(1,1,"bad clock",std::nan("")));}
  {Controls c;assert(c.Admit(1,1,40,"toHaveText")&&c.Validated(1,1)&&c.Admitted(1,1)&&c.Freeze(1,1,40));assert(c.Ready(1,1,true)==Controls::Verdict::HardFailure&&!c.Admitted(1,1));}
  std::cout<<"PASS9 native assertion deadline policy controls; no renderer integration claim\n";
}
