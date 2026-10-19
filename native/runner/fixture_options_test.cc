// SPDX-License-Identifier: MIT
#include "fixture_options.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
FixtureLiteral J(std::string json){return {FixtureLiteral::Kind::JSON,std::move(json)};}
FixtureDeclaration F(uint64_t id,std::string name,std::vector<std::string> dependencies={},std::optional<Scope> scope=std::nullopt){return {id,std::move(name),std::move(dependencies),scope,std::nullopt};}
FixtureValueDeclaration V(uint64_t id,std::optional<bool> option=std::nullopt,std::optional<FixtureLiteral> literal=std::nullopt){return {id,option,std::move(literal)};}
template<class Fn>void Reject(Fn&& fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){unsigned groups=0;
 {for(const auto* text:{"null","false","true","0","-0","1.25e+3","\"\"","\"π😀\"","\"\\ud800\\u0000\\udfff\""," {\"nested\":[true,null,-1,{}]} \n"})assert(J(text).Valid());++groups;}
 {for(const auto* text:{"","0 0","null,\"other\":1","{","[]x","[1,]","{\"x\":1,}","{\"x\":}","\"\\x\"","\"raw\nline\"","+1","01","-","1.","1e"})assert(!J(text).Valid());++groups;}
 {assert(!J(std::string("\"")+char(0xff)+"\"").Valid());assert(!J(std::string("\"")+char(0xed)+char(0xa0)+char(0x80)+"\"").Valid());++groups;}
 {assert(J(std::string(16,'[')+"0"+std::string(16,']')).Valid());assert(!J(std::string(17,'[')+"0"+std::string(17,']')).Valid());++groups;}
 {std::string json="[0";for(size_t n=1;n<2047;++n)json+=",0";assert(J(json+"]").Valid());assert(!J(json+",0]").Valid());++groups;}
 {assert(J("\""+std::string(8190,'x')+"\"").Valid());assert(!J("\""+std::string(8191,'x')+"\"").Valid());++groups;}
 {for(const auto* number:{"NaN","Infinity","-Infinity","1e9999","-1e9999","1e-9999"})assert(!J(number).Valid());assert(J("1.7976931348623157e308").Valid());++groups;}
 {FixtureLiteral undefined;assert(undefined.Valid()&&undefined!=J("null")&&undefined!=J("\"\""));undefined.json="null";assert(!undefined.Valid());undefined={static_cast<FixtureLiteral::Kind>(99),{}};assert(!undefined.Valid());++groups;}
 {FixtureOptionCatalog c({{F(1,"foo")}});assert(c.Get(1).origin==FixtureOptionCatalog::Origin::Callback&&!c.Get(1).option&&!c.Get(1).constant);assert(c.Graph().Effective("foo").id==1);++groups;}
 {FixtureOptionCatalog c({{F(1,"foo")}}, {V(1,std::nullopt,J("{\"x\":1}"))});assert(c.Get(1).origin==FixtureOptionCatalog::Origin::DeclaredLiteral&&c.Get(1).constant==J("{\"x\":1}"));assert(c.Graph().Definitions().size()==1);++groups;}
 {FixtureOptionCatalog c({{F(1,"foo")},{F(2,"foo",{"foo"})},{F(3,"foo",{"foo"})}}, {V(1,true)});assert(c.Get(1).option&&c.Get(2).option&&c.Get(3).option);assert(c.Graph().Get(3).bindings[0].declaration==2);++groups;}
 {Reject([]{FixtureOptionCatalog c({{F(1,"foo")},{F(2,"foo")}}, {V(1,true),V(2,false)});});++groups;}
 {FixtureOptionCatalog c({{F(1,"foo")},{F(2,"foo")}}, {V(1,false),V(2,true)});assert(!c.Get(1).option&&c.Get(2).option);++groups;}
 {FixtureOptionCatalog c({{F(1,"foo",{"missing"})}}, {V(1,true)},{{"foo",J("17")}});assert(c.Get(1).origin==FixtureOptionCatalog::Origin::ConfiguredLiteral&&c.Get(1).constant==J("17"));assert(c.Graph().Effective("foo").dependencies.empty()&&c.Graph().Effective("foo").bindings.empty());assert(c.RawLayers()[0][0].dependencies==std::vector<std::string>{"missing"}&&c.Get(1).original_dependencies==std::vector<std::string>{"missing"});++groups;}
 {Reject([]{FixtureOptionCatalog c({{F(1,"foo")}}, {},{{"foo",J("0")}});});Reject([]{FixtureOptionCatalog c({{F(1,"foo")}}, {V(1,true)},{{"missing",J("0")}});});Reject([]{FixtureOptionCatalog c({{F(1,"foo")}}, {V(1,true)},{{"foo",J("0")},{"foo",J("1")}});});++groups;}
 {FixtureOptionCatalog c({{F(1,"foo")}}, {V(1,true,J("null"))},{{"foo",FixtureLiteral{}}});assert(c.Get(1).origin==FixtureOptionCatalog::Origin::ConfiguredLiteral&&c.Get(1).constant->kind==FixtureLiteral::Kind::Undefined&&c.Get(1).constant->json.empty());++groups;}
 {FixtureOptionCatalog c({{F(1,"foo")},{F(2,"foo")}}, {V(1,true,J("\"old\""))},{{"foo",J("\"configured\"")}});assert(c.Get(1).origin==FixtureOptionCatalog::Origin::DeclaredLiteral&&c.Get(1).constant==J("\"old\""));assert(c.Get(2).option&&c.Get(2).origin==FixtureOptionCatalog::Origin::ConfiguredLiteral&&!c.Graph().Get(1).active);++groups;}
 {FixtureOptionCatalog c({{F(1,"foo"),F(2,"bar",{"foo"})},{F(3,"foo",{"foo"})}}, {V(1,true)},{{"foo",J("false")}});assert(c.Graph().Get(2).bindings[0].declaration==3&&c.Graph().Get(3).dependencies.empty()&&!c.Graph().Get(1).active&&c.Get(2).origin==FixtureOptionCatalog::Origin::Callback);++groups;}
 {Reject([]{FixtureOptionCatalog c({{F(1,"foo",{"bar"}),F(2,"bar")}}, {V(1,true,J("0"))});});++groups;}
 {Reject([]{FixtureOptionCatalog c({{F(1,"foo")}}, {V(2,true)});});Reject([]{FixtureOptionCatalog c({{F(1,"foo")}}, {V(1,true),V(1,false)});});Reject([]{FixtureOptionCatalog c({{F(2,"foo")}});});++groups;}
 {FixtureOptionCatalog empty(std::vector<FixtureCatalog::Layer>(1));assert(empty.Graph().Definitions().empty());Reject([]{FixtureOptionCatalog c(std::vector<FixtureCatalog::Layer>{});});Reject([]{FixtureOptionCatalog c(std::vector<FixtureCatalog::Layer>(33));});Reject([]{FixtureOptionCatalog c({{}},std::vector<FixtureValueDeclaration>(257));});Reject([]{FixtureOptionCatalog c({{}},{},std::vector<FixtureOptionOverride>(257));});++groups;}
 {std::vector<FixtureDeclaration> fixtures;std::vector<FixtureValueDeclaration> values;for(uint64_t n=1;n<=256;++n){fixtures.push_back(F(n,"value"+std::to_string(n)));values.push_back(V(n,true,J("\""+std::string(8190,'x')+"\"")));}Reject([&]{FixtureOptionCatalog c({fixtures},values);});++groups;}
 {FixtureOptionCatalog c({{F(1,"foo")}}, {V(1,true,J("17"))});std::vector<FixtureOwners::Event> events;bool ready=false,finished=false;FixtureOwners owners(FixtureGraph(c.Graph().Definitions()),{[&](const auto& e){events.push_back(e);},[&](auto,auto){ready=true;},[&](auto,auto,bool ok,const auto&){assert(ok);finished=true;},[](const auto&){assert(false);}});assert(owners.Begin(1,1,c.Graph().Roots({"foo"}),100,0));assert(!ready&&events.size()==1&&owners.State().physical==1);const auto event=events[0];owners.Ready(event.token,event.test,event.generation,1);assert(ready&&!finished&&owners.State().physical==1);owners.Finish(true,"",2);assert(!finished&&events.size()==2&&owners.State().physical==1);const auto teardown=events[1];owners.Settled(teardown.token,teardown.test,teardown.generation,true,"",3);assert(finished&&!owners.State().physical);++groups;}
 {Reject([]{FixtureOptionCatalog c({{F(1,"foo",{},Scope::Worker)},{F(2,"foo",{},Scope::Test)}},{V(1,true)},{{"foo",J("0")}});});Reject([]{FixtureOptionCatalog c({{F(1,"foo",{"bar"},Scope::Worker),F(2,"bar",{},Scope::Test)}});});FixtureOptionCatalog c({{F(1,"foo",{"bar"},Scope::Worker),F(2,"bar",{},Scope::Test)}},{V(1,true)},{{"foo",J("0")}});assert(c.Graph().Get(1).scope==Scope::Worker&&c.Graph().Get(1).bindings.empty());++groups;}
 std::cout<<"PASS "<<groups<<" native literal/option catalog controls (controlled fixture receipts, no OS/host integration claim)\n";
}
