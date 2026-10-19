// SPDX-License-Identifier: MIT
#pragma once
#include "project_selection.h"
#include "runner_report.h"
#include "suite_file_owner.h"
#include "output_directory_options.h"
#include "repeat_configuration.h"
#include "test_group_tree.h"

namespace suitecut::runner {
// Declarations only. This view neither owns nor admits author callbacks.
struct TestListingCatalog {
  struct Test {
    uint64_t id=0,timeout_ms=0;unsigned retries=0;bool expected_failure=false;
    std::string title_json;std::vector<std::string> fixtures;
  };
  std::vector<Test> tests;
  std::map<uint64_t,std::string> grep_titles;
  std::vector<ProjectDeclaration> projects;
  std::vector<std::string> project_metadata;
  std::map<uint64_t,size_t> project_tests;
  std::vector<SuiteDeclaration> suites;
  std::vector<SuiteTestDeclaration> members;
  std::map<uint64_t,std::string> suite_titles;
  std::map<uint64_t,SuiteModifiers> suite_modifiers;
  std::map<uint64_t,std::string> skipped;
  std::vector<uint64_t> focused;
  bool has_suite_focus=false;
  // The exact, bounded collection transcript also retains declared fixture,
  // hook, parallel-mode and lock facts which are not selection fields.
  std::string protocol;
  std::map<size_t,std::string> project_output_roots={};
  NativeRepeatDeclarations repeat_declarations={};
  std::optional<bool> fully_parallel=std::nullopt;std::map<size_t,bool> project_parallel={};std::map<uint64_t,WorkGroupMode> suite_modes={};
};
struct TestListingSelection {
  std::set<uint64_t> tests;
  std::vector<std::string> project_roles;
  std::vector<std::vector<uint64_t>> project_tests;
};
inline TestListingSelection SelectListedTests(const TestListingCatalog& catalog,
    const TestSelection& selection,const std::vector<std::string>& projects={},bool no_dependencies=false){
  for(const auto& [project,root]:catalog.project_output_roots)if(project>=catalog.projects.size()||NativeOutputDirectoryRoot(root)!=root)throw std::invalid_argument("Native listing project output root rejected");
  std::vector<uint64_t> ids;for(const auto& test:catalog.tests)ids.push_back(test.id);
  TestListingSelection result;
  if(catalog.projects.empty()){
    if(!catalog.project_tests.empty()||!projects.empty()||no_dependencies)
      throw std::invalid_argument("Native project selector requires declared projects");
    for(const auto index:SelectTestIndices(ids,selection,catalog.focused,catalog.has_suite_focus))result.tests.insert(ids[index]);
  }else{
    if(catalog.project_metadata.size()!=catalog.projects.size()||catalog.project_tests.size()!=catalog.tests.size())
      throw std::invalid_argument("Native listing project metadata/membership incomplete");
    std::vector<ProjectTestDeclaration> declarations;
    for(const auto id:ids)declarations.push_back({id,catalog.project_tests.at(id)});
    const auto selected=SelectProjectTests(ProjectGraph(catalog.projects),declarations,selection,projects,no_dependencies,catalog.focused,
      ProjectSuiteFocus(catalog.projects.size(),catalog.suites,catalog.suite_modifiers));
    result.project_roles=selected.roles;result.project_tests=selected.tests;
    for(const auto& tests:selected.tests)result.tests.insert(tests.begin(),tests.end());
  }
  return result;
}
namespace listing_detail {
inline std::string Quote(std::string_view text){
  constexpr char h[]="0123456789abcdef";std::string result="\"";
  for(const unsigned char c:text){if(c=='"'||c=='\\'){result+='\\';result+=static_cast<char>(c);}else if(c<32){result+="\\u00";result+=h[c>>4];result+=h[c&15];}else result+=static_cast<char>(c);}
  result+='"';if(!JsonString(result,16384))throw std::invalid_argument("Native listing string must be bounded UTF8");return result;
}
inline void IDs(report_detail::AtomicFile& file,const auto& ids){bool first=true;file.Put("[");for(const auto id:ids){if(!first)file.Put(",");first=false;file.Put("\"");file.NumberValue(id);file.Put("\"");}file.Put("]");}
inline void Strings(report_detail::AtomicFile& file,const auto& values){bool first=true;file.Put("[");for(const auto& value:values){if(!first)file.Put(",");first=false;file.Put(Quote(value));}file.Put("]");}
}
// A failed collection/selection has selected:null, never a fabricated empty run.
// Workers are actual collection process receipts, not execution workers.
inline void WriteTestListing(const std::filesystem::path& output,const std::string& path,
    const SuiteFileToken& identity,const TestListingCatalog* catalog,const TestListingSelection* selected,
    const TestSelection& filter,const std::vector<std::string>& projects,bool no_dependencies,
    const std::vector<ReportWorker>& workers,const std::vector<std::string>& errors,
    const std::optional<std::string>& grep=std::nullopt,const std::optional<std::string>& grep_invert=std::nullopt,bool pass_with_no_tests=false,NativeReportPublication* publication=nullptr){
  using namespace listing_detail;
  report_detail::AtomicFile file(output,publication);
  file.Put("{\"version\":1,\"kind\":\"suitecut-native-test-list\",\"selectionResolved\":");file.Put(selected?"true":"false");
  file.Put(",\"file\":{\"path\":");file.Put(Quote(path));file.Put(",\"id\":\"");file.NumberValue(identity.id);file.Put("\",\"device\":\"");file.NumberValue(identity.device);file.Put("\",\"inode\":\"");file.NumberValue(identity.inode);file.Put("\"},\"selection\":{\"testIds\":");IDs(file,filter.ids);
  file.Put(",\"grep\":");file.Put(grep?Quote(*grep):"null");file.Put(",\"grepInvert\":");file.Put(grep_invert?Quote(*grep_invert):"null");
  file.Put(",\"passWithNoTests\":");file.Put(pass_with_no_tests?"true":"false");
  file.Put(",\"projectPatterns\":");Strings(file,projects);file.Put(",\"noDependencies\":");file.Put(no_dependencies?"true":"false");
  file.Put(",\"forbidOnly\":");file.Put(filter.forbid_only?"true":"false");file.Put(",\"shard\":{\"current\":");file.NumberValue(filter.shard_current);file.Put(",\"total\":");file.NumberValue(filter.shard_total);file.Put("}},\"selectedTestIds\":");if(selected){std::vector<uint64_t> ordered;for(const auto& test:catalog->tests)if(selected->tests.contains(test.id))ordered.push_back(test.id);IDs(file,ordered);}else file.Put("null");
  file.Put(",\"tests\":[");bool first=true;
  if(catalog)for(const auto& test:catalog->tests){
    if(!test.id||!JsonString(test.title_json,4096))throw std::invalid_argument("Invalid native listing test declaration");
    if(!first)file.Put(",");
    first=false;file.Put("{\"testId\":\"");file.NumberValue(test.id);file.Put("\",\"title\":");file.Put(test.title_json);if(catalog->grep_titles.contains(test.id)){file.Put(",\"grepTitle\":");file.Put(catalog->grep_titles.at(test.id));}
    file.Put(",\"selected\":");file.Put(selected?(selected->tests.contains(test.id)?"true":"false"):"null");
    file.Put(",\"projectId\":");if(const auto project=catalog->project_tests.find(test.id);project!=catalog->project_tests.end()){file.Put("\"");file.NumberValue(project->second+1);file.Put("\"");}else file.Put("null");
    const auto member=std::find_if(catalog->members.begin(),catalog->members.end(),[&](const auto& row){return row.id==test.id;});
    file.Put(",\"suiteId\":");if(member!=catalog->members.end()){file.Put("\"");file.NumberValue(member->suite);file.Put("\"");}else file.Put("null");
    file.Put(",\"timeoutMs\":");file.NumberValue(test.timeout_ms);file.Put(",\"retries\":");file.NumberValue(test.retries);file.Put(",\"expectedFailure\":");file.Put(test.expected_failure?"true":"false");
    file.Put(",\"focused\":");file.Put(std::find(catalog->focused.begin(),catalog->focused.end(),test.id)!=catalog->focused.end()?"true":"false");
    file.Put(",\"skipReason\":");if(const auto skip=catalog->skipped.find(test.id);skip!=catalog->skipped.end()){if(!JsonString(skip->second,8192))throw std::invalid_argument("Invalid native listing skip reason");file.Put(skip->second);}else file.Put("null");
    file.Put(",\"effectiveFixtures\":");Strings(file,test.fixtures);file.Put("}");
  }
  file.Put("],\"suites\":[");first=true;
  if(catalog)for(const auto& suite:catalog->suites){if(!first)file.Put(",");first=false;file.Put("{\"suiteId\":\"");file.NumberValue(suite.id);file.Put("\",\"parentId\":\"");file.NumberValue(suite.parent);file.Put("\",\"position\":");file.NumberValue(suite.position);file.Put(",\"title\":");const auto& title=catalog->suite_titles.at(suite.id);if(!JsonString(title,4096))throw std::invalid_argument("Invalid native listing suite title");file.Put(title);file.Put("}");}
  file.Put("],\"projects\":[");first=true;
  if(catalog)for(size_t index=0;index<catalog->projects.size();++index){const auto& project=catalog->projects[index];const auto& metadata=catalog->project_metadata.at(index);if(!ProjectMetadataJSON(metadata))throw std::invalid_argument("Invalid native listing project metadata");if(!first)file.Put(",");first=false;file.Put("{\"projectId\":\"");file.NumberValue(index+1);file.Put("\",\"name\":");file.Put(Quote(project.name));file.Put(",\"metadata\":");file.Put(metadata);if(const auto root=catalog->project_output_roots.find(index);root!=catalog->project_output_roots.end()){file.Put(",\"outputDir\":");file.Put(Quote(root->second));}file.Put(",\"defaultEnabled\":");file.Put(project.default_enabled?"true":"false");file.Put(",\"dependencies\":");Strings(file,project.dependencies);file.Put(",\"teardown\":");file.Put(project.teardown?Quote(*project.teardown):"null");file.Put(",\"selectionRole\":");file.Put(selected?Quote(selected->project_roles.at(index)):"null");file.Put(",\"selectedTestIds\":");if(selected)IDs(file,selected->project_tests.at(index));else file.Put("null");file.Put("}");}
  file.Put("],\"collectionProtocolUTF8Hex\":\"");if(catalog)file.Hex(catalog->protocol);file.Put("\",\"collectionWorkers\":[");first=true;
  for(const auto& worker:workers){if(!worker.worker_index||worker.pid<=0||worker.exit_code<0)throw std::invalid_argument("Native listing requires actual collection worker exit");if(!first)file.Put(",");first=false;file.Put("{\"workerIndex\":");file.NumberValue(worker.worker_index);file.Put(",\"pid\":");file.NumberValue(worker.pid);file.Put(",\"exitStatusKnown\":true,\"exitCode\":");if(worker.signaled)file.Put("null");else file.NumberValue(worker.exit_code);file.Put(",\"signal\":");if(worker.signaled)file.NumberValue(worker.exit_code);else file.Put("null");file.Put(",\"stdoutUTF8Hex\":\"");file.Hex(worker.stdout_bytes);file.Put("\",\"stderrUTF8Hex\":\"");file.Hex(worker.stderr_bytes);file.Put("\"}");}
  file.Put("],\"errors\":[");first=true;for(const auto& error:errors){if(!JsonString(error,8192))throw std::invalid_argument("Invalid native listing error JSON");if(!first)file.Put(",");first=false;file.Put(error);}file.Put("]}\n");file.Commit();
}
// Read-only inputs must never become the listing's atomic rename destination.
inline void CheckTestListingOutput(const std::string& output,const std::vector<std::string>& inputs){
  const auto destination=std::filesystem::weakly_canonical(output);
  for(const auto& input:inputs){const auto source=std::filesystem::canonical(input);std::error_code error;
    const bool same=std::filesystem::equivalent(output,source,error);
    if(destination==source||(!error&&same))throw std::invalid_argument("Native listing output aliases a collection input");
  }
}
}
