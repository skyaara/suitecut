// SPDX-License-Identifier: MIT
#pragma once

#include <cerrno>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

// These functions run in the engine's isolated DOM world. Native C++ owns
// resolution, deadlines, retries and trusted checkbox/radio pointer dispatch.
inline constexpr const char* kSuiteCutFormInspect = R"JS((nodes,action,checked,force)=>{
  if(nodes.length>1)throw new Error('Strict locator matched multiple elements');
  if(!nodes.length)return {ready:false,reason:'element to be attached'};
  let e=nodes[0];
  if(e instanceof HTMLLabelElement&&e.control)e=e.control;
  if(action!=='setInputFiles'&&!e.isConnected)return {ready:false,reason:'element to be attached'};
  if(action==='setChecked'){
    const native=e instanceof HTMLInputElement&&['checkbox','radio'].includes(e.type),role=e.getAttribute('role'),aria=['checkbox','radio','switch','menuitemcheckbox','menuitemradio','option','treeitem'].includes(role);
    if(!native&&!aria)throw new Error('Not a checkbox or radio button');
    const state=native?e.checked:e.getAttribute('aria-checked')==='true';
    if(state===checked)return {ready:true,done:true};
    if(native&&e.type==='radio'&&!checked)throw new Error('Cannot uncheck radio button');
    return {ready:true,done:false};
  }
  if(action==='selectOption'){
    if(!(e instanceof HTMLSelectElement))throw new Error('Element is not a <select> element');
    const r=e.getBoundingClientRect(),s=getComputedStyle(e);
    if(!force&&(!r.width||!r.height||s.visibility==='hidden'||s.visibility==='collapse'||s.display==='none'))return {ready:false,reason:'element to be visible'};
    if(!force&&(e.matches(':disabled')||e.closest('[aria-disabled="true"]')))return {ready:false,reason:'element to be enabled'};
    return {ready:true};
  }
  if(action==='setInputFiles'){
    if(!(e instanceof HTMLInputElement)||e.type!=='file')throw new Error('Node is not an HTMLInputElement of type file');
    return {ready:true,multiple:e.multiple,directory:e.webkitdirectory};
  }
  throw new Error('Unknown native form action');
})JS";

inline constexpr const char* kSuiteCutSelectOptions = R"JS((nodes,choices,force)=>{
  const state=(INSPECT)(nodes,'selectOption',false,force);if(!state.ready)return state;
  let e=nodes[0];if(e instanceof HTMLLabelElement&&e.control)e=e.control;
  const all=Array.from(e.options),selected=[];
  const match=(candidate,index,wanted)=>wanted instanceof HTMLOptionElement?candidate===wanted:
    (wanted.valueOrLabel===undefined||(candidate.value===wanted.valueOrLabel||candidate.label.trim()===wanted.valueOrLabel))&&
    (wanted.value===undefined||candidate.value===wanted.value)&&(wanted.label===undefined||candidate.label.trim()===wanted.label)&&(wanted.index===undefined||index===wanted.index);
  for(const wanted of choices)if(wanted instanceof Element&&!(wanted instanceof HTMLOptionElement))throw new Error('Element is not an <option> element');
  if(e.multiple){for(const wanted of choices){const matches=all.filter((candidate,index)=>match(candidate,index,wanted));if(!matches.length)return {ready:false,reason:'all specified options to be present'};for(const candidate of matches)if(!selected.includes(candidate))selected.push(candidate);}}
  else if(choices.length){const first=all.find((candidate,index)=>choices.some(wanted=>match(candidate,index,wanted)));if(!first)return {ready:false,reason:'specified option to be present'};selected.push(first);}
  if(!force&&selected.some(option=>option.matches(':disabled')))return {ready:false,reason:'option being selected is not enabled'};
  for(const candidate of all)candidate.selected=false;
  for(const candidate of selected)candidate.selected=true;
  if(!choices.length)e.selectedIndex=-1;
  e.dispatchEvent(new Event('input',{bubbles:true,composed:true}));
  e.dispatchEvent(new Event('change',{bubbles:true}));
  return {ready:true,values:Array.from(e.selectedOptions,option=>option.value)};
})JS";

inline std::string SuiteCutSelectScript() {
  std::string result=kSuiteCutSelectOptions;
  result.replace(result.find("INSPECT"),7,kSuiteCutFormInspect);
  return result;
}

// Scope-owned staging keeps uploaded bytes alive until Chromium acknowledges
// DOM.setFileInputFiles. Every path is removed on success, error or cancellation.
class SuiteCutUploadFiles {
 public:
  SuiteCutUploadFiles()=default;
  SuiteCutUploadFiles(const SuiteCutUploadFiles&)=delete;
  SuiteCutUploadFiles& operator=(const SuiteCutUploadFiles&)=delete;
  ~SuiteCutUploadFiles() { if(!directory_.empty()){std::error_code error;std::filesystem::remove_all(directory_,error);} }
  std::string Stage(const std::string& name,std::string_view bytes) {
    if(name.empty()||name=="."||name==".."||name.size()>255||name.find('/')!=std::string::npos||name.find('\\')!=std::string::npos||name.find('\0')!=std::string::npos)
      throw std::invalid_argument("Upload file name must be a basename");
    if(directory_.empty()){
      std::string pattern=(std::filesystem::temp_directory_path()/"suitecut-upload-XXXXXX").string();
      std::vector<char> buffer(pattern.begin(),pattern.end());buffer.push_back('\0');
      char* path=mkdtemp(buffer.data());if(!path)throw std::runtime_error("Cannot create upload directory");directory_=path;
    }
    // A separate child directory preserves duplicate upload basenames safely.
    auto folder=std::filesystem::path(directory_)/std::to_string(count_++);
    std::filesystem::create_directory(folder);
    const auto path=(folder/name).string();
    const int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)throw std::runtime_error("Cannot create upload file");
    size_t offset=0;
    while(offset<bytes.size()){
      const auto written=write(fd,bytes.data()+offset,bytes.size()-offset);
      if(written<0&&errno==EINTR)continue;
      if(written<=0){close(fd);throw std::runtime_error("Cannot write upload file");}
      offset+=static_cast<size_t>(written);
    }
    if(close(fd)!=0)throw std::runtime_error("Cannot close upload file");
    return path;
  }
  void Append(const std::string& path,std::string_view bytes) {
    // Only a path produced by this scope may be appended. Canonicalize its
    // parent, including our private numeric subdirectory, before opening.
    const auto parent=std::filesystem::path(path).parent_path();
    if(directory_.empty()||parent.parent_path()!=std::filesystem::path(directory_))throw std::invalid_argument("Upload append path belongs to another scope");
    const int fd=open(path.c_str(),O_WRONLY|O_APPEND|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)throw std::runtime_error("Cannot append upload file");
    size_t offset=0;while(offset<bytes.size()){
      const auto written=write(fd,bytes.data()+offset,bytes.size()-offset);
      if(written<0&&errno==EINTR)continue;
      if(written<=0){close(fd);throw std::runtime_error("Cannot append upload file");}
      offset+=static_cast<size_t>(written);
    }
    if(close(fd)!=0)throw std::runtime_error("Cannot close upload file");
  }
 private:
  std::string directory_;
  size_t count_=0;
};

// Chromium reads a File's path lazily, so successful staging must outlive the
// command. Retain it until input replacement, frame destruction or source exit.
class SuiteCutUploadStore {
 public:
  bool CanReplace(const std::string& key,size_t bytes) const {
    const auto found=files_.find(key);
    return (found!=files_.end()||files_.size()<1024)&&bytes<=kCapacity&&bytes<=kCapacity-(bytes_-(found==files_.end()?0:found->second.bytes));
  }
  void Replace(const std::string& key,const std::string& frame,size_t bytes,std::shared_ptr<SuiteCutUploadFiles> files) {
    const auto found=files_.find(key);if(found!=files_.end()){bytes_-=found->second.bytes;files_.erase(found);}
    if(files){files_.emplace(key,Entry{frame,bytes,std::move(files)});bytes_+=bytes;}
  }
  void DropFrame(const std::string& frame) {
    for(auto it=files_.begin();it!=files_.end();)if(it->second.frame==frame){bytes_-=it->second.bytes;it=files_.erase(it);}else ++it;
  }
  void Clear(){files_.clear();bytes_=0;}
  size_t Bytes() const{return bytes_;}
  size_t Inputs() const{return files_.size();}
 private:
  struct Entry { std::string frame;size_t bytes;std::shared_ptr<SuiteCutUploadFiles> files; };
  static constexpr size_t kCapacity=128*1048576;
  std::map<std::string,Entry> files_;
  size_t bytes_=0;
};

struct SuiteCutPendingUpload {
  std::shared_ptr<SuiteCutUploadFiles> files;
  std::vector<std::string> paths;
  std::vector<std::string> names,mime_types;
  std::vector<size_t> expected,received;
  size_t bytes=0;
  double expires=0, deadline=0;
  int active_owner=0;
  uint64_t active_generation=0;
};
