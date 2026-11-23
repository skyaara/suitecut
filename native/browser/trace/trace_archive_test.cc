// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <fstream>
#include <string>
#include "trace/trace_archive.h"
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
  const auto u16=[&](size_t offset){return uint32_t(zip[offset])|(uint32_t(zip[offset+1])<<8);};
  const auto u32=[&](size_t offset){return u16(offset)|(u16(offset+2)<<16);};
  const auto u64=[&](size_t offset){return uint64_t(u32(offset))|(uint64_t(u32(offset+4))<<32);};
  // Walk the central directory found through the end record (ZIP64 when present)
  // and return each name with its ZIP64-resolved size and local header offset.
  struct Row{std::string name;uint64_t size,offset;};
  const auto rows=[&](bool zip64){
    const auto end=zip.size()-22;assert(u32(end)==0x06054b50);uint64_t count=u16(end+10),length=u32(end+12);size_t at=u32(end+16),stop=end;
    assert(zip64==(end>=20&&u32(end-20)==0x07064b50));
    if(zip64){const auto record=size_t(u64(end-20+8));
      assert(u32(record)==0x06064b50&&u64(record+4)==44&&u64(record+24)==u64(record+32));count=u64(record+32);length=u64(record+40);at=size_t(u64(record+48));stop=record;
      assert(u16(end+8)==std::min<uint64_t>(count,0xffff)&&u16(end+10)==std::min<uint64_t>(count,0xffff));}
    assert(stop==at+length);
    std::vector<Row> out;for(uint64_t i=0;i<count;++i){
      assert(u32(at)==0x02014b50);const auto name_length=u16(at+28),extra_length=u16(at+30);uint64_t size=u32(at+24),offset=u32(at+42);
      size_t field=at+46+name_length;const auto field_end=field+extra_length;
      if(extra_length){assert(u16(field)==1&&u16(field+2)+4u==extra_length&&u16(at+6)==45);field+=4;if(size==0xffffffffu){size=u64(field);assert(u64(field+8)==size);field+=16;}if(offset==0xffffffffu){offset=u64(field);field+=8;}assert(field==field_end);}
      out.push_back({std::string(zip.begin()+long(at+46),zip.begin()+long(at+46+name_length)),size,offset});at=field_end;
    }
    assert(at==stop);return out;
  };
  // No count cap: more entries than ZIP32's 16-bit count switch to ZIP64 end records.
  archive.Clear();for(size_t i=0;i<70000;++i)assert(archive.AddText("resources/"+std::to_string(i),std::to_string(i),error));assert(archive.Count()==70000&&archive.Encode(zip,error));
  {const auto all=rows(true);assert(all.size()==70000);for(const auto& row:all){assert(u32(row.offset)==0x04034b50&&u16(row.offset+26)==row.name.size());const auto body=std::string(zip.begin()+long(row.offset+30+row.name.size()),zip.begin()+long(row.offset+30+row.name.size()+row.size));assert("resources/"+body==row.name);}}
  // Forced ZIP64 records carry exact sizes/offsets in local and central extra fields.
  archive.Clear();archive.ForceZip64(true);assert(archive.AddText("a.txt","hello",error)&&archive.Add("b.bin",{0,1,2,0xff},error)&&archive.Encode(zip,error));
  {const auto all=rows(true);assert(all.size()==2&&all[0].name=="a.txt"&&all[0].size==5&&all[0].offset==0&&all[1].name=="b.bin"&&all[1].size==4);
   for(const auto& row:all){assert(u32(row.offset)==0x04034b50&&u16(row.offset+4)==45&&u32(row.offset+18)==0xffffffffu&&u32(row.offset+22)==0xffffffffu&&u16(row.offset+28)==20);const auto extra=row.offset+30+row.name.size();assert(u16(extra)==1&&u16(extra+2)==16&&u64(extra+4)==row.size&&u64(extra+12)==row.size);}
   assert(all[1].offset==30+5+20+5);}
  archive.ForceZip64(false);
  // No suitecut name or byte cap: long names and entries past the old 64 MiB pass whole.
  archive.Clear();const std::string long_name(4096,'n');assert(archive.AddText(long_name,"x",error));assert(archive.Add("resources/large.bin",std::vector<uint8_t>(65*1024*1024+1,7),error)&&archive.Bytes()==65*1024*1024+2&&archive.Encode(zip,error));
  {const auto all=rows(false);assert(all.size()==2&&all[0].name==long_name&&all[1].size==65*1024*1024+1);}
  // The ZIP name-length field itself is 16 bits in ZIP32 and ZIP64 alike.
  assert(!archive.AddText(std::string(65536,'n'),"x",error)&&archive.AddText(std::string(65535,'m'),"x",error));
}
