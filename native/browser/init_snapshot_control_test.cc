// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "init_snapshot_control.h"
#include "renderer_app.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <cstdio>
using Control=suitecut::InitSnapshotControl;
int main(){assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
 {uint64_t value=0;for(const auto&text:{"","0","01","-1","+1","1 ","18446744073709551616"})assert(!Control::UInt(text,value));assert(Control::UInt("18446744073709551615",value)&&value==UINT64_MAX);++groups;}
 {auto message=Control::Admission(Control::kAdmission,4,9,11,"actual-frame");int context=0;uint64_t epoch=0,request=0;std::string frame;assert(Control::Admission(message,context,epoch,request,frame)&&context==4&&epoch==9&&request==11&&frame=="actual-frame");++groups;}
 {auto message=Control::Admission(Control::kAdmission,4,9,11,"actual-frame");message->GetArgumentList()->SetDouble(1,9);int context=0;uint64_t epoch=0,request=0;std::string frame;assert(!Control::Admission(message,context,epoch,request,frame));++groups;}
 {const suitecut::InitSnapshotPin pin{{4,8,19,"actual-frame"},UINT64_MAX,UINT64_MAX};auto message=Control::Receipt(Control::kAck,pin,true);suitecut::InitSnapshotPin actual;bool installed=false;assert(Control::Receipt(message,actual,installed)&&actual==pin&&installed);++groups;}
 {const suitecut::InitSnapshotPin pin{{4,8,19,"actual-frame"},7,3};auto message=Control::Receipt(Control::kReleased,pin);message->GetArgumentList()->SetString(5,"0");suitecut::InitSnapshotPin actual;bool installed=false;assert(!Control::Receipt(message,actual,installed));++groups;}
 {const suitecut::InitSnapshotPin pin{{4,8,19,"actual-frame"},7,3};auto message=Control::Receipt(Control::kReleased,pin);message->GetArgumentList()->SetString(3,std::string(257,'x'));suitecut::InitSnapshotPin actual;bool installed=false;assert(!Control::Receipt(message,actual,installed));++groups;}
 std::printf("Init snapshot native control codec: %d actual CEF message groups PASS (no IPC claim)\n",groups);
}
