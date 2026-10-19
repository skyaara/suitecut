// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http_upload_preparations.h"
#include <cassert>
#include <limits>
#include <iostream>
int main(){
  constexpr size_t MiB=1048576,budget=16*MiB;std::vector<uint8_t>upload;std::string error;
  for(size_t offset=0;offset<8*MiB;){const size_t amount=std::min(size_t(24576),8*MiB-offset);const auto previous=upload.capacity();
    assert(suitecut::AppendNativeHTTPUpload(upload,amount,previous,8*MiB,budget,[offset](uint8_t*target,size_t size){for(size_t n=0;n<size;++n)target[n]=uint8_t((offset+n)%251);return true;},error));
    assert(upload.capacity()<=8*MiB);if(upload.capacity()!=previous)assert(previous+upload.capacity()<=budget);offset+=amount;
  }
  for(size_t n=0;n<upload.size();++n)assert(upload[n]==uint8_t(n%251));
  std::vector<uint8_t>().swap(upload);upload.resize(5*MiB);const auto before=upload.capacity();bool filled=false;
  assert(!suitecut::AppendNativeHTTPUpload(upload,MiB,before+6*MiB,8*MiB,budget,[&](uint8_t*,size_t){filled=true;return true;},error));assert(!filled&&upload.size()==5*MiB&&upload.capacity()==before);
  assert(!suitecut::AppendNativeHTTPUpload(upload,4*MiB,before,8*MiB,budget,[&](uint8_t*,size_t){filled=true;return true;},error));assert(!filled&&upload.size()==5*MiB);
  assert(!suitecut::AppendNativeHTTPUpload(upload,1,before,8*MiB,budget,[](uint8_t*,size_t){return false;},error));assert(upload.size()==5*MiB&&upload.capacity()==before);
  assert(!suitecut::AppendNativeHTTPUpload(upload,1,before-1,8*MiB,budget,[](uint8_t*,size_t){return true;},error));assert(upload.size()==5*MiB);
  std::vector<uint8_t>().swap(upload);
  suitecut::NativeHTTPUploadPreparations leases;
  assert(leases.Reserve("actual-token",{7,2,11,100},1));
  assert(!leases.Find("actual-token",3));auto entry=leases.Find("actual-token",2);assert(entry&&entry->owner==7&&entry->generation==11&&entry->deadline==100);
  assert(!leases.Reserve("actual-token",{8,2,12,100},2));assert(!leases.Reserve("other-token",{7,2,12,100},2));assert(leases.Size()==1);
  leases.Cancel(7);assert(!leases.Find("actual-token",2));assert(leases.Reserve("actual-token",{8,3,12,200},2));assert(!leases.Find("actual-token",2));assert(leases.Find("actual-token",3)->generation==12);
  for(int n=0;n<15;++n){assert(leases.Reserve("capacity"+std::to_string(n),{100+n,3,uint64_t(100+n),0},2));}
  assert(leases.Size()==16);assert(!leases.Reserve("excess",{999,3,999,0},2));
  for(int n=0;n<15;++n){leases.Cancel(100+n);}
  leases.Cancel(8);assert(leases.Size()==0);
  assert(!leases.Reserve("expired",{1,1,1,2},2));assert(!leases.Reserve("invalid",{1,1,1,std::numeric_limits<double>::quiet_NaN()},1));assert(!leases.Reserve("invalid",{1,1,1,100},std::numeric_limits<double>::infinity()));
  for(int cycle=0;cycle<100000;++cycle){assert(leases.Reserve("reused",{1,2,uint64_t(cycle)+1,100},1));const auto actual=leases.Find("reused",2);assert(actual&&actual->generation==uint64_t(cycle)+1&&actual->deadline==100);leases.Cancel(1);assert(leases.Size()==0);}
  std::cout<<"Native multipart preparation original owner/context/generation/deadline correlation, replacement, finite admission, capacity and100000 cancellation cycles passed\n";
}
