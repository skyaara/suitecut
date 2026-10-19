// SPDX-License-Identifier: MIT
#include "trace_archive.h"
#include <array>
#include <limits>
#include <utility>
namespace suitecut {
namespace {
bool Name(const std::string& value){
  if(value.empty()||value.size()>512||value.front()=='/'||value.find('\0')!=std::string::npos||value.find('\\')!=std::string::npos||value.find(':')!=std::string::npos)return false;
  size_t offset=0;while(offset<value.size()){const auto end=value.find('/',offset);const auto part=value.substr(offset,end==std::string::npos?end:end-offset);if(part.empty()||part=="."||part=="..")return false;if(end==std::string::npos)return true;offset=end+1;}return false;
}
void U16(std::vector<uint8_t>& output,uint16_t value){output.push_back(uint8_t(value));output.push_back(uint8_t(value>>8));}
void U32(std::vector<uint8_t>& output,uint32_t value){U16(output,uint16_t(value));U16(output,uint16_t(value>>16));}
constexpr auto CRCTable(){std::array<uint32_t,256> table{};for(size_t byte=0;byte<table.size();++byte){uint32_t crc=uint32_t(byte);for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));table[byte]=crc;}return table;}
constexpr auto kCRC=CRCTable();
uint32_t CRC(const std::vector<uint8_t>& value){uint32_t crc=0xffffffff;for(const auto byte:value)crc=(crc>>8)^kCRC[(crc^byte)&255];return ~crc;}
}
bool TraceArchive::ValidName(const std::string& name){return Name(name);}
bool TraceArchive::Add(std::string name,std::vector<uint8_t> bytes,std::string& error){
  if(!Name(name)){error="Invalid native trace archive entry name";return false;}
  if(entries_.contains(name)){error="Duplicate native trace archive entry";return false;}
  if(entries_.size()>=kEntries||bytes.size()>kEntryBytes||bytes.size()>kBytes-bytes_){error="Native trace archive resource capacity exceeded";return false;}
  bytes_+=bytes.size();entries_.emplace(std::move(name),std::move(bytes));return true;
}
bool TraceArchive::AddText(std::string name,const std::string& text,std::string& error){return Add(std::move(name),std::vector<uint8_t>(text.begin(),text.end()),error);}
bool TraceArchive::Encode(std::vector<uint8_t>& output,std::string& error) const {
  struct Entry {const std::string* name;const std::vector<uint8_t>* bytes;uint32_t crc,offset;};std::vector<Entry> directory;std::vector<uint8_t> result;
  result.reserve(bytes_+entries_.size()*128+22);
  for(const auto& [name,bytes]:entries_){
    if(result.size()>std::numeric_limits<uint32_t>::max()){error="Native trace ZIP32 offset exceeds capacity";return false;}
    const auto crc=CRC(bytes);directory.push_back({&name,&bytes,crc,uint32_t(result.size())});U32(result,0x04034b50);U16(result,20);U16(result,0x0800);U16(result,0);U16(result,0);U16(result,0x0021);U32(result,crc);U32(result,uint32_t(bytes.size()));U32(result,uint32_t(bytes.size()));U16(result,uint16_t(name.size()));U16(result,0);result.insert(result.end(),name.begin(),name.end());result.insert(result.end(),bytes.begin(),bytes.end());
  }
  const auto offset=uint32_t(result.size());
  for(const auto& entry:directory){const auto& name=*entry.name;const auto size=uint32_t(entry.bytes->size());U32(result,0x02014b50);U16(result,20);U16(result,20);U16(result,0x0800);U16(result,0);U16(result,0);U16(result,0x0021);U32(result,entry.crc);U32(result,size);U32(result,size);U16(result,uint16_t(name.size()));U16(result,0);U16(result,0);U16(result,0);U16(result,0);U32(result,0);U32(result,entry.offset);result.insert(result.end(),name.begin(),name.end());}
  const auto length=uint32_t(result.size())-offset;U32(result,0x06054b50);U16(result,0);U16(result,0);U16(result,uint16_t(directory.size()));U16(result,uint16_t(directory.size()));U32(result,length);U32(result,offset);U16(result,0);output=std::move(result);return true;
}
} // namespace suitecut
