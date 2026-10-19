// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iterator>
#include "include/cef_api_hash.h"
#ifndef SUITECUT_WEBSOCKET_CONTROLS_HEADER
#define SUITECUT_WEBSOCKET_CONTROLS_HEADER "websocket_controls.h"
#endif
#include SUITECUT_WEBSOCKET_CONTROLS_HEADER
int main(int argc,char**argv){
 assert(cef_api_hash(CEF_API_VERSION,0));
 using Controls=suitecut::NativeWebSocketControls;using Dict=Controls::Dict;
 std::vector<std::pair<Dict,Controls::Bytes>> emitted;std::string failure;bool accept=true;int wakes=0;
 Controls controls([&](Dict event,const Controls::Bytes&bytes){if(!accept)return false;emitted.emplace_back(event->Copy(false),bytes);return true;},[&]{++wakes;},[&](const std::string&error){failure=error;});
 std::vector<std::pair<double,Controls::Bytes>> observed;controls.SetObserver([&](Dict,const Controls::Bytes&bytes,double now){observed.emplace_back(now,bytes);});
 auto created=CefDictionaryValue::Create();created->SetString("requestId","actual-request");created->SetString("url","ws://127.0.0.1/actual");controls.Event("Network.webSocketCreated",created,"",1);assert(emitted.size()==1&&controls.sockets()==1);
 bool replayed=false;assert(controls.Wait(99,"1","created",0,100,[&](bool ok,Dict event,const Controls::Bytes&,const std::string&){assert(ok&&event->IsValid()&&event->GetString("url")=="ws://127.0.0.1/actual");replayed=true;}));assert(replayed);
 bool waited=false;assert(controls.Wait(1,"1","framereceived",1,100,[&](bool ok,Dict event,const Controls::Bytes&bytes,const std::string&){assert(ok&&event->GetInt("opcode")==2&&bytes.size()==256);for(size_t i=0;i<bytes.size();++i)assert(bytes[i]==i);assert(emitted.size()==2);waited=true;}));
 std::vector<uint8_t> expected(256);for(size_t i=0;i<expected.size();++i)expected[i]=i;
 auto frame=CefDictionaryValue::Create();frame->SetInt("opcode",2);frame->SetString("payloadData",CefBase64Encode(expected.data(),expected.size()));auto params=CefDictionaryValue::Create();params->SetString("requestId","actual-request");params->SetDictionary("response",frame);accept=false;controls.Event("Network.webSocketFrameReceived",params,"",2);assert(!waited&&controls.pending()==1&&wakes>0);assert(observed.size()==2&&observed.back().first==2&&observed.back().second==expected);bool pending_replay=false;assert(controls.Wait(98,"1","framereceived",1,100,[&](bool ok,Dict,const Controls::Bytes&,const std::string&){assert(ok&&emitted.size()==2);pending_replay=true;}));assert(!pending_replay);accept=true;controls.Tick(3);assert(waited&&pending_replay&&controls.pending()==0&&emitted.back().second==expected);assert(observed.size()==2);
 controls.Event("Network.webSocketCreated",created,"actual-child-session",4);assert(emitted.back().first->GetString("socketId")=="2"&&controls.sockets()==2);controls.SessionDetached("actual-child-session",5);assert(emitted.back().first->GetString("event")=="close"&&controls.sockets()==1);
 bool timedout=false;assert(controls.Wait(2,"1","close",controls.sequence(),6,[&](bool ok,Dict,const Controls::Bytes&,const std::string&error){timedout=!ok&&!error.empty();}));controls.Tick(6);assert(timedout);
 assert(controls.Wait(3,"1","close",controls.sequence(),6,[](bool,Dict,const Controls::Bytes&,const std::string&){assert(false);}));controls.Cancel(3);controls.Tick(6);
 accept=false;controls.Event("Network.webSocketFrameReceived",params,"",10);controls.Tick(30010);assert(!failure.empty()&&controls.retained_bytes()==0);
 // Accepted output must precede close completion; a blocked output cannot
 // complete a native wait, including a waiter registered from its journal.
 {bool accepted=false,closed=false,expired=false;int outputs=0;
  Controls ordered([&](Dict,const Controls::Bytes&){if(!accepted)return false;++outputs;return true;},[]{},[](const std::string&){assert(false);});
  ordered.Event("Network.webSocketCreated",created,"",1);assert(ordered.pending()==1);
  assert(ordered.Wait(1,"1","created",0,3,[&](bool ok,Dict,const Controls::Bytes&,const std::string&){assert(!ok&&outputs==0);expired=true;}));
  ordered.Tick(3);assert(expired&&ordered.pending()==1);accepted=true;ordered.Tick(4);assert(outputs==1);
  assert(ordered.Wait(2,"1","close",ordered.sequence(),10,[&](bool ok,Dict,const Controls::Bytes&,const std::string&){assert(ok&&outputs==2);closed=true;}));
  accepted=false;ordered.Event("Network.webSocketClosed",created,"",5);assert(!closed&&ordered.pending()==1);accepted=true;ordered.Tick(6);assert(closed&&outputs==2&&ordered.pending()==0);
 }
 // A genuine close rejects other waits only after Writer admission. Another
 // socket, a close waiter, and a canceled waiter keep their own ownership.
 {bool accepted=true,rejected=false,closed=false,other=false;int outputs=0;
  Controls early([&](Dict,const Controls::Bytes&){if(!accepted)return false;++outputs;return true;},[]{},[](const std::string&){assert(false);});
  early.Event("Network.webSocketCreated",created,"",1);
  early.Event("Network.webSocketCreated",created,"child",2);
  const auto cursor=early.sequence();
  assert(early.Wait(1,"1","framereceived",cursor,100,[&](bool ok,Dict event,const Controls::Bytes&bytes,const std::string&error){assert(!ok&&!event&&bytes.empty()&&error=="WebSocket closed"&&outputs==3);rejected=true;}));
  assert(early.Wait(2,"1","close",cursor,100,[&](bool ok,Dict event,const Controls::Bytes&,const std::string&error){assert(ok&&event->GetString("event")=="close"&&error.empty()&&outputs==3);closed=true;}));
  assert(early.Wait(3,"2","framereceived",cursor,100,[&](bool ok,Dict,const Controls::Bytes&,const std::string&error){assert(!ok&&error=="Native WebSocket event deadline exceeded");other=true;}));
  assert(early.Wait(4,"1","framesent",cursor,100,[](bool,Dict,const Controls::Bytes&,const std::string&){assert(false);}));early.Cancel(4);
  accepted=false;early.Event("Network.webSocketClosed",created,"",3);assert(!rejected&&!closed&&!other&&early.pending()==1);
  bool late=false;assert(early.Wait(5,"1","framesent",early.sequence(),100,[&](bool ok,Dict,const Controls::Bytes&,const std::string&error){assert(!ok&&error=="WebSocket closed"&&outputs==3);late=true;}));assert(!late);
  accepted=true;early.Tick(4);assert(rejected&&closed&&late&&!other&&early.pending()==0);
  bool already=false;assert(early.Wait(6,"1","socketerror",early.sequence(),100,[&](bool ok,Dict,const Controls::Bytes&,const std::string&error){assert(!ok&&error=="WebSocket closed");already=true;}));assert(already);
  assert(!early.Wait(7,"3","close",early.sequence(),100,[](bool,Dict,const Controls::Bytes&,const std::string&){assert(false);}));
  assert(!early.Wait(7,"01","close",early.sequence(),100,[](bool,Dict,const Controls::Bytes&,const std::string&){assert(false);}));
  early.Tick(100);assert(other);
 }
 // A frame preceding close wins, including pending/replayed output. A close
 // cannot extend the original deadline while its Writer remains blocked.
 {bool accepted=true,frame_won=false,replay_won=false,expired=false;int outputs=0;
  Controls ordered([&](Dict,const Controls::Bytes&){if(!accepted)return false;++outputs;return true;},[]{},[](const std::string&){assert(false);});
  ordered.Event("Network.webSocketCreated",created,"",1);const auto cursor=ordered.sequence();
  assert(ordered.Wait(1,"1","framereceived",cursor,100,[&](bool ok,Dict event,const Controls::Bytes&bytes,const std::string&error){assert(ok&&event->GetString("event")=="framereceived"&&bytes==expected&&error.empty()&&outputs==2);frame_won=true;}));
  accepted=false;ordered.Event("Network.webSocketFrameReceived",params,"",2);ordered.Event("Network.webSocketClosed",created,"",3);
  assert(ordered.Wait(2,"1","framereceived",cursor,100,[&](bool ok,Dict,const Controls::Bytes&bytes,const std::string&){assert(ok&&bytes==expected&&outputs==2);replay_won=true;}));
  assert(ordered.Wait(3,"1","framesent",cursor,4,[&](bool ok,Dict,const Controls::Bytes&,const std::string&error){assert(!ok&&error=="Native WebSocket event deadline exceeded"&&outputs==1);expired=true;}));
  ordered.Tick(4);assert(expired&&!frame_won&&!replay_won&&ordered.pending()==2);
  accepted=true;ordered.Tick(5);assert(frame_won&&replay_won&&outputs==3);
  bool replay=false;assert(ordered.Wait(4,"1","framereceived",cursor,100,[&](bool ok,Dict,const Controls::Bytes&bytes,const std::string&){assert(ok&&bytes==expected);replay=true;}));assert(replay);
 }
 // Session detachment supplies an actual close. Closed identity knowledge is
 // bounded by the issued serial/live table even after journal eviction.
 {Controls detached([](Dict,const Controls::Bytes&){return true;},[]{},[](const std::string&){assert(false);});
  detached.Event("Network.webSocketCreated",created,"child",1);bool rejected=false;
  assert(detached.Wait(1,"1","framereceived",detached.sequence(),100,[&](bool ok,Dict,const Controls::Bytes&,const std::string&error){assert(!ok&&error=="WebSocket closed");rejected=true;}));
  detached.SessionDetached("child",2);assert(rejected&&detached.sockets()==0);
  detached.Event("Network.webSocketCreated",created,"",3);
  for(int i=0;i<520;++i)detached.Event("Network.webSocketFrameReceived",params,"",4+i);
  bool evicted=false;assert(detached.Wait(2,"1","framereceived",detached.sequence(),1000,[&](bool ok,Dict,const Controls::Bytes&,const std::string&error){assert(!ok&&error=="WebSocket closed");evicted=true;}));assert(evicted);
 }
 if(argc>1){
  std::ifstream input(argv[1]);const std::string bytes((std::istreambuf_iterator<char>(input)),{});auto parsed=CefParseJSON(bytes,JSON_PARSER_RFC);auto records=parsed?parsed->GetList():nullptr;assert(records&&records->GetSize()>=8);int sent=0,received=0,closed=0;
  Controls actual([&](Dict event,const Controls::Bytes&payload){auto kind=event->GetString("event").ToString();if(kind=="framesent")++sent;if(kind=="framereceived")++received;if(kind=="close")++closed;if(event->GetInt("opcode")==2)assert(payload==expected);return true;},[]{},[](const std::string&){assert(false);});
  for(size_t i=0;i<records->GetSize();++i){auto record=records->GetDictionary(i);actual.Event(record->GetString("method"),record->GetDictionary("params"),record->GetString("sessionId"),double(i));}assert(sent==2&&received==2&&closed==1&&actual.sockets()==0);
 }
}
