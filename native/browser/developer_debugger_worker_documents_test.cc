// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "developer_debugger_worker_documents.h"
using Documents=suitecut::DeveloperDebuggerWorkerDocuments;
static const Documents::Host host{"controlled-real-Source",9};
static Documents::Attachment Attached(uint64_t generation=1,const std::string&session="worker-session",const std::string&target="worker-target"){
  return {host,target,session,"actual-parent-session",generation};
}
int main(){size_t groups=0;std::string error;
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin;
  assert(docs.Attached(attachment,"worker",error)&&!docs.Resolve(attachment,pin));
  assert(docs.ContextCreated(attachment,7,"actual-runtime-unique",error)&&docs.Resolve(attachment,pin)&&docs.Current(pin));
  assert(pin.attachment==attachment&&pin.context==7&&pin.unique_context=="actual-runtime-unique"&&pin.observation);++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin;assert(docs.Attached(attachment,"worker",error));
  assert(!docs.ContextCreated(attachment,7,"",error)&&!docs.Resolve(attachment,pin));
  assert(!docs.ContextCreated(attachment,0,"actual-runtime-unique",error)&&!docs.Resolve(attachment,pin));++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin;assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"actual-runtime-unique",error)&&docs.Resolve(attachment,pin));
  const auto payload=docs.Payload();assert(docs.ContextCreated(attachment,7,"actual-runtime-unique",error));Documents::Pin duplicate;
  assert(docs.Resolve(attachment,duplicate)&&duplicate==pin&&docs.Payload()==payload);++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin,replacement;assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"old-unique",error)&&docs.Resolve(attachment,pin));
  assert(docs.ContextCreated(attachment,7,"new-unique",error)&&!docs.Current(pin)&&docs.Resolve(attachment,replacement)&&docs.Current(replacement)&&replacement.observation!=pin.observation);++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin,replacement;assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"same-real-unique",error)&&docs.Resolve(attachment,pin));
  assert(!docs.ContextDestroyed(attachment,8)&&docs.Current(pin));assert(docs.ContextDestroyed(attachment,7)&&!docs.Current(pin)&&!docs.Resolve(attachment,replacement));
  assert(docs.ContextCreated(attachment,7,"same-real-unique",error)&&docs.Resolve(attachment,replacement)&&replacement.observation!=pin.observation&&!docs.Current(pin));++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin;assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"actual-runtime-unique",error)&&docs.Resolve(attachment,pin));
  auto wrong=attachment;wrong.host.source="foreign-source";assert(!docs.ContextsCleared(wrong)&&!docs.Detached(wrong)&&docs.Current(pin));
  wrong=attachment;wrong.host.observer++;assert(!docs.ContextDestroyed(wrong,7)&&!docs.ContextCreated(wrong,7,"foreign-realm",error)&&docs.Current(pin));++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin;assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"actual-runtime-unique",error)&&docs.Resolve(attachment,pin));
  auto wrong=attachment;wrong.generation++;assert(!docs.Detached(wrong)&&!docs.ContextsCleared(wrong)&&docs.Current(pin));
  wrong=attachment;wrong.target="different-target";assert(!docs.ContextCreated(wrong,7,"other-unique",error)&&docs.Current(pin));++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin,replacement;assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"actual-runtime-unique",error)&&docs.Resolve(attachment,pin));
  assert(docs.Detached(attachment)&&!docs.Current(pin)&&docs.Payload()==0);auto next=attachment;next.generation++;
  assert(docs.Attached(next,"worker",error)&&docs.ContextCreated(next,7,"actual-runtime-unique",error)&&docs.Resolve(next,replacement)&&docs.Current(replacement));
  assert(!docs.Detached(attachment)&&!docs.ContextDestroyed(attachment,7)&&!docs.ContextCreated(attachment,7,"late-old-reply",error)&&docs.Current(replacement));++groups;}
 {Documents docs(host);const auto attachment=Attached();assert(docs.Attached(attachment,"worker",error));
  assert(!docs.Attached(attachment,"worker",error));assert(!docs.Attached(Attached(2,"other-session"),"worker",error));
  assert(!docs.Attached(Attached(2,"other-session","other-target"),"service_worker",error)&&docs.Records()==1);++groups;}
 {Documents docs(host);Documents::Pin first;for(size_t i=0;i<Documents::kRecords;++i){const auto attachment=Attached(i+1,"session-"+std::to_string(i),"target-"+std::to_string(i));assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"unique-"+std::to_string(i),error));if(!i)assert(docs.Resolve(attachment,first));}
  const auto bytes=docs.Payload();assert(!docs.Attached(Attached(1000,"over-capacity","new-target"),"worker",error)&&docs.Records()==Documents::kRecords&&docs.Payload()==bytes&&docs.Current(first));
  assert(docs.Detached(first.attachment)&&docs.Attached(Attached(1000,"over-capacity","new-target"),"worker",error));++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin;assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"actual-runtime-unique",error)&&docs.Resolve(attachment,pin));
  const auto payload=docs.Payload();assert(!docs.ContextCreated(attachment,7,std::string(257,'x'),error)&&docs.Current(pin)&&docs.Payload()==payload);
  assert(!docs.ContextCreated(attachment,7,std::string("a\0b",3),error)&&docs.Current(pin));++groups;}
 {Documents docs(host);const auto attachment=Attached();Documents::Pin pin;assert(docs.Attached(attachment,"worker",error)&&docs.ContextCreated(attachment,7,"actual-runtime-unique",error)&&docs.Resolve(attachment,pin));
  docs.ObserverClosed({"foreign-source",host.observer});assert(docs.Current(pin));docs.ObserverClosed(host);
  assert(!docs.Available()&&!docs.Current(pin)&&!docs.Records()&&!docs.Payload()&&!docs.Attached(Attached(2),"worker",error));++groups;}
 {Documents docs({"",1});assert(!docs.Available()&&!docs.Attached(Attached(),"worker",error));Documents replacement({host.source,host.observer+1});assert(!replacement.Attached(Attached(),"worker",error));++groups;}
 std::cout<<"native worker debugger observed-document metadata "<<groups<<" groups passed\n";
}
