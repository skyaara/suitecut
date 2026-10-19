// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace suitecut {
struct NativeHTTPMultipartPart {
  std::string name,filename,mime;
  size_t offset=0,size=0;
  bool file=false;
};
// Host adapters marshal source values and bytes; this native encoder owns the
// wire representation, injection checks, ordering and final upload byte limit.
// Plan before allocating the body so the caller can reserve the simultaneous
// raw+encoded vector capacities, independently of bounded header metadata.
inline bool EncodeNativeHTTPMultipart(std::span<const unsigned char>input,
    const std::vector<NativeHTTPMultipartPart>&parts,std::string_view boundary,
    size_t limit,std::vector<unsigned char>&output,std::string&error,
    size_t capacity_limit=std::numeric_limits<size_t>::max()){
  std::vector<unsigned char>().swap(output);
  const auto fail=[&](const char*message){std::vector<unsigned char>().swap(output);error=message;return false;};
  if(parts.size()>1024||input.size()>limit||boundary.size()<16||boundary.size()>70)return fail("Native multipart input capacity exceeded");
  for(const unsigned char c:boundary)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return fail("Invalid native multipart boundary");
  const auto quote=[](std::string_view text){std::string value;for(const char c:text){if(c=='\r')value+="%0D";else if(c=='\n')value+="%0A";else if(c=='\"')value+="%22";else value+=c;}return value;};
  std::vector<std::string>headers;headers.reserve(parts.size());
  size_t cursor=0,metadata=0,total=0;
  const auto measure=[&](size_t bytes){if(bytes>limit-total)return false;total+=bytes;return true;};
  for(const auto&part:parts){
    if(part.offset!=cursor||part.size>input.size()-cursor)return fail("Native multipart part ranges must describe the exact ordered source bytes");
    if(part.name.size()>8192||part.filename.size()>8192||part.mime.size()>4096||part.name.find('\0')!=std::string::npos||part.filename.find('\0')!=std::string::npos||part.mime.find_first_of("\r\n")!=std::string::npos||part.mime.find('\0')!=std::string::npos)return fail("Invalid native multipart field metadata");
    const size_t cost=part.name.size()+part.filename.size()+part.mime.size();if(cost>65536-metadata)return fail("Native multipart metadata exceeds 64 KiB");metadata+=cost;
    auto header="--"+std::string(boundary)+"\r\ncontent-disposition: form-data; name=\""+quote(part.name)+"\""+(part.file?"; filename=\""+quote(part.filename)+"\"":"")+"\r\n"+(part.file?"content-type: "+(part.mime.empty()?"application/octet-stream":part.mime)+"\r\n":"")+"\r\n";
    if(!measure(header.size())||!measure(part.size)||!measure(2))return fail("Native multipart wire body exceeds 8 MiB");
    headers.push_back(std::move(header));cursor+=part.size;
  }
  if(cursor!=input.size())return fail("Native multipart source bytes were not fully assigned");
  const auto closing="--"+std::string(boundary)+"--\r\n";
  if(!measure(closing.size()))return fail("Native multipart wire body exceeds 8 MiB");
  if(total>capacity_limit)return fail("Native API simultaneous upload capacity exceeded");
  std::vector<unsigned char>encoded(total);
  if(encoded.capacity()>capacity_limit)return fail("Native API simultaneous upload capacity exceeded");
  size_t offset=0;
  const auto copy=[&](std::span<const unsigned char>bytes){std::copy(bytes.begin(),bytes.end(),encoded.begin()+offset);offset+=bytes.size();};
  for(size_t i=0;i<parts.size();++i){const auto&header=headers[i];copy({reinterpret_cast<const unsigned char*>(header.data()),header.size()});copy(input.subspan(parts[i].offset,parts[i].size));copy({reinterpret_cast<const unsigned char*>("\r\n"),2});}
  copy({reinterpret_cast<const unsigned char*>(closing.data()),closing.size()});
  output.swap(encoded);error.clear();return true;
}
} // namespace suitecut
