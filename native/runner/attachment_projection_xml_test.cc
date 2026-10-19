// SPDX-License-Identifier: MIT
#include "runner_report.h"
#include "runner_html.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace suitecut::runner;
std::string ReadProjectionFile(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){
  assert(argc==2);const std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
  const StepOwner owner{1,1,0,1,1,1,getpid(),"body"};
  AttachmentDeclarations declarations;declarations.Open(owner,1000,0);
  const auto id=declarations.Declare(owner,1,"\"authored \\ud800 <& Ω\"","\"x\"",AttachmentDeclarations::Source::PathReference,"\"missing\\ud800<&\"",std::nullopt,1);
  AttachmentMutations native(owner);native.Open(owner,1000,0);const auto ref=native.AdmitDeclaration(declarations,id);
  std::vector<AttachmentMutations::Slot> slots(64,ref);slots[3]=std::nullopt;
  for(uint64_t index=0;index<32;++index)native.Apply(owner,index+2,index,slots,static_cast<double>(index+2));
  RunnerReport report;report.workers.push_back({1,getpid(),false,0,{},{}});
  report.attempts.push_back({1,1,0,1,getpid(),"\"controlled projection report\"","\"\"","passed",true,35});
  report.attachment_declarations=declarations.Records();UpsertAttachmentProjection(report.attachment_views,native,{});
  const auto original=AttachmentProjectionJSON(report.attachment_views);assert(original.size()>8192&&original.size()<AttachmentMutations::kMetadata);
  // This is the exact former production call. Its refusal proves the case
  // crosses the unrelated path bound rather than only exercising tiny output.
  bool refused=false;try{(void)AttachmentPathJSON(original);}catch(const std::invalid_argument&){refused=true;}assert(refused);
  const auto quoted=AttachmentProjectionQuotedJSON(original);assert(JsonString(quoted,AttachmentMutations::kMetadata));
  assert(report_detail::XML(quoted).sanitized==0);
  WriteRunnerJson(report,output/"large.json");WriteRunnerJUnit(report,output/"large.xml");WriteRunnerHTML(report,output/"large.html");
  const auto xml=ReadProjectionFile(output/"large.xml");assert(xml.find("native.attachmentViewsJSON")!=std::string::npos&&xml.find("&quot;")!=std::string::npos);
  // Actual accepted native rejection metadata may contain a genuine XML1.0
  // noncharacter. JSON keeps its bytes; XML preserves them in the existing hex
  // fallback instead of accepting a fabricated sanitized original.
  native.Reject(owner,34,native.Revision(),"\"original \\ud800 <& \\u0000 "+std::string("\xef\xbf\xbe")+"\"",35);
  UpsertAttachmentProjection(report.attachment_views,native,{});
  const auto invalid_xml_original=AttachmentProjectionJSON(report.attachment_views);
  assert(report_detail::XML(AttachmentProjectionQuotedJSON(invalid_xml_original)).sanitized==1);
  WriteRunnerJson(report,output/"noncharacter.json");WriteRunnerJUnit(report,output/"noncharacter.xml");
  const auto with_fallback=ReadProjectionFile(output/"noncharacter.xml");assert(with_fallback.find("native.attachmentViewsJSONUtf8Hex")!=std::string::npos);
  refused=false;try{(void)AttachmentProjectionQuotedJSON(std::string(AttachmentMutations::kMetadata+1,'x'));}catch(const std::length_error&){refused=true;}assert(refused);
  refused=false;try{(void)AttachmentProjectionQuotedJSON(std::string("\xff",1));}catch(const std::invalid_argument&){refused=true;}assert(refused);
  std::cout<<"PASS 4 controlled native projection JSON/XML groups: >8192-byte actual report writes, UTF16/UTF8/XML exact fallback, original section-size bound, invalid UTF8; no author/file-job receipt claim\n";
}
