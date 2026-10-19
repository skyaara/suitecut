// SPDX-License-Identifier: MIT
#pragma once
#include "test_steps.h"
#include <filesystem>
namespace suitecut::runner {
// Successful native artifacts only: file bytes and worker exit are observed by
// C++ before publication. Failed/canceled jobs never manufacture an artifact ref.
// Private callback-iteration identity. Public step_id keeps the original author owner.
struct AttachmentRetryProvenance {
  uint64_t assertion_id=0,invocation_nonce=0,iteration_step=0;
  bool Empty()const{return !assertion_id&&!invocation_nonce&&!iteration_step;}
  bool Complete()const{return assertion_id&&invocation_nonce&&iteration_step;}
  bool operator==(const AttachmentRetryProvenance&)const=default;
};
struct AttachmentRecord {
  uint64_t id=0,request=0;StepOwner owner;
  std::string name_json,content_type_json,source_kind,path;
  uint64_t bytes=0,device=0,inode=0;
  int64_t writer_pid=0;bool writer_signaled=false;int writer_exit=-1;
  double started=0,committed=0,received=0;
  uint64_t step_id=0;
  AttachmentRetryProvenance callback_retry{};
};
inline std::string AttachmentPathJSON(std::string_view path){
  static constexpr char digits[]="0123456789abcdef";
  std::string value="\"";
  for(const unsigned char c:path){
    if(c=='"'||c=='\\'){value+='\\';value+=static_cast<char>(c);}
    else if(c<32){value+="\\u00";value+=digits[c>>4];value+=digits[c&15];}
    else value+=static_cast<char>(c);
  }
  value+='"';
  if(!JsonString(value,8192))throw std::invalid_argument("Native attachment filesystem path is not bounded UTF8");
  return value;
}
// Used only for native default filename extensions, never to reconstruct names.
inline std::string AttachmentASCII(std::string_view checked){
  std::string value;
  for(size_t i=1;i+1<checked.size();++i){unsigned code=static_cast<unsigned char>(checked[i]);
    if(code=='\\'){
      const char kind=checked[++i];
      if(kind=='u'){code=0;for(unsigned j=0;j<4;++j)code=(code<<4)|static_cast<unsigned>(HexDigit(checked[++i]));}
      else if(kind=='b')code=8;else if(kind=='f')code=12;else if(kind=='n')code=10;else if(kind=='r')code=13;else if(kind=='t')code=9;else code=static_cast<unsigned char>(kind);
    }
    if(code>=128)return {};
    value+=static_cast<char>(code);
  }
  return value;
}
inline void ValidateNativeAttachment(const AttachmentRecord& record){
  const auto& o=record.owner;
  if(!record.callback_retry.Empty()&&!record.callback_retry.Complete())throw std::invalid_argument("Invalid native attachment callback retry identity");
  if(!record.id||!record.request||!o.test_id||!o.attempt_generation||!o.scope||!o.scope_generation||o.pid<=0||o.pid>INT32_MAX||record.writer_pid<=0||record.writer_pid>INT32_MAX||record.writer_signaled||record.writer_exit!=0||record.writer_pid==o.pid||!JsonString(record.name_json,4096)||!JsonString(record.content_type_json,1024)||record.bytes>16*1024*1024||!record.inode||record.path.empty()||record.path.front()!='/'||record.path.find('\0')!=std::string::npos||(record.source_kind!="path"&&record.source_kind!="string"&&record.source_kind!="bytes")||!std::isfinite(record.started)||!std::isfinite(record.committed)||!std::isfinite(record.received)||record.started<0||record.committed<record.started||record.received<record.committed||(o.callback_kind!="body"&&o.callback_kind!="beforeEach"&&o.callback_kind!="afterEach"&&o.callback_kind!="beforeAll"&&o.callback_kind!="afterAll")||(o.callback_kind!="body"&&!o.callback_token))throw std::invalid_argument("Invalid native attachment record/provenance");
  (void)AttachmentPathJSON(record.path);

}
} // namespace suitecut::runner
