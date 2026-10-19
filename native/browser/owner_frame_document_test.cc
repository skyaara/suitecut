// SPDX-License-Identifier: MIT
#include "owner_frame_document.h"
#include <cassert>
#include <iostream>
#include <vector>
using Read=suitecut::OwnerFrameDocument;
struct Harness{
 bool origin=true,peer=true;int fatal=0,done=0;Read::Result result;std::vector<Read::Method>sent;std::function<void(Read::Reply)>pending;std::shared_ptr<Read>read;
 Harness(){read=std::make_shared<Read>([this](Read::Method method,const std::string&,auto callback){assert(!pending);sent.push_back(method);pending=std::move(callback);},[this]{return origin;},[this]{return peer;},[this](Read::Result value){++done;result=std::move(value);},[this](const std::string&){++fatal;},"actual-isolate",30000);}
 void Ack(Read::Reply value){auto reply=std::move(pending);pending={};assert(reply);reply(std::move(value));}
 void Isolate(){read->Start();Ack({true,"actual-isolate",{},0});assert(sent.back()==Read::Method::Document);}
 void Document(){Isolate();Ack({true,{},"genuine-document",0});assert(sent.back()==Read::Method::Describe);}
};
int main(){
 {Harness h;h.Document();h.Ack({true,{},{},41});assert(h.read->GroupOwned()&&!h.done&&h.sent.back()==Read::Method::Release);h.Ack({true,{},{},0});assert(h.done==1&&h.result.ok&&h.result.backend==41&&!h.read->GroupOwned());}
 {Harness h;h.origin=false;h.read->Start();assert(h.done==1&&!h.result.ok&&h.sent.empty());}
 {Harness h;h.read->Start();h.Ack({true,"different-real-isolate",{},0});assert(h.done==1&&h.result.ok&&h.result.backend==0&&h.sent.size()==1);}
 {Harness h;h.Isolate();h.origin=false;h.Ack({true,{},"late-document",0});assert(!h.done&&h.read->GroupOwned()&&h.sent.back()==Read::Method::Release);h.Ack({true,{},{},0});assert(h.done==1&&!h.result.ok);}
 {Harness h;h.Document();h.origin=false;h.Ack({true,{},{},41});assert(!h.done&&h.sent.back()==Read::Method::Release);h.Ack({true,{},{},0});assert(h.done==1&&!h.result.ok);}
 {Harness h;h.Isolate();h.Ack({false,{},{},0});assert(h.sent.back()==Read::Method::Release&&!h.done);h.Ack({true,{},{},0});assert(h.done==1&&!h.result.ok);}
 {Harness h;h.Document();h.Ack({true,{},{},41});h.Ack({false,{},{},0});assert(h.fatal==1&&!h.done&&h.read->GroupOwned());h.read->ChannelClosed();assert(h.done==1&&!h.result.ok&&!h.read->GroupOwned());}
 {Harness h;h.Isolate();h.read->Tick(30000);assert(h.fatal==1&&!h.done&&h.read->GroupOwned());h.read->Tick(40000);assert(h.fatal==1);h.read->ChannelClosed();assert(h.done==1&&!h.result.ok);h.Ack({true,{},"late",0});assert(h.done==1);}
 {Harness h;h.Document();h.peer=false;h.Ack({true,{},{},41});assert(h.sent.back()==Read::Method::Release&&!h.done);h.read->ChannelClosed();assert(h.done==1&&!h.result.ok);h.Ack({true,{},{},0});assert(h.done==1);}
 {Harness h;h.Document();h.Ack({true,{},{},41});h.peer=false;h.Ack({true,{},{},0});assert(h.done==1&&!h.result.ok);}
 {Harness h;h.read->Start();h.Ack({false,{},{},0,"Actual native protocol error"});assert(h.done==1&&!h.result.ok&&h.result.error=="Actual native protocol error"&&!h.read->GroupOwned());}
 {Harness h;h.Isolate();h.Ack({false,{},{},0,std::string(4097,'x')});assert(!h.done&&h.read->GroupOwned());h.Ack({true,{},{},0});assert(h.done==1&&!h.result.ok&&h.result.error=="Native owner document protocol diagnostic limit exceeded");}
 std::cout<<"Native owner-document transaction: 12 physical ACK/cancellation/group ownership groups passed\n";
}
