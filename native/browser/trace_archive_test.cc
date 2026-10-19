// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <string>
#include "trace_archive.h"
int main(int argc,char** argv){
  suitecut::TraceArchive archive;std::string error;for(const auto* name:{"../escape","/absolute","resources/../escape","resources\\escape","resources/"})assert(!archive.AddText(name,"",error));
  const std::string nul("bad\0name",8);assert(!archive.AddText(nul,"",error));
  assert(archive.AddText("trace.trace","{\"type\":\"context-options\"}\n",error));assert(!archive.AddText("trace.trace","replacement",error));
  assert(archive.Add("resources/real.bin",{0,1,2,0xff},error));assert(archive.Bytes()==31&&archive.Count()==2);
  std::vector<uint8_t> zip;assert(archive.Encode(zip,error));assert(zip.size()>100);
  if(argc==2){std::ofstream output(argv[1],std::ios::binary);output.write(reinterpret_cast<const char*>(zip.data()),zip.size());assert(output.good());}
  archive.Clear();assert(!archive.Bytes()&&!archive.Count());assert(archive.Encode(zip,error)&&zip.size()==22);
  // Validate ZIP header CRCs against the polynomial independently of the
  // production table, including all byte values and empty input.
  for(const size_t length:{size_t(0),size_t(9),size_t(256),size_t(65537)}){
    std::vector<uint8_t> body(length);for(size_t i=0;i<length;++i)body[i]=uint8_t(i);
    if(length==9)body={'1','2','3','4','5','6','7','8','9'};
    uint32_t crc=0xffffffff;for(const auto byte:body){crc^=byte;for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}crc=~crc;
    if(length==9)assert(crc==0xcbf43926);
    archive.Clear();assert(archive.Add("body",body,error));assert(archive.Encode(zip,error));
    const auto u32=[&](size_t offset){return uint32_t(zip[offset])|(uint32_t(zip[offset+1])<<8)|(uint32_t(zip[offset+2])<<16)|(uint32_t(zip[offset+3])<<24);};
    assert(u32(14)==crc);const auto directory=30+size_t(4)+length;
    assert(u32(directory)==0x02014b50&&u32(directory+16)==crc);
  }
}
