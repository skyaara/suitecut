// SPDX-License-Identifier: MIT
#pragma once
#include "attachment_declarations.h"
#include <string_view>
namespace suitecut::runner {
// D is a Body declaration plus an authored path reference. Only body bytes
// enter the real writer; this value owns no filesystem or publication receipt.
struct AttachmentAppendSource {
  AttachmentDeclarations::Source source=AttachmentDeclarations::Source::Absent;
  std::optional<std::string> author_path_json;
  std::optional<uint64_t> expected_bytes;
};
inline AttachmentAppendSource ReadAttachmentAppendSource(std::string_view kind,uint64_t bytes,const std::string& path_json){
  if(!JsonString(path_json,8192))throw std::invalid_argument("Native append path metadata rejected");
  const bool body=kind=="B"||kind=="D",path=kind=="R"||kind=="D";
  if((kind!="N"&&!body&&!path)||(!body&&bytes)||bytes>16*1024*1024||(!path&&path_json!="\"\""))
    throw std::invalid_argument("Native append source shape is contradictory");
  return {body?AttachmentDeclarations::Source::Body:path?AttachmentDeclarations::Source::PathReference:AttachmentDeclarations::Source::Absent,
          path?std::make_optional(path_json):std::nullopt,body?std::make_optional(bytes):std::nullopt};
}
} // namespace suitecut::runner
