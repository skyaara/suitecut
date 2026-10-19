// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http_multipart.h"
#include <cassert>
#include <iostream>
#include <random>
using Part=suitecut::NativeHTTPMultipartPart;
int main(){
  const std::string boundary="scut--0123456789abcdef0123456789abcdef";
  std::vector<unsigned char>raw={'J','o','h','n',0,255,13,10},encoded;
  std::vector<Part>parts={{"name","","",0,4,false},{"file","q\"\r\n.bin","application/octet-stream",4,4,true},{"name","","",8,0,false}};
  std::string error;assert(suitecut::EncodeNativeHTTPMultipart(raw,parts,boundary,8388608,encoded,error));
  const std::string first="--"+boundary+"\r\ncontent-disposition: form-data; name=\"name\"\r\n\r\nJohn\r\n--"+boundary+"\r\ncontent-disposition: form-data; name=\"file\"; filename=\"q%22%0D%0A.bin\"\r\ncontent-type: application/octet-stream\r\n\r\n";
  std::vector<unsigned char>expected(first.begin(),first.end());expected.insert(expected.end(),raw.begin()+4,raw.end());const auto tail="\r\n--"+boundary+"\r\ncontent-disposition: form-data; name=\"name\"\r\n\r\n\r\n--"+boundary+"--\r\n";expected.insert(expected.end(),tail.begin(),tail.end());assert(encoded==expected&&error.empty());
  assert(encoded.capacity()==encoded.size());const size_t wire_size=encoded.size();
  assert(!suitecut::EncodeNativeHTTPMultipart(raw,parts,boundary,8388608,encoded,error,wire_size-1));assert(encoded.empty()&&encoded.capacity()==0);
  assert(suitecut::EncodeNativeHTTPMultipart(raw,parts,boundary,8388608,encoded,error,wire_size));assert(encoded==expected&&encoded.capacity()==wire_size);
  for(int failure=0;failure<7;++failure){auto bad=parts;auto token=boundary;size_t limit=8388608;
    if(failure==0)bad[0].offset=1;
    if(failure==1)bad[0].size=99;
    if(failure==2)bad[1].mime="text/plain\r\nInjected: yes";
    if(failure==3)bad[0].name=std::string("x\0y",3);
    if(failure==4)bad.pop_back(),bad[1].size=3;
    if(failure==5)limit=10;
    if(failure==6)token+="\r\n";
    assert(!suitecut::EncodeNativeHTTPMultipart(raw,bad,token,limit,encoded,error));assert(encoded.empty()&&!error.empty());}
  std::mt19937 rng(0x5c017);for(int cycle=0;cycle<10000;++cycle){const size_t size=rng()%1024;std::vector<unsigned char>bytes(size);for(auto&byte:bytes)byte=static_cast<unsigned char>(rng());std::vector<Part>one={{"binary","actual.bin","application/octet-stream",0,size,true}};assert(suitecut::EncodeNativeHTTPMultipart(bytes,one,boundary,8388608,encoded,error));const std::string wire(encoded.begin(),encoded.end());const auto offset=wire.find("\r\n\r\n")+4;assert(offset>=4&&offset+size<=encoded.size());assert(std::equal(bytes.begin(),bytes.end(),encoded.begin()+offset));}
  std::cout<<"native multipart golden wire, duplicate/empty fields, binary preservation, escaped names, injection/range/capacity rejection and10000 binary payloads passed\n";
}
