// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
#include <brotli/encode.h>
#include "http_content_decoder.h"
using Decoder=suitecut::NativeHTTPContentDecoder;using Bytes=std::vector<unsigned char>;
Bytes Z(const Bytes&input,int bits){z_stream z{};assert(deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,bits,8,Z_DEFAULT_STRATEGY)==Z_OK);Bytes output(compressBound(input.size())+64);z.next_in=const_cast<unsigned char*>(input.data());z.avail_in=input.size();z.next_out=output.data();z.avail_out=output.size();assert(deflate(&z,Z_FINISH)==Z_STREAM_END);output.resize(z.total_out);deflateEnd(&z);return output;}
Bytes B(const Bytes&input){size_t size=BrotliEncoderMaxCompressedSize(input.size());Bytes output(size);assert(BrotliEncoderCompress(5,24,BROTLI_MODE_GENERIC,input.size(),input.data(),&size,output.data()));output.resize(size);return output;}
bool Read(const std::string&encoding,const Bytes&compressed,Bytes&actual,size_t chunk=1){Decoder decoder([&](auto bytes){actual.insert(actual.end(),bytes.begin(),bytes.end());return true;});if(!decoder.Configure(encoding))return false;for(size_t i=0;i<compressed.size();i+=chunk)if(!decoder.Push(std::span(compressed).subspan(i,std::min(chunk,compressed.size()-i))))return false;return decoder.Finish();}
int main(){
 const Bytes hello{'h','e','l','l','o'};const auto gzip=Z(hello,MAX_WBITS+16),wrapped=Z(hello,MAX_WBITS),raw=Z(hello,-MAX_WBITS),br=B(hello);Bytes actual;
 for(const auto&[name,bytes]:std::vector<std::pair<std::string,Bytes>>{{"gzip",gzip},{"deflate",wrapped},{"deflate",raw},{"br",br},{"",hello}}){actual.clear();assert(Read(name,bytes,actual));assert(actual==hello);assert(Decoder::GlobalBytes()==0);}
 Bytes ambiguous{0x78,0x9c,0x00,0x63,0xff};ambiguous.insert(ambiguous.end(),156,'x');ambiguous.push_back(3);ambiguous.push_back(0);actual.clear();assert(Read("deflate",ambiguous,actual));assert(actual==Bytes(156,'x'));
 actual.clear();assert(Read("gzip, br",B(gzip),actual));assert(actual==hello);
 actual.clear();auto concatenated=gzip;concatenated.insert(concatenated.end(),gzip.begin(),gzip.end());assert(Read("gzip",concatenated,actual));auto twice=hello;twice.insert(twice.end(),hello.begin(),hello.end());assert(actual==twice);
 for(size_t count=0;count<gzip.size();++count){actual.clear();assert(!Read("gzip",Bytes(gzip.begin(),gzip.begin()+count),actual));assert(Decoder::GlobalBytes()==0);}
 for(size_t count=0;count<br.size();++count){actual.clear();assert(!Read("br",Bytes(br.begin(),br.begin()+count),actual));assert(Decoder::GlobalBytes()==0);}
 auto corrupt=gzip;corrupt.back()^=1;actual.clear();assert(!Read("gzip",corrupt,actual));
 auto extra=br;extra.push_back(0);actual.clear();assert(!Read("br",extra,actual));
 for(const std::string value:{"gzip,","gzip,,br","unsupported","gzip,gzip,gzip,gzip,gzip"}){Decoder decoder([](auto){assert(false);return true;});assert(!decoder.Configure(value));}
 {Decoder decoder([](auto){return true;},32);assert(!decoder.Configure("gzip"));assert(decoder.bytes()==0);}
 {bool current=true;Decoder decoder([](auto){return true;},Decoder::kPerCodecBytes,[&]{return current;});assert(decoder.Configure("gzip"));assert(decoder.Push(std::span(gzip).first(2)));const auto live=decoder.bytes();current=false;assert(!decoder.Push(std::span(gzip).subspan(2)));assert(decoder.bytes()==live);decoder.Cancel();assert(Decoder::GlobalBytes()==0);}
 {std::unique_ptr<Decoder>decoder;decoder=std::make_unique<Decoder>([&](auto){assert(Decoder::GlobalBytes()>0);decoder.reset();assert(Decoder::GlobalBytes()>0);return true;});assert(decoder->Configure("gzip"));auto*inflight=decoder.get();assert(!inflight->Push(gzip));assert(!decoder&&Decoder::GlobalBytes()==0);}
 Bytes large(1024*1024);std::mt19937 random(7);for(auto&byte:large)byte=random();const auto compressed=B(large);
 {Decoder decoder([](auto){return true;},65536);assert(decoder.Configure("br"));assert(!decoder.Push(compressed));assert(decoder.bytes()<=65536);decoder.Cancel();assert(Decoder::GlobalBytes()==0);}
 actual.clear();assert(Read("br",compressed,actual,4096));assert(actual==large);assert(Decoder::GlobalBytes()==0);
 {Decoder decoder([](auto){return true;});assert(decoder.Configure("gzip"));assert(decoder.Push(gzip));assert(decoder.Finish());assert(decoder.Finish());assert(!decoder.Push(gzip));}
 {std::vector<std::unique_ptr<Decoder>> held;for(int i=0;i<64;++i){held.push_back(std::make_unique<Decoder>([](auto){return true;}));assert(held.back()->Configure(""));}assert(Decoder::Instances()==64);Decoder rejected([](auto){return true;});assert(!rejected.Configure(""));held.pop_back();assert(Decoder::Instances()==63);Decoder admitted([](auto){return true;});assert(admitted.Configure(""));}
 {std::vector<std::unique_ptr<Decoder>> held;bool full=false;for(int i=0;i<64;++i){auto decoder=std::make_unique<Decoder>([](auto){return true;});assert(decoder->Configure("br"));const bool accepted=decoder->Push(compressed);assert(Decoder::GlobalBytes()<=Decoder::kGlobalCodecBytes);held.push_back(std::move(decoder));if(!accepted){full=true;break;}}assert(full);assert(Decoder::GlobalBytes()>0);held.clear();assert(Decoder::GlobalBytes()==0);}
 {int calls=0;Decoder decoder([&](auto){++calls;return false;});assert(decoder.Configure(""));assert(decoder.Push({}));assert(calls==0);assert(!decoder.Push(hello));assert(decoder.sink_rejected());assert(!decoder.error().empty());}
 {Decoder decoder([](auto){return false;});assert(decoder.Configure("gzip"));assert(!decoder.Push(gzip));assert(decoder.sink_rejected());}
 assert(Decoder::Instances()==0&&Decoder::GlobalBytes()==0);
 std::cout<<"PASS: gzip/deflate/raw/br streams, chains/members, real finalization, allocation limits, original checkpoint, reentrant cancellation\n";
}
