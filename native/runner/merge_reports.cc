// SPDX-License-Identifier: MIT
#include "report_merge.h"
#include <iostream>

int main(int argc,char** argv){
  using namespace suitecut::runner;
  try{
    std::vector<ReportMergeInput> inputs;ReportMergeOptions options;
    std::filesystem::path json,junit,html;bool fail_on_run_failure=false;
    const auto next=[&](int& index){if(++index>=argc)merge_detail::Invalid("missing option value");return std::string(argv[index]);};
    const auto set_output=[&](std::filesystem::path& path,std::string value){if(!path.empty()||value.empty())merge_detail::Invalid("duplicate or empty output option");path=std::move(value);};
    for(int index=1;index<argc;++index){const std::string_view flag=argv[index];
      if(flag=="--help"){
        std::cout<<"Usage: suitecut-merge-reports --output MERGED.json [--junit MERGED.xml] [--html MERGED.html]\n"
          "       [--expected-inputs N] [--artifacts references|require] [--fail-on-run-failure] [--fail-on-flaky-tests]\n"
          "       --input SOURCE_ID REPORT.json [--input SOURCE_ID REPORT.json ...]\n"
          "Preserves original reports and absolute artifact references; copies no files.\n"
          "Source IDs qualify local identities, independently of project names or shard labels.\n"
          "Exit: 0 merge published; 1 source failed with --fail-on-run-failure or explicit flaky policy failed; 2 merge/IO error.\n";return 0;
      }
      if(flag=="--output")set_output(json,next(index));
      else if(flag=="--junit")set_output(junit,next(index));
      else if(flag=="--html")set_output(html,next(index));
      else if(flag=="--input"){const auto id=next(index);inputs.push_back({id,next(index)});}
      else if(flag=="--expected-inputs"){const auto text=next(index);size_t count=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),count);if(options.expected_inputs||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!count||count>128)merge_detail::Invalid("invalid/duplicate expected input count");options.expected_inputs=count;}
      else if(flag=="--artifacts"){const auto policy=next(index);if(policy!="references"&&policy!="require")merge_detail::Invalid("unknown artifact policy");options.require_artifacts=policy=="require";}
      else if(flag=="--fail-on-flaky-tests"){if(options.fail_on_flaky_tests)merge_detail::Invalid("duplicate fail-on-flaky option");options.fail_on_flaky_tests=true;}
      else if(flag=="--fail-on-run-failure")fail_on_run_failure=true;
      else merge_detail::Invalid("unknown option "+std::string(flag));
    }
    if(json.empty())merge_detail::Invalid("--output is required");
    auto report=MergedRunnerReport::Read(inputs,options);
    std::vector<std::filesystem::path> outputs;for(const auto& path:{json,junit,html})if(!path.empty()){
      report.CheckOutput(path);const auto canonical=std::filesystem::weakly_canonical(std::filesystem::absolute(path));
      for(const auto& previous:outputs){std::error_code error;if(canonical==std::filesystem::weakly_canonical(std::filesystem::absolute(previous))||(std::filesystem::equivalent(path,previous,error)&&!error))merge_detail::Invalid("output paths overlap");}
      outputs.push_back(path);
    }
    report.WriteJSON(json);if(!junit.empty())report.WriteJUnit(junit);if(!html.empty())report.WriteHTML(html);
    std::cout<<"Published native merged JSON";if(!junit.empty())std::cout<<", JUnit";if(!html.empty())std::cout<<", HTML";std::cout<<"; source run outcome: "<<(report.RunFailed()?"failed":"passed")<<"\n";
    report.WritePolicyConsole(std::cout);
    return (fail_on_run_failure&&report.RunFailed())||report.FlakyPolicyFailed()?1:0;
  }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 2;}
}
