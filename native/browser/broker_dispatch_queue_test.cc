// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include "broker_dispatch_queue.h"
int main(){using Queue=suitecut::BrokerDispatchQueue;Queue queue;assert(!queue.Push(0,"{}",Queue::Owner::None));assert(!queue.Push(1,"",Queue::Owner::None));
 assert(queue.Push(7,"root actual packet",Queue::Owner::Control));assert(queue.Push(8,"child exact session packet",Queue::Owner::Bootstrap));assert(queue.Count()==2);
 auto root=queue.Pop();assert(root&&root->id==7&&root->owner==Queue::Owner::Control&&root->json=="root actual packet");
 // A reply enqueues follow-up work; this admission cannot synchronously consume
 // either the child or follow-up command from within the current observer.
 assert(queue.Push(9,"follow-up actual packet",Queue::Owner::Operation));auto child=queue.Pop();assert(child&&child->id==8&&child->json=="child exact session packet");auto follow=queue.Pop();assert(follow&&follow->id==9&&follow->owner==Queue::Owner::Operation);assert(!queue.Pop()&&!queue.Bytes());
 for(size_t i=0;i<Queue::kCount;++i){assert(queue.Push(int(i+1),"{}",Queue::Owner::None));}assert(!queue.Push(5000,"{}",Queue::Owner::None));queue.Clear();assert(!queue.Count()&&!queue.Bytes());
 assert(queue.Push(1,std::string(Queue::kBytes,'x'),Queue::Owner::Operation));assert(!queue.Push(2,"x",Queue::Owner::Control));queue.Clear();assert(queue.Empty());
}
