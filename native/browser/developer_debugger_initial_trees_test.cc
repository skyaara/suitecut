// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "developer_debugger_initial_trees.h"
#include "developer_debugger_documents.h"
using Trees=suitecut::FrameDebuggerInitialTrees;
using Documents=suitecut::FrameDebuggerDocuments;
int main(){size_t groups=0;std::string error;
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.DefaultObserved("",1,"root",31,error));assert(!trees.InitialDocument(10,"",1,"root"));
  assert(trees.Dispatching(10,"",1)&&trees.InitialDocument(10,"",1,"root")==31);assert(trees.Finished(10,"",1)&&!trees.Trees()&&!trees.Frames()&&!trees.Payload());++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.Dispatching(10,"",1));assert(trees.DefaultObserved("",1,"root",31,error));
  assert(trees.InitialDocument(10,"",1,"root")==31&&trees.DefaultObserved("",1,"root",31,error)&&trees.InitialDocument(10,"",1,"root")==31);++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.DefaultObserved("",1,"root",31,error));assert(trees.FrameInvalidated("",1,"root",error));
  assert(trees.DefaultObserved("",1,"root",32,error)&&trees.Dispatching(10,"",1)&&trees.InitialDocument(10,"",1,"root")==32);++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.DefaultObserved("",1,"root",31,error)&&trees.Dispatching(10,"",1));
  assert(trees.FrameInvalidated("",1,"root",error)&&trees.DefaultObserved("",1,"root",32,error)&&!trees.InitialDocument(10,"",1,"root"));++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.Dispatching(10,"",1));
  assert(trees.FrameInvalidated("",1,"root",error)&&trees.DefaultObserved("",1,"root",31,error)&&!trees.InitialDocument(10,"",1,"root"));++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.DefaultObserved("",1,"root",31,error)&&trees.Dispatching(10,"",1));
  assert(trees.DefaultObserved("",1,"root",32,error)&&!trees.InitialDocument(10,"",1,"root"));++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.DefaultObserved("",1,"root",31,error));trees.ContextsCleared("",1);assert(!trees.Frames());
  assert(trees.DefaultObserved("",1,"root",32,error)&&trees.Dispatching(10,"",1)&&trees.InitialDocument(10,"",1,"root")==32);trees.ContextsCleared("",1);
  assert(trees.DefaultObserved("",1,"new-frame",33,error)&&!trees.InitialDocument(10,"",1,"root")&&!trees.InitialDocument(10,"",1,"new-frame"));++groups;}
 {Trees trees;assert(trees.Prepare(10,"actual-child",7,error)&&trees.DefaultObserved("actual-child",7,"frame",31,error)&&trees.Dispatching(10,"actual-child",7));
  assert(!trees.InitialDocument(10,"actual-child",8,"frame")&&!trees.InitialDocument(10,"",7,"frame")&&!trees.InitialDocument(11,"actual-child",7,"frame"));
  assert(!trees.Detached("actual-child",8)&&trees.InitialDocument(10,"actual-child",7,"frame")==31);assert(trees.Detached("actual-child",7)&&!trees.Trees());
  assert(trees.Prepare(11,"actual-child",8,error)&&trees.DefaultObserved("actual-child",8,"frame",32,error)&&trees.Dispatching(11,"actual-child",8));assert(!trees.Finished(10,"actual-child",7)&&trees.InitialDocument(11,"actual-child",8,"frame")==32);++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.Prepare(11,"other-channel",2,error));assert(trees.DefaultObserved("",1,"root",31,error)&&trees.DefaultObserved("other-channel",2,"child",32,error));
  assert(trees.Dispatching(10,"",1)&&trees.Dispatching(11,"other-channel",2));trees.ContextsCleared("",1);assert(!trees.InitialDocument(10,"",1,"root")&&trees.InitialDocument(11,"other-channel",2,"child")==32);++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.DefaultObserved("",1,"one",31,error)&&trees.Dispatching(10,"",1));const auto before=trees.Payload();
  for(int i=0;i<1100;++i)assert(trees.FrameInvalidated("",1,"new-before-default-"+std::to_string(i),error));assert(trees.FrameInvalidated("",1,std::string(4096,'f'),error));
  assert(trees.Available()&&trees.Frames()==1102&&trees.Payload()>before&&trees.InitialDocument(10,"",1,"one")==31);trees.Closed();assert(!trees.Trees()&&!trees.Frames()&&!trees.Payload());++groups;}
 {Trees trees;for(int i=0;i<1100;++i)assert(trees.Prepare(10+i,"channel-"+std::to_string(i),2,error));assert(trees.Prepare(2000,std::string(4096,'c'),2,error)&&trees.Available()&&trees.Trees()==1101);++groups;}
 {Trees trees;assert(trees.Prepare(10,"",1,error)&&trees.Prepare(10,"",1,error)&&trees.Trees()==1);assert(trees.Finished(10,"",1));assert(!trees.Prepare(10,"",1,error)&&!trees.Available()&&!trees.Trees());++groups;}
 {Documents docs({"source",1});Trees trees;Documents::Snapshot initial{"","root","","actual-unique",7};assert(trees.Prepare(10,"",1,error)&&docs.DefaultCreated(initial,error));const auto observed=docs.DocumentStamp("root");assert(observed&&trees.DefaultObserved("",1,"root",observed,error)&&trees.Dispatching(10,"",1));
  docs.FrameInvalidated("root");assert(trees.FrameInvalidated("",1,"root",error));assert(docs.DefaultCreated(initial,error)&&trees.DefaultObserved("",1,"root",docs.DocumentStamp("root"),error));
  initial.loader="real-later-loader";const auto token=trees.InitialDocument(10,"",1,"root");assert(!token&&!docs.InitialFrameAssociated(initial,observed,error));suitecut::FrameDebuggerScopes::Pin current;assert(docs.Resolve("root",current)&&current.loader.empty());++groups;}
 std::cout<<"native initial frame-tree association "<<groups<<" groups passed\n";
}
