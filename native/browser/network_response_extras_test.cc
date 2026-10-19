// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_response_extras.h"
using Extras=suitecut::NativeNetworkResponseExtras;using Dict=Extras::Dict;
Dict Headers(const std::string&value){auto result=CefDictionaryValue::Create();result->SetString("actual",value);return result;}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));std::vector<std::pair<std::string,std::string>>seen;std::vector<std::string>missing,failures;
 Extras extras([&](const auto&id,Dict headers){seen.emplace_back(id,headers->GetString("actual").ToString());},[&](const auto&id,const auto&){missing.push_back(id);},[&](const auto&error){failures.push_back(error);});
 // The actual first receipt may precede its redirect boundary.
 assert(extras.Receive("\nreal-id",Headers("first"),100));assert(seen.empty());assert(extras.Expect("\nreal-id","one",true,100));assert((seen==std::vector<std::pair<std::string,std::string>>({{"one","first"}})));
 assert(extras.Expect("\nreal-id","two",false,100));assert(extras.Expect("\nreal-id","final",false,100));extras.Complete("\nreal-id");assert(extras.bytes()==0&&extras.hops()==0&&extras.receipts()==0);
 // Its equally genuine late arrival belongs to the earlier explicitly marked
 // hop, even after the next response and final completion have been observed.
 assert(extras.Expect("child\nsame", "one",true,100));assert(extras.Expect("child\nsame","two",false,100));assert(extras.Expect("child\nsame","three",true,100));extras.Complete("child\nsame");assert(extras.Move("child\nsame","parent\nsame"));assert(extras.Receive("parent\nsame",Headers("late-one"),100));assert(seen.back()==std::make_pair(std::string("one"),std::string("late-one")));assert(extras.Receive("parent\nsame",Headers("late-three"),100));assert(seen.back()==std::make_pair(std::string("three"),std::string("late-three")));assert(extras.bytes()==0);
 assert(extras.Expect("closing\nid","closed",true,100));extras.SessionClosed("closing");assert(missing==std::vector<std::string>({"closed"})&&extras.bytes()==0);
 assert(extras.Expect("\nexpired","expired",true,100));extras.Tick(100);assert(missing.back()=="expired"&&extras.bytes()==0);
 // A genuine migration can precede the response's expected-hop flag. The
 // original observer can publish its receipt after the new renderer finished.
 assert(extras.Move("\npre-response","child\npre-response",100));assert(extras.aliases()==1);assert(extras.Expect("child\npre-response","moved",true,100));extras.Complete("child\npre-response");assert(extras.Receive("\npre-response",Headers("after-finish"),100));assert(seen.back()==std::make_pair(std::string("moved"),std::string("after-finish")));assert(extras.bytes()==0&&extras.aliases()==0);
 // A later genuine merge can return to a prior observer without alias loops.
 assert(extras.Move("\nback","child\nback",100));assert(extras.Expect("child\nback","back",true,100));assert(extras.Move("child\nback","\nback",200));extras.Complete("\nback");assert(extras.Receive("child\nback",Headers("back-wire"),100));assert(seen.back()==std::make_pair(std::string("back"),std::string("back-wire"))&&extras.bytes()==0);
 // An unrelated migration cannot steal an existing original-observer alias
 // or join a different still-live canonical destination before its response.
 assert(extras.Move("\nconflict","owner\nconflict",100));const auto conflict_bytes=extras.bytes();
 assert(!extras.Move("unrelated\nconflict","\nconflict",100));assert(extras.bytes()==conflict_bytes&&extras.aliases()==1);
 assert(!extras.Move("unrelated\nconflict","owner\nconflict",100));assert(extras.bytes()==conflict_bytes&&extras.aliases()==1);
 extras.Complete("owner\nconflict");assert(extras.bytes()==0);failures.clear();
 assert(extras.Move("\nno-extra","child\nno-extra",100));extras.Complete("child\nno-extra");assert(extras.bytes()==0);
 assert(extras.Move("\nclose-alias","child\nclose-alias",100));extras.SessionClosed("child");assert(extras.bytes()==0);
 assert(extras.Move("\nexpired-alias","child\nexpired-alias",100));extras.Tick(100);assert(extras.bytes()==0);
 // Capacity is reserved atomically even when a migration precedes all headers.
 for(int i=0;i<4096;++i){assert(extras.Move("old\n"+std::to_string(i),"new\n"+std::to_string(i),100));}
 const auto aliases_before=extras.bytes();assert(!extras.Move("old\nover","new\nover",100));assert(extras.aliases()==4096&&extras.bytes()==aliases_before);extras.Tick(100);assert(extras.bytes()==0&&extras.aliases()==0);failures.clear();
 // Atomic retained-byte admission includes newly inserted physical key bytes.
 auto large=Headers(std::string(1048563,'a'));for(int i=0;i<7;++i)assert(extras.Receive("\nlarge"+std::to_string(i),large,100));const auto retained=extras.bytes();assert(!extras.Receive("\nlarge8",large,100));assert(extras.bytes()==retained&&failures.size()==1);extras.Clear();assert(extras.bytes()==0);
 // A callback may clear its native owner while several receipts are pending.
 Extras* owner=nullptr;int applied=0;Extras reentrant([&](const auto&,Dict){++applied;owner->Clear();},[](const auto&,const auto&){assert(false);},[](const auto&){assert(false);});owner=&reentrant;assert(reentrant.Receive("\nreentrant",Headers("one"),100));assert(reentrant.Receive("\nreentrant",Headers("two"),100));assert(reentrant.Expect("\nreentrant","one",true,100));assert(applied==1&&reentrant.bytes()==0);
}
