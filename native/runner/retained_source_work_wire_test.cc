// SPDX-License-Identifier: MIT
#include "retained_source_work_wire.h"
#include <cassert>
#include <iostream>
using Wire=suitecut::runner::RetainedSourceWorkWire;
static void Rejected(auto action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){size_t groups=0;
 {const auto value=Wire::Issue(7,3,9,11,1000.125);const auto decoded=Wire::Decode(Wire::Encode(value));assert(decoded.kind==Wire::Kind::Begin&&decoded.challenge==value);++groups;}
 {const Wire::Cuts value{7,3,{2,0,4},{1,UINT64_MAX,8}};for(const auto kind:{Wire::Kind::Cut,Wire::Kind::Observed,Wire::Kind::Acknowledged}){const auto decoded=Wire::Decode(Wire::Encode(kind,value));assert(decoded.kind==kind&&decoded.cuts==value);}++groups;}
 {const Wire::Resume value{7,3,12};auto a=Wire::Decode(Wire::Encode(value)),b=Wire::Decode(Wire::Resumed(value));assert(a.kind==Wire::Kind::Resume&&b.kind==Wire::Kind::Resumed&&a.resume.generation==12&&b.resume.ticket==7);++groups;}
 {const auto decoded=Wire::Decode(Wire::Install(7));assert(decoded.kind==Wire::Kind::Install&&decoded.ticket==7);const auto cap=Wire::Decode(Wire::Encode(Wire::Capability{7,1234,5678}));assert(cap.kind==Wire::Kind::Capability&&cap.capability.pid==1234&&cap.capability.start==5678);++groups;}
 {const auto value=Wire::Decode(Wire::Unsupported(7,3,256));assert(value.kind==Wire::Kind::Unsupported&&value.ticket==7&&value.nonce==3&&value.reason==256);++groups;}
 {for(const auto packet:{"SOURCE_WORK_INSTALL 1 0\n","SOURCE_WORK_INSTALL 1 07\n","SOURCE_WORK_INSTALL 2 7\n","SOURCE_WORK_INSTALL 1 7","SOURCE_WORK_INSTALL 1 7 \n","SOURCE_WORK_INSTALL  1 7\n","SOURCE_WORK_INSTALL 1 7\r\n","SOURCE_WORK_BEGIN 1 7 3 9 11 nan\n","SOURCE_WORK_BEGIN 1 7 3 9 11 0\n","SOURCE_WORK_BEGIN 1 7 3 9 11 01000\n","SOURCE_WORK_BEGIN 1 7 3 9 11 1e3\n","SOURCE_WORK_CAP 1 7 2147483648 1\n","SOURCE_WORK_CUT 1 7 3 2 0 0 1 17 8\n","SOURCE_WORK_ACK 1 7 3 2 13 4 1 18446744073709551616 8\n","SOURCE_WORK_UNKNOWN 1 7\n"})Rejected([&]{Wire::Decode(packet);});++groups;}
 {auto value=Wire::Issue(7,3,9,11,1000.125);value.deadline_text="1000.1250";Rejected([&]{Wire::Encode(value);});for(const double deadline:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})Rejected([&]{Wire::Issue(7,3,9,11,deadline);});++groups;}
 {const Wire::Cuts value{7,3,{2,13,4},{1,17,8}};Rejected([&]{Wire::Encode(Wire::Kind::Begin,value);});auto invalid=value;invalid.reader.instance=0;Rejected([&]{Wire::Encode(Wire::Kind::Cut,invalid);});++groups;}
 assert(groups==8);std::cout<<"Native retained Source wire: "<<groups<<" canonical original-deadline/cut/incarnation controls passed (codec only; no browser receipt claim)\n";
}
