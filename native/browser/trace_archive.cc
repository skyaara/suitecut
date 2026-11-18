// SPDX-License-Identifier: MIT
#include "trace_archive.h"
#include <algorithm>
#include <array>
#include <limits>
#include <utility>
namespace suitecut {
namespace {
bool Name(const std::string& value){
  if(value.empty()||value.front()=='/'||value.find('\0')!=std::string::npos||value.find('\\')!=std::string::npos||value.find(':')!=std::string::npos)return false;
  size_t offset=0;while(offset<value.size()){const auto end=value.find('/',offset);const auto part=value.substr(offset,end==std::string::npos?end:end-offset);if(part.empty()||part=="."||part=="..")return false;if(end==std::string::npos)return true;offset=end+1;}return false;
}
void U16(std::vector<uint8_t>& output,uint16_t value){output.push_back(uint8_t(value));output.push_back(uint8_t(value>>8));}
void U32(std::vector<uint8_t>& output,uint32_t value){U16(output,uint16_t(value));U16(output,uint16_t(value>>16));}
void U64(std::vector<uint8_t>& output,uint64_t value){U32(output,uint32_t(value));U32(output,uint32_t(value>>32));}
constexpr uint64_t kZip32=0xffffffffu;constexpr uint64_t kZip16=0xffffu;
// ZIP has one 16-bit name-length field in both ZIP32 and ZIP64 headers.
bool NameFits(const std::string& name){return name.size()<=std::numeric_limits<uint16_t>::max();}
constexpr auto CRCTable(){std::array<uint32_t,256> table{};for(size_t byte=0;byte<table.size();++byte){uint32_t crc=uint32_t(byte);for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));table[byte]=crc;}return table;}
constexpr auto kCRC=CRCTable();
uint32_t CRC(const std::vector<uint8_t>& value){uint32_t crc=0xffffffff;for(const auto byte:value)crc=(crc>>8)^kCRC[(crc^byte)&255];return ~crc;}
}
bool TraceArchive::ValidName(const std::string& name){return Name(name);}
bool TraceArchive::Add(std::string name,std::vector<uint8_t> bytes,std::string& error){
  if(!Name(name)){error="Invalid native trace archive entry name";return false;}
  if(!NameFits(name)){error="Native trace archive entry name does not fit the ZIP 16-bit name-length field";return false;}
  if(entries_.contains(name)){error="Duplicate native trace archive entry";return false;}
  bytes_+=bytes.size();entries_.emplace(std::move(name),std::move(bytes));return true;
}
bool TraceArchive::AddText(std::string name,const std::string& text,std::string& error){return Add(std::move(name),std::vector<uint8_t>(text.begin(),text.end()),error);}
bool TraceArchive::Encode(std::vector<uint8_t>& output,std::string& error) const {
  struct Entry {const std::string* name;uint64_t size,offset;uint32_t crc;};std::vector<Entry> directory;std::vector<uint8_t> result;
  result.reserve(bytes_+entries_.size()*176+98);
  for(const auto& [name,bytes]:entries_){
    if(!NameFits(name)){error="Native trace archive entry name does not fit the ZIP 16-bit name-length field";return false;}
    const uint64_t size=bytes.size(),offset=result.size();const auto crc=CRC(bytes);directory.push_back({&name,size,offset,crc});
    // A local ZIP64 extra field must carry both sizes.
    const bool big=force_zip64_||size>=kZip32;
    U32(result,0x04034b50);U16(result,big?45:20);U16(result,0x0800);U16(result,0);U16(result,0);U16(result,0x0021);U32(result,crc);U32(result,big?uint32_t(kZip32):uint32_t(size));U32(result,big?uint32_t(kZip32):uint32_t(size));U16(result,uint16_t(name.size()));U16(result,big?20:0);result.insert(result.end(),name.begin(),name.end());
    if(big){U16(result,0x0001);U16(result,16);U64(result,size);U64(result,size);}
    result.insert(result.end(),bytes.begin(),bytes.end());
  }
  const uint64_t offset=result.size();
  for(const auto& entry:directory){
    const auto& name=*entry.name;const bool big_size=force_zip64_||entry.size>=kZip32,big_offset=force_zip64_||entry.offset>=kZip32;
    // ZIP64 extra fields hold, in order, only the fields whose ZIP32 value is 0xffffffff.
    const uint16_t extra=uint16_t((big_size?16:0)+(big_offset?8:0));const uint16_t version=extra?45:20;
    U32(result,0x02014b50);U16(result,version);U16(result,version);U16(result,0x0800);U16(result,0);U16(result,0);U16(result,0x0021);U32(result,entry.crc);U32(result,big_size?uint32_t(kZip32):uint32_t(entry.size));U32(result,big_size?uint32_t(kZip32):uint32_t(entry.size));U16(result,uint16_t(name.size()));U16(result,extra?uint16_t(4+extra):0);U16(result,0);U16(result,0);U16(result,0);U32(result,0);U32(result,big_offset?uint32_t(kZip32):uint32_t(entry.offset));result.insert(result.end(),name.begin(),name.end());
    if(extra){U16(result,0x0001);U16(result,extra);if(big_size){U64(result,entry.size);U64(result,entry.size);}if(big_offset)U64(result,entry.offset);}
  }
  const uint64_t length=result.size()-offset,count=directory.size();
  if(force_zip64_||count>=kZip16||length>=kZip32||offset>=kZip32){
    // ZIP64 end of central directory record, then its locator.
    const uint64_t record=result.size();
    U32(result,0x06064b50);U64(result,44);U16(result,45);U16(result,45);U32(result,0);U32(result,0);U64(result,count);U64(result,count);U64(result,length);U64(result,offset);
    U32(result,0x07064b50);U32(result,0);U64(result,record);U32(result,1);
  }
  // Fields that do not fit are 0xffff/0xffffffff and resolved from the ZIP64 record.
  U32(result,0x06054b50);U16(result,0);U16(result,0);U16(result,uint16_t(std::min(count,kZip16)));U16(result,uint16_t(std::min(count,kZip16)));U32(result,uint32_t(std::min(length,kZip32)));U32(result,uint32_t(std::min(offset,kZip32)));U16(result,0);output=std::move(result);return true;
}
} // namespace suitecut
