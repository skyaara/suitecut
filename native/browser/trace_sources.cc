// SPDX-License-Identifier: MIT
#include "trace_sources.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <chrono>
#include <limits>
#include "openssl/evp.h"
namespace suitecut {
namespace {struct SourceFile {int fd;~SourceFile(){if(fd>=0)close(fd);}};}
struct TraceSources::Work {TraceJournal::Lease lease;std::string path,name,error;std::vector<uint8_t> bytes;uint64_t id;std::chrono::steady_clock::time_point deadline;std::atomic_bool done=false,canceled=false;};
bool TraceSources::Capture(TraceJournal::Lease lease,const std::string& path){
  if(!lease.context||!lease.session||path.empty()||path.size()>4096||path.find('\0')!=std::string::npos||next_>=9007199254740991ULL)return false;
  if(!cache_.contains(lease.context)&&cache_.size()>=128){return false;}auto& cache=cache_[lease.context];if(cache.session!=lease.session||cache.chunk!=lease.chunk){cache.session=lease.session;cache.chunk=lease.chunk;cache.files.clear();}if(cache.files.contains(path))return true;
  for(const auto& [id,work]:pending_)if(work->lease.context==lease.context&&work->lease.session==lease.session&&work->lease.chunk==lease.chunk&&work->path==path)return true;
  if(cache.files.size()>=4096||pending_.size()>=4)return false;
  auto work=std::make_shared<Work>();work->lease=lease;work->path=path;work->id=++next_;work->deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);pending_[work->id]=work;const auto weak=weak_from_this();const auto post=hooks_.owner;
  bool submitted=false;try{submitted=hooks_.background([weak,post,work]{
    try{if(work->canceled||std::chrono::steady_clock::now()>=work->deadline)work->error="Native trace source capture canceled or expired";SourceFile file{work->error.empty()?open(work->path.c_str(),O_RDONLY|O_CLOEXEC|O_NONBLOCK):-1};const int input=file.fd;struct stat status{};if(input<0)work->error="Actual trace source file is unavailable: "+work->path;else{if(fstat(input,&status)!=0||!S_ISREG(status.st_mode))work->error="Actual trace source is not a readable regular file";else if(status.st_size<0||uint64_t(status.st_size)>8*1024*1024)work->error="Native trace source file byte capacity exceeded";else{std::vector<uint8_t> chunk(65536);while(!work->canceled){if(std::chrono::steady_clock::now()>=work->deadline){work->error="Native trace source read deadline expired";break;}const auto count=read(input,chunk.data(),chunk.size());if(count<0&&errno==EINTR)continue;if(count<0){work->error="Actual trace source file read failed";break;}if(!count)break;if(size_t(count)>8*1024*1024-work->bytes.size()){work->error="Native trace source file byte capacity exceeded";break;}work->bytes.insert(work->bytes.end(),chunk.begin(),chunk.begin()+count);}}}if(work->canceled)work->error="Native trace source capture canceled";
      if(work->error.empty()){unsigned char digest[EVP_MAX_MD_SIZE];unsigned length=0;if(!EVP_Digest(work->bytes.data(),work->bytes.size(),digest,&length,EVP_sha1(),nullptr)||length!=20)work->error="Native trace source digest failed";else{static const char hex[]="0123456789abcdef";std::string hash;for(unsigned i=0;i<length;++i){hash+=hex[digest[i]>>4];hash+=hex[digest[i]&15];}auto extension=std::filesystem::path(work->path).extension().string();if(extension.size()>16||extension.find_first_not_of(".abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")!=std::string::npos)extension=".txt";work->name="src/"+hash+extension;}}
    }catch(...){work->error="Native trace source capture failed";}work->done.store(true,std::memory_order_release);try{post([weak,id=work->id]{if(const auto self=weak.lock())self->Finish(id);});}catch(...){/* Native Tick drains failed owner posts. */}
  });}catch(...){submitted=false;}if(!submitted){pending_.erase(work->id);return false;}if(hooks_.wake)hooks_.wake();return true;
}
void TraceSources::Finish(uint64_t id){const auto found=pending_.find(id);if(found==pending_.end()||!found->second->done.load(std::memory_order_acquire))return;const auto work=found->second;pending_.erase(found);if(hooks_.wake)hooks_.wake();if(work->canceled)return;if(!work->error.empty()){hooks_.error(work->lease,work->error);return;}const auto cache=cache_.find(work->lease.context);if(cache!=cache_.end()&&cache->second.session==work->lease.session&&cache->second.chunk==work->lease.chunk)cache->second.files.insert(work->path);hooks_.captured(work->lease,work->name,std::move(work->bytes));}
bool TraceSources::Pending(int context) const{for(const auto& [id,work]:pending_)if(work->lease.context==context)return true;return false;}
void TraceSources::Tick(){std::vector<uint64_t> finished;for(const auto& [id,work]:pending_)if(work->done.load(std::memory_order_acquire))finished.push_back(id);for(const auto id:finished)Finish(id);}
void TraceSources::Closed(int context){cache_.erase(context);for(const auto& [id,work]:pending_)if(work->lease.context==context)work->canceled=true;}
void TraceSources::Clear(){cache_.clear();for(const auto& [id,work]:pending_)work->canceled=true;}
} // namespace suitecut
