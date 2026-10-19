// SPDX-License-Identifier: MIT
#include "owner_frame_origin.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <vector>
using Origin=suitecut::OwnerFrameOrigin;
struct Harness{
 bool current=true;std::vector<Origin::Method>calls;std::deque<std::function<void(Origin::Reply)>>pending;
 std::vector<Origin::Result>results;std::shared_ptr<Origin>origin;
 explicit Harness(int backend=42,int type=1){origin=std::make_shared<Origin>([this](auto method,const std::string&,auto reply){calls.push_back(method);pending.push_back(std::move(reply));},[this]{return current;},[this](auto result){results.push_back(std::move(result));},backend,type);origin->Start();}
 void Ack(Origin::Reply reply){assert(!pending.empty());auto next=std::move(pending.front());pending.pop_front();next(std::move(reply));}
 void Utility(){Origin::Reply reply;reply.ok=true;reply.node=true;reply.object="actual-utility-document";Ack(reply);}
 void Document(int backend=7,int type=9){Origin::Reply reply;reply.ok=true;reply.backend=backend;reply.type=type;Ack(reply);}
 void Before(){Utility();Document();assert(calls.back()==Origin::Method::Resolve);}
 void Missing(){Origin::Reply reply;reply.code=-32000;reply.error="Node with given id does not belong to the document";Ack(reply);}
 void Retained(int backend=42,int type=1){Origin::Reply reply;reply.ok=true;reply.backend=backend;reply.type=type;Ack(reply);}
 void Failed(){assert(results.size()==1&&!results[0].ok&&!results[0].no_owner);}
};
int main(){
 {Harness h;h.Before();Origin::Reply reply;reply.ok=true;reply.node=true;reply.object="actual-resolved-node";h.Ack(reply);assert(h.results.size()==1&&h.results[0].ok&&!h.results[0].no_owner&&h.results[0].object=="actual-resolved-node");assert(h.calls.size()==3);}
 {Harness h;h.Before();h.Missing();h.Utility();h.Document();h.Retained();assert(h.results.size()==1&&h.results[0].ok&&h.results[0].no_owner&&h.results[0].object.empty());assert(h.calls.size()==6);}
 {for(const auto code:{-32001,0}){Harness h;h.Before();Origin::Reply reply;reply.code=code;reply.error="Node with given id does not belong to the document";h.Ack(reply);h.Failed();assert(h.calls.size()==3);}Harness h;h.Before();Origin::Reply reply;reply.code=-32000;reply.error="Cannot find context with specified id";h.Ack(reply);h.Failed();assert(h.calls.size()==3);}
 {Harness h;h.Before();h.Missing();Origin::Reply reply;reply.code=-32000;reply.error="Cannot find context with specified unique id";h.Ack(reply);h.Failed();assert(h.results[0].error==reply.error&&h.calls.size()==4);}
 {Harness h;h.Before();h.Missing();h.Utility();h.Document(8);h.Failed();assert(h.calls.size()==5);}
 {Harness h;h.Before();h.Missing();h.Utility();h.Document();h.Retained(43);h.Failed();Harness other;other.Before();other.Missing();other.Utility();other.Document();other.Retained(42,3);other.Failed();}
 {Harness h;h.Utility();h.Document(7,1);h.Failed();assert(h.calls.size()==2);Harness wrapper;Origin::Reply reply;reply.ok=true;reply.object="author-object";wrapper.Ack(reply);wrapper.Failed();}
 {Harness h;h.current=false;h.Utility();h.Failed();assert(h.calls.size()==1);Harness after;after.Before();after.Missing();after.current=false;after.Utility();after.Failed();assert(after.calls.size()==4);}
 {Harness h;auto first=h.pending.front();h.Utility();Origin::Reply replay;replay.ok=true;replay.node=true;replay.object="stale-native-document";first(replay);assert(h.calls.size()==2&&h.pending.size()==1);h.Document();h.Missing();h.Utility();h.Document();h.Retained();assert(h.results.size()==1&&h.results[0].no_owner);first(replay);assert(h.results.size()==1);}
 {Harness h;h.origin->Start();assert(h.calls.size()==1);Origin::Reply reply;reply.error=std::string(4097,'x');h.Ack(reply);h.Failed();assert(h.results[0].error=="Native element owner protocol diagnostic limit exceeded");}
 {Harness h(0,1);h.Failed();assert(h.calls.empty());Harness other(42,13);other.Failed();assert(other.calls.empty());}
 std::cout<<"PASS11 owner-frame origin groups: real wrapper resolution, source-attested exact error only after before/after Document+context and retained-node validation, invalid context/changed document/node/cancellation/stale ACK rejection, diagnostics and schema bounds\n";
}
