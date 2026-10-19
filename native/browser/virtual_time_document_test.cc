// SPDX-License-Identifier: MIT
#include "virtual_time_document.h"
#include <cassert>
using Pin=suitecut::NativeVirtualTimeDocument;
int main(){
 const Pin::Identity owner{"root","","document-a",23,1};
 Pin pin;assert(!pin.Current(owner));
 auto bad=owner;bad.unique_context.clear();assert(!pin.Pin(bad));
 bad=owner;bad.session="child-session";assert(!pin.Pin(bad));
 bad=owner;bad.renderer_epoch=0;assert(!pin.Pin(bad));
 assert(pin.Pin(owner)&&pin.Current(owner)&&!pin.Pin(owner));
 assert(pin.Frame("","root")&&!pin.Frame("child-session","root")&&!pin.Frame("","child"));
 // Exact document receipts invalidate; unrelated or stale identity receipts do
 // not. Legacy numeric-only and modern unique-only receipts are both accepted.
 assert(pin.Destroyed("",23,"document-a"));
 assert(pin.Destroyed("",23,""));assert(pin.Destroyed("",0,"document-a"));
 assert(!pin.Destroyed("child-session",23,"document-a"));
 assert(!pin.Destroyed("",23,"retired-document"));
 assert(!pin.Destroyed("",24,"document-a"));assert(!pin.Destroyed("",0,""));
 for(int field=0;field<5;++field){
  auto next=owner;
  if(field==0)next.frame="new-root";
  if(field==1)next.session="new-session";
  if(field==2)next.unique_context="document-b";
  if(field==3)next.context=24;
  if(field==4)next.renderer_epoch=2;
  assert(!pin.Current(next));
 }
 pin.Reset();assert(!pin.Current(owner));
 auto replacement=owner;replacement.unique_context="document-b";
 assert(pin.Pin(replacement));assert(!pin.Current(owner));
 assert(!pin.Destroyed("",23,"document-a"));
 assert(pin.Destroyed("",23,"document-b"));
}
