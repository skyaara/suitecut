// SPDX-License-Identifier: MIT
#include "accepted_packet_receipt.h"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
using Reader=suitecut::AcceptedReaderWork;
using Writer=suitecut::AcceptedWriterWork;
int main(){
 size_t controls=0;
 auto reader=std::make_shared<Reader>();const auto empty=reader->Cut();assert(reader->Through(empty));++controls;
 const auto first=*reader->Admit();const auto cut=reader->Cut();assert(!reader->Through(cut)&&reader->Inspect().waiting_post==1);++controls;
 assert(reader->PostingReturned(first,true)&&!reader->Through(cut));++controls;
 {suitecut::AcceptedRouteFrame frame(reader,first);assert(reader->Inspect().executing==1&&!reader->Through(cut));}assert(reader->Through(cut)&&reader->Inspect().pending==0);++controls;
 const auto later=*reader->Admit();assert(reader->Through(cut)&&!reader->Through(reader->Cut()));assert(reader->NeverPosted(later)&&reader->Inspect().not_posted==1);++controls;
 auto altered=cut;altered.sequence=0;assert(!reader->Through(altered));assert(reader->ReleaseCut(cut)&&!reader->Through(cut)&&!reader->ReleaseCut(cut));++controls;
 Reader foreign;assert(!foreign.Through(empty)&&!foreign.PostingReturned(first,true));++controls;
 auto reentrant=std::make_shared<Reader>();suitecut::AcceptedPostReceipt post(reentrant);const auto reentrant_cut=reentrant->Cut();{suitecut::AcceptedRouteFrame frame(reentrant,post.Ticket());assert(!reentrant->Through(reentrant_cut));}assert(reentrant->Inspect().pending==1&&!reentrant->Through(reentrant_cut));post.AboutToPost();post.Returned(true);assert(reentrant->Through(reentrant_cut));++controls;
 auto refused=std::make_shared<Reader>();{suitecut::AcceptedPostReceipt allocation(refused);}assert(refused->Inspect().pending==0&&refused->Inspect().not_posted==1&&refused->Through(refused->Cut()));++controls;
 auto rejected=std::make_shared<Reader>();{suitecut::AcceptedPostReceipt reject(rejected);reject.AboutToPost();reject.Returned(false);}assert(rejected->Inspect().pending==0&&rejected->Inspect().posted==0&&rejected->Inspect().not_posted==1);++controls;
 auto uncertain=std::make_shared<Reader>();std::optional<suitecut::NativeStreamTicket> uncertain_ticket;{suitecut::AcceptedPostReceipt throws(uncertain);uncertain_ticket=throws.Ticket();throws.AboutToPost();}assert(uncertain->Inspect().pending==1&&uncertain->Inspect().unwound==1&&!uncertain->Through(uncertain->Cut()));{suitecut::AcceptedRouteFrame frame(uncertain,uncertain_ticket);}assert(uncertain->Inspect().pending==0&&!uncertain->Through(uncertain->Cut()));++controls;
 auto throwing=std::make_shared<Reader>();const auto thrown=*throwing->Admit();assert(throwing->PostingReturned(thrown,true));try{suitecut::AcceptedRouteFrame frame(throwing,thrown);throw std::runtime_error("original route failure");}catch(const std::runtime_error&){}assert(throwing->Inspect().pending==0&&throwing->Inspect().poisoned);++controls;
 auto saturated=std::make_shared<Reader>();for(size_t n=0;n<Reader::kPending;++n)assert(saturated->Admit());assert(!saturated->Admit()&&saturated->Inspect().pending==Reader::kPending&&saturated->Inspect().poisoned);++controls;
 auto cutfull=std::make_shared<Reader>();for(size_t n=0;n<64;++n)assert(cutfull->Cut().nonce);assert(!cutfull->Cut().nonce&&cutfull->Inspect().poisoned);++controls;
 auto writer=std::make_shared<Writer>();const auto blank=writer->Cut();assert(writer->Through(blank));++controls;
 const auto packet=*writer->Admit(Writer::Kind::Control,12);const auto publication=writer->Cut();assert(writer->Inspect().queued==1&&!writer->Through(publication));++controls;
 assert(writer->Dequeue(packet)&&writer->Inspect().queued==0&&writer->Inspect().writing==1&&!writer->Through(publication));++controls;
 assert(writer->Wrote(packet,4)&&!writer->FullWrite(packet)&&!writer->Through(publication));++controls;
 assert(writer->Wrote(packet,8)&&writer->FullWrite(packet)&&!writer->FullWrite(packet)&&writer->Through(publication)&&writer->Inspect().actual_bytes==12);++controls;
 const auto new_packet=*writer->Admit(Writer::Kind::Event,5);assert(writer->Through(publication));assert(writer->Dequeue(new_packet)&&writer->Wrote(new_packet,5)&&writer->FullWrite(new_packet));++controls;
 auto falsecut=publication;falsecut.sequence=0;assert(!writer->Through(falsecut));Writer other;assert(!other.Through(publication));assert(writer->ReleaseCut(publication)&&!writer->Through(publication));++controls;
 const auto frame=*writer->Admit(Writer::Kind::Frame,100);assert(writer->Dropped(frame,Writer::Discard::FrameReplaced)&&writer->Inspect().discarded==1&&writer->Inspect().full_written==2);++controls;
 const auto audio=*writer->Admit(Writer::Kind::Audio,20);assert(!writer->Dropped(audio,Writer::Discard::FrameReplaced)&&writer->Dropped(audio,Writer::Discard::AudioBudget));++controls;
 const auto event=*writer->Admit(Writer::Kind::Event,10);assert(!writer->Dropped(event,Writer::Discard::PageDropped)&&writer->Dequeue(event)&&!writer->Dropped(event,Writer::Discard::AudioBudget));assert(writer->Wrote(event,10)&&writer->FullWrite(event));++controls;
 auto abandoned=std::make_shared<Writer>();{suitecut::AcceptedPacketReceipt actual(abandoned,Writer::Kind::Control,10);}assert(abandoned->Inspect().poisoned&&abandoned->Inspect().queued==0&&abandoned->Inspect().full_written==0&&!abandoned->Through(abandoned->Cut()));++controls;
 auto moved=std::make_shared<Writer>();{suitecut::AcceptedPacketReceipt first_move(moved,Writer::Kind::Frame,10);suitecut::AcceptedPacketReceipt second_move(std::move(first_move));second_move.Dropped(Writer::Discard::PageDropped);}assert(!moved->Inspect().poisoned&&moved->Inspect().discarded==1);++controls;
 auto partial=std::make_shared<Writer>();{suitecut::AcceptedPacketReceipt actual(partial,Writer::Kind::Control,10);actual.Dequeued();actual.Wrote(3);actual.Failed();}assert(partial->Inspect().poisoned&&partial->Inspect().writing==1&&partial->Inspect().full_written==0);++controls;
 auto overflow=std::make_shared<Writer>();const auto oversize=*overflow->Admit(Writer::Kind::Control,10);assert(overflow->Dequeue(oversize)&&!overflow->Wrote(oversize,11)&&overflow->Inspect().writing==1&&overflow->Inspect().poisoned);++controls;
 auto full=std::make_shared<Writer>();for(size_t n=0;n<Writer::kPending;++n)assert(full->Admit(Writer::Kind::Event,4));assert(!full->Admit(Writer::Kind::Event,4)&&full->Inspect().poisoned);++controls;
 // Actual kernel prefix/header/payload writes retain the same native receipt.
 int fds[2];assert(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fds)==0);
 auto kernel=std::make_shared<Writer>();const std::string bytes="0008headerpayload";suitecut::AcceptedPacketReceipt actual(kernel,Writer::Kind::Control,bytes.size());const auto kernel_cut=kernel->Cut();actual.Dequeued();
 const auto prefix=write(fds[0],bytes.data(),4);assert(prefix==4);actual.Wrote(static_cast<size_t>(prefix));assert(!kernel->Through(kernel_cut)&&kernel->Inspect().queued==0&&kernel->Inspect().writing==1);++controls;
 char received[64]{};assert(read(fds[1],received,4)==4&&std::string(received,4)==bytes.substr(0,4));assert(!kernel->Through(kernel_cut));++controls;
 const auto payload=write(fds[0],bytes.data()+4,bytes.size()-4);assert(payload==static_cast<ssize_t>(bytes.size()-4));actual.Wrote(static_cast<size_t>(payload));assert(!kernel->Through(kernel_cut));actual.FullWrite();assert(kernel->Through(kernel_cut));assert(read(fds[1],received,bytes.size()-4)==payload&&std::string(received,static_cast<size_t>(payload))==bytes.substr(4));++controls;
 assert(close(fds[0])==0&&close(fds[1])==0);++controls;
 // Closing the receiver is a real failed write, never a published watermark.
 signal(SIGPIPE,SIG_IGN);assert(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fds)==0);assert(close(fds[1])==0);auto failed=std::make_shared<Writer>();{suitecut::AcceptedPacketReceipt lost(failed,Writer::Kind::Watermark,4);const auto failed_cut=failed->Cut();lost.Dequeued();errno=0;assert(write(fds[0],"mark",4)==-1&&errno==EPIPE);lost.Failed();assert(!failed->Through(failed_cut)&&failed->Inspect().actual_bytes==0);}assert(close(fds[0])==0);++controls;
 assert(controls==34);std::cout<<"Accepted stream work: "<<controls<<" controlled policy/owner/kernel-boundary cases passed; no actual Source/Engine idle proof\n";
}
