// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "route_callback_leases.h"
int main(){
  using Book=suitecut::NativeRouteCallbackLeases;Book book;const auto page=*book.Scope(),other=*book.Scope();
  auto policy=suitecut::NativeContextRouteRegistration::Create("context","**/*",0);assert(policy);
  const auto queued=*book.Issue(page,"actual-page-queued"),running=*book.Issue(page,"actual-page-running"),context=*book.Issue(page,"actual-context",policy),elsewhere=*book.Issue(other,"other-page");
  assert(book.Start(running,page,"actual-page-running"));
  const auto selected=book.CapturePage(page);assert(selected.scope==page&&selected.tokens==std::vector<uint64_t>({queued,running})&&selected.receipts.size()==2&&selected.bytes()<=Book::kCapacity*(sizeof(uint64_t)+sizeof(suitecut::NativeRouteCallbackSnapshot::value_type))*2);
  assert(book.CapturePage(0).scope==0&&book.CapturePage(other+1).scope==0);
  const auto future=*book.Issue(page,"actual-future");book.Close(page);assert(selected.receipts[0]->settled()&&!selected.receipts[1]->settled());
  assert(book.Finish(running,page,"actual-page-running"));assert(selected.receipts[1]->settled());assert(book.CapturePage(page).receipts.empty());
  assert(!book.Start(queued,page,"actual-page-queued")&&!book.Start(context,page,"actual-context")&&!book.Start(future,page,"actual-future"));assert(book.Finish(elsewhere,other,"other-page"));
  const auto second=*book.Issue(page,"real-disposal");const auto unavailable=book.CapturePage(page);assert(unavailable.tokens==std::vector<uint64_t>{second});book.Clear();assert(!unavailable.receipts[0]->settled()&&unavailable.receipts[0]->unavailable());
  std::cout<<"Page callback selection: exact scope/context exclusion/original receipt/disposal groups passed\n";
}
