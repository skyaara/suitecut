// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <set>
#include "developer_debugger_documents.h"
using Documents=suitecut::FrameDebuggerDocuments;
using Scopes=suitecut::FrameDebuggerScopes;
Documents::Snapshot Default(std::string frame="root",std::string session="",int context=7,std::string unique="actual-default"){
  return {std::move(session),std::move(frame),"actual-loader",std::move(unique),context};
}
int main(){size_t groups=0;std::string error;
 {Documents docs({"actual-source",1});assert(docs.MainFrame("root",error)&&docs.DefaultCreated(Default(),error));Scopes::Pin root;
  assert(docs.Resolve("root",root)&&root.root&&root.channel.session.empty()&&root.attachment==1&&docs.Current(root));
  const auto count=docs.Documents(),bytes=docs.Payload();assert(docs.DefaultCreated(Default(),error)&&docs.Documents()==count&&docs.Payload()==bytes&&docs.Current(root));
  // Utility creation and same-document navigation intentionally have no ledger
  // mutation. They cannot change this captured default identity.
  assert(docs.MainFrame("root",error)&&docs.Current(root));++groups;}
 {Documents docs({"actual-source",1});assert(docs.DefaultCreated(Default(),error));Scopes::Pin before,after;assert(docs.Resolve("root",before));
  docs.FrameInvalidated("root");assert(!docs.Resolve("root",after)&&!docs.Current(before));
  // A genuinely restored Source tuple still receives a new native document.
  assert(docs.DefaultCreated(Default(),error)&&docs.Resolve("root",after)&&after.document>before.document&&!docs.Current(before));++groups;}
 {Documents docs({"actual-source",1});assert(docs.DefaultCreated(Default(),error));Scopes::Pin before,after;assert(docs.Resolve("root",before));
  auto replacement=Default();replacement.context=8;replacement.unique_context="actual-replacement";
  assert(docs.DefaultCreated(replacement,error)&&docs.Resolve("root",after)&&after.document>before.document&&!docs.Current(before));
  docs.ContextDestroyed("",7,"actual-default");assert(docs.Current(after));docs.ContextDestroyed("",8,"wrong-unique");assert(docs.Current(after));
  docs.ContextDestroyed("other-session",8,"actual-replacement");assert(docs.Current(after));docs.ContextDestroyed("",8,"actual-replacement");assert(!docs.Current(after));++groups;}
 {Documents docs({"actual-source",19});assert(docs.MainFrame("root",error)&&docs.DefaultCreated(Default(),error)&&docs.DefaultCreated(Default("same-process-child","",8,"child-default"),error));Scopes::Pin root,child;
  assert(docs.Resolve("root",root)&&docs.Resolve("same-process-child",child)&&root.channel==child.channel&&root.attachment==child.attachment&&root.root&&!child.root&&root.context!=child.context);
  docs.FrameInvalidated("same-process-child");assert(docs.Current(root)&&!docs.Current(child));++groups;}
 {Documents docs({"actual-source",1});assert(docs.Attached("actual-oopif","","remote-root",error));const auto attachment=docs.AttachmentFor("actual-oopif");assert(attachment&&docs.Attached("actual-oopif","","remote-root",error)&&docs.AttachmentFor("actual-oopif")==attachment);
  assert(docs.DefaultCreated(Default("remote-root","actual-oopif",11,"remote-context"),error)&&docs.DefaultCreated(Default("remote-local-child","actual-oopif",12,"remote-child-context"),error));Scopes::Pin remote,local;
  assert(docs.Resolve("remote-root",remote)&&docs.Resolve("remote-local-child",local)&&remote.channel==local.channel&&remote.attachment==local.attachment);
  assert(!docs.Detached("actual-oopif",attachment+1)&&docs.Current(remote));assert(docs.Detached("actual-oopif",attachment)&&!docs.Current(remote)&&!docs.Current(local));
  assert(docs.Attached("actual-oopif","","remote-root",error)&&docs.AttachmentFor("actual-oopif")>attachment&&docs.DefaultCreated(Default("remote-root","actual-oopif",11,"remote-context"),error));Scopes::Pin newer;assert(docs.Resolve("remote-root",newer)&&newer.document>remote.document&&!docs.Detached("actual-oopif",attachment)&&docs.Current(newer));++groups;}
 {Documents docs({"actual-source",1});assert(docs.Attached("parent","","remote-root",error)&&docs.Attached("descendant","parent","nested-root",error));assert(docs.DefaultCreated(Default("nested-root","descendant",11,"nested-context"),error));Scopes::Pin child;assert(docs.Resolve("nested-root",child));
  const auto parent=docs.AttachmentFor("parent"),nested=docs.AttachmentFor("descendant");assert(docs.Detached("parent",parent)&&!docs.Current(child)&&!docs.AttachmentFor("descendant"));
  assert(docs.Attached("parent","","remote-root",error)&&docs.AttachmentFor("parent")!=parent&&!docs.Current(child)&&!docs.AttachmentFor("descendant"));
  assert(docs.Detached("descendant",nested)&&docs.Attachments()==1&&docs.Documents()==0);++groups;}
 {Documents docs({"actual-source",1});assert(docs.Attached("remote","","remote-root",error)&&docs.DefaultCreated(Default(),error)&&docs.DefaultCreated(Default("remote-root","remote",7,"remote-default"),error));Scopes::Pin root,remote;assert(docs.Resolve("root",root)&&docs.Resolve("remote-root",remote));
  docs.ContextsCleared("");assert(!docs.Current(root)&&docs.Current(remote));docs.ContextsCleared("remote");assert(!docs.Current(remote)&&docs.Attachments()==1);++groups;}
 {Documents docs({"actual-source",1},{2,2,4096});assert(docs.DefaultCreated(Default("one"),error)&&docs.DefaultCreated(Default("two"),error));Scopes::Pin before;assert(docs.Resolve("one",before));const auto bytes=docs.Payload();
  assert(!docs.DefaultCreated(Default("overflow"),error)&&!docs.Available()&&!docs.Current(before)&&docs.Documents()==2&&docs.Payload()==bytes);++groups;}
 {Documents docs({"actual-source",1},{2,1,4096});assert(docs.Attached("first","","first-frame",error));const auto bytes=docs.Payload();assert(!docs.Attached("second","","second-frame",error)&&docs.Attachments()==1&&docs.Payload()==bytes&&!docs.Available());++groups;}
 {Documents docs({"actual-source",1},{2,2,128});const auto before=docs.Payload();assert(!docs.DefaultCreated(Default(std::string(256,'f'),"",7,std::string(256,'c')),error)&&docs.Documents()==0&&docs.Payload()==before&&!docs.Available());++groups;}
 {Documents docs({"actual-source",1},{2,2,4096,1});assert(docs.DefaultCreated(Default(),error));docs.FrameInvalidated("root");assert(!docs.DefaultCreated(Default(),error)&&!docs.Documents()&&!docs.Available());
  Documents attached({"actual-source",1},{2,2,4096,1});assert(attached.Attached("actual","","frame",error));assert(attached.Detached("actual",attached.AttachmentFor("actual")));assert(!attached.Attached("actual","","frame",error)&&!attached.Attachments());++groups;}
 {Documents docs({"actual-source",1});assert(docs.DefaultCreated(Default(),error));Scopes::Pin before;assert(docs.Resolve("root",before));auto invalid=Default();invalid.unique_context.clear();const auto bytes=docs.Payload();assert(!docs.DefaultCreated(invalid,error)&&!docs.Available()&&docs.Payload()==bytes&&!docs.Current(before));++groups;}
 {Documents docs({"actual-source",1});assert(docs.Attached("actual","","frame",error));const auto before=docs.Payload();assert(!docs.Attached("actual","","different-frame",error)&&!docs.Available()&&docs.Payload()==before);Documents missing({"actual-source",1});assert(!missing.Attached("child","unobserved-parent","frame",error)&&!missing.Attachments()&&!missing.Available());++groups;}
 {Documents docs({"actual-source",1});assert(docs.DefaultCreated(Default(),error));Scopes::Pin pin;assert(docs.Resolve("root",pin));pin.channel.host.observer=2;assert(!docs.Current(pin));docs.Closed();assert(!docs.Available()&&!docs.Documents()&&!docs.Attachments()&&!docs.Resolve("root",pin));++groups;}
 {Documents docs({"actual-source",1});assert(docs.MainFrame("root",error)&&docs.DefaultCreated(Default(),error)&&docs.DefaultCreated(Default("child","",8,"child-context"),error));Scopes::Pin root,child;assert(docs.Resolve("root",root)&&docs.Resolve("child",child));std::set<uint64_t>leases;size_t cleanup=0;double now=1;
  Scopes::Hooks hooks;hooks.now=[&]{return now;};hooks.current=[&](const auto&pin){return docs.Current(pin);};hooks.lease_present=[&](uint64_t id){return leases.contains(id);};hooks.cleanup=[&](auto,const auto&){++cleanup;};hooks.close_channel=[](const auto&,const auto&){};hooks.wake=[]{};
  auto budget=std::make_shared<Scopes::Budget>();auto book=Scopes::Create(root.channel.host,std::move(hooks),budget);const auto token=book->Reserve(root,1,100,error);assert(token&&book->BeginAcquire(token));leases.insert(1);assert(book->Ready(token,true));book->AcquireReturned(token);const auto pending=book->Hold(token);assert(pending);
  docs.FrameInvalidated("root");book->Poll(token);assert(cleanup==1&&!book->Current(token));leases.erase(1);book->Poll(token);assert(!book->Reserve(child,2,100,error)&&book->Retained()==1);assert(book->Retire(pending)&&book->Retained()==0);assert(book->Reserve(child,2,100,error));book->HostClosed(root.channel.host);++groups;}
 {Documents docs({"actual-source",1});auto context=Default();context.loader.clear();assert(docs.DefaultCreated(context,error));Scopes::Pin before,associated;assert(docs.Resolve("root",before));
  context.loader="actual-initial-tree-loader";assert(docs.InitialFrameAssociated(context,before.document,error)&&docs.Resolve("root",associated)&&associated.loader==context.loader&&!docs.Current(before));
  context.loader="different-loader-without-new-context";assert(docs.InitialFrameAssociated(context,associated.document,error)&&docs.Available()&&!docs.Current(associated)&&!docs.Documents());
  assert(!docs.InitialFrameAssociated(context,associated.document,error)&&!docs.Documents());++groups;}
 {Documents docs({"actual-source",1});auto context=Default();context.loader.clear();assert(docs.DefaultCreated(context,error));Scopes::Pin old,reused;assert(docs.Resolve("root",old));
  docs.FrameInvalidated("root");assert(docs.DefaultCreated(context,error)&&docs.Resolve("root",reused)&&old.document!=reused.document);
  context.loader="actual-late-frame-tree-loader";const auto bytes=docs.Payload();
  assert(!docs.InitialFrameAssociated(context,old.document,error)&&docs.Available()&&docs.Current(reused)&&docs.Payload()==bytes);
  assert(!docs.InitialFrameAssociated(context,0,error)&&docs.Current(reused));++groups;}
 std::cout<<"native observed frame debugger documents "<<groups<<" groups passed\n";
}
