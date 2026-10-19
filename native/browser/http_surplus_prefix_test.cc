// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http_surplus_prefix.h"
#include <cassert>
#include <iostream>
#include <string>
int main(){
 using P=suitecut::NativeHTTP1SurplusPrefix;
 auto bytes=[](const std::string&s){return std::span(reinterpret_cast<const unsigned char*>(s.data()),s.size());};
 P parser;parser.Configure(5,true);assert(parser.Observe(bytes("hello"))&&!parser.Failed());assert(parser.BodyObserved()==5&&parser.PrefixObserved()==0);
 assert(parser.Observe(bytes("HT"))&&parser.PrefixObserved()==2);assert(parser.Observe(bytes("TP/1.1 200 OK"))&&parser.PrefixObserved()==5);
 for(size_t split=0;split<=7;++split){parser.Configure(5,true);const std::string input("hello\0x",7);assert(parser.Observe(bytes(input).first(split))==(split<=5));assert(!parser.Observe(bytes(input).subspan(split)));assert(parser.Failed()&&parser.BodyObserved()==5);}
 parser.Configure(1,true);const std::string original("\x0b\0",2);assert(!parser.Observe(bytes(original)));assert(parser.BodyObserved()==1&&parser.PrefixObserved()==0);
 parser.Configure(0,true);assert(!parser.Observe(bytes("X")));
 parser.Configure(0,false);assert(parser.Observe(bytes("not a body to classify")));assert(!parser.Failed()&&parser.BodyObserved()==0);
 parser.Reset();assert(parser.Observe(bytes("uninitialized observer"))&&!parser.Failed());
 std::cout<<"PASS fixed-space real-boundary prefix grammar, every segmentation, exact Brotli surplus, disabled/uninitialized provenance\n";
}
