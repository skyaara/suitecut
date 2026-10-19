// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <thread>
#include "download_files.h"
#include "download_budget.h"

int main(){
  using namespace suitecut;
  const auto root=std::filesystem::temp_directory_path();
  auto files=std::make_shared<DownloadFiles>();const auto directory=files->directory();
  struct stat info{};assert(!stat(directory.c_str(),&info));assert((info.st_mode&0777)==0700);
  const std::string bytes("\0\xff\x80\n",4);
  {std::ofstream file(files->path(),std::ios::binary);file.write(bytes.data(),bytes.size());}
  assert(files->Complete()==4);assert(files->DiskBytes()==4);assert(!stat(files->path().c_str(),&info));assert((info.st_mode&0777)==0600);
  assert(!files->HasOpenDescriptor());
  {DownloadDescriptor fd(open(files->path().c_str(),O_RDONLY));assert(fd.get()>=0);assert(files->HasOpenDescriptor());}
  assert(!files->HasOpenDescriptor());
  const auto destination=directory+"/nested-測試/é-copy.bin";std::atomic<bool> canceled{false};
  auto result=CopyDownloadFile(files->path(),destination,canceled,0,[]{return 0.;});assert(result.ok&&result.bytes==4);
  {std::ifstream file(destination,std::ios::binary);const std::string actual((std::istreambuf_iterator<char>(file)),{});assert(actual==bytes);}
  canceled=true;result=CopyDownloadFile(files->path(),destination,canceled,0,[]{return 0.;});assert(!result.ok&&result.error.find("canceled")!=std::string::npos);
  canceled=false;result=CopyDownloadFile(files->path(),destination,canceled,10,[]{return 10.;});assert(!result.ok&&result.error.find("Timeout")!=std::string::npos);
  {std::ifstream file(destination,std::ios::binary);const std::string actual((std::istreambuf_iterator<char>(file)),{});assert(actual==bytes);}
  const auto symlink=directory+"/source-link";assert(!::symlink(files->path().c_str(),symlink.c_str()));
  assert(files->DiskBytes()==4);
  assert(!CopyDownloadFile(symlink,destination,canceled,0,[]{return 0.;}).ok);
  assert(!CopyDownloadFile(files->path(),"relative.bin",canceled,0,[]{return 0.;}).ok);
  bool escaped=false;try{files->Adopt("/tmp/unowned-download");}catch(const std::runtime_error&){escaped=true;}assert(escaped);
  for(const auto& entry:std::filesystem::directory_iterator(std::filesystem::path(destination).parent_path()))assert(!entry.path().filename().string().starts_with(".suitecut-download-save-"));
  auto quota=std::make_shared<DownloadQuota>(64);
  {auto lease=std::make_shared<DownloadQuotaLease>(quota);assert(lease->Add(64));assert(!lease->Add(1));assert(!lease->ReduceTo(65));assert(quota->Used()==64);assert(lease->ReduceTo(4));assert(quota->Used()==4);}
  assert(quota->Used()==0);
  std::vector<std::thread> workers;std::vector<std::shared_ptr<DownloadQuotaLease>> leases(32);
  for(size_t i=0;i<leases.size();++i)workers.emplace_back([&,i]{auto lease=std::make_shared<DownloadQuotaLease>(quota);lease->Add(4);leases[i]=lease;});
  for(auto& worker:workers){worker.join();}
  assert(quota->Used()<=64);leases.clear();assert(quota->Used()==0);
  files.reset();assert(!std::filesystem::exists(directory));
  for(int i=0;i<10000;++i){
    auto owner=std::make_shared<DownloadFiles>(root);const auto path=owner->directory();
    {std::ofstream output(owner->path(),std::ios::binary);output<<"owned native file";}
    assert(owner->Complete()==17);owner.reset();assert(!std::filesystem::exists(path));
  }
}
