// SPDX-License-Identifier: MIT
#include "fixture_graph.h"
#include "fixture_catalog.h"
#include "fixture_timeout_catalog.h"
#include "fixture_timeout_runtime.h"
#include "fixture_default_timeouts.h"
#include "fixture_source_setup_clock.h"
#include "fixture_transition_time.h"
#include "fixture_result_duration.h"
#include "runner_global_console.h"
#include "terminal_reporter.h"
#include "fixture_declaration_sources.h"
#include "fixture_options.h"
#include "runner_retry_plan.h"
#include "host_process.h"
#include "json_text.h"
#include "runtime_skip.h"
#include "runner_report.h"
#include "attempt_error_attribution.h"
#include "runner_steps.h"
#include "runner_soft_assertions.h"
#include "runner_callback_retries.h"
#include "runner_attachments.h"
#include "runner_output_directories.h"
#include "output_directory_options.h"
#include "project_output_roots.h"
#include "output_retention_finalize.h"
#include "output_retention_files.h"
#include "runner_html.h"
#include "runner_interrupt.h"
#include "suite_deadline.h"
#include "hook_worker.h"
#include "descendant_owner.h"
#include "test_selection.h"
#include "test_listing.h"
#include "snapshot_paths.h"
#include "suite_discovery.h"
#include "directory_admission.h"
#include "repeat_configuration.h"
#include "repeat_selection.h"
#include "repeat_projection.h"
#include "directory_report.h"
#include "directory_source_coverage.h"
#include "report_merge.h"
#include "suite_discovery_config.h"
#include "grep_title.h"
#include "grep_configuration.h"
#include "grep_file.h"
#include "suite_worker_plan.h"
#include "project_plan.h"
#include "project_selection.h"
#include "project_pool.h"
#include "resource_lock_declarations.h"
#include "project_actor_completion.h"
#include "work_group_locks.h"
#include "file_group_locks.h"
#include "worker_reuse_inventory.h"
#include "runner_retained_sources.h"
#include "native_continuation.h"
#include "project_descendants.h"
#include <charconv>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <string_view>
#include <unistd.h>
#ifndef SUITECUT_NATIVE_COORDINATOR_TEST_HOOK
#define SUITECUT_NATIVE_COORDINATOR_TEST_HOOK(phase,pid,workers,files) ((void)0)
#endif
namespace sr = suitecut::runner;
namespace {
std::string Quote(std::string_view input) {
  static constexpr char h[]="0123456789abcdef";
  std::string out="\"";
  size_t units=0;
  for (size_t i=0;i<input.size()&&units<8192;++i) {
    const auto c=static_cast<unsigned char>(input[i]);
    ++units;
    if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
    else if(c<32){out+="\\u00";out+=h[c>>4];out+=h[c&15];}
    else if(c<128)out+=static_cast<char>(c);
    else {
      size_t length=0;if((c&0xe0)==0xc0)length=2;else if((c&0xf0)==0xe0)length=3;else if((c&0xf8)==0xf0)length=4;
      const auto part=input.substr(i,length);
      if(length&&i+length<=input.size()&&sr::JsonString(std::string("\"")+std::string(part)+"\"",2)){
        if(length==4&&++units>8192)break;
        out.append(part);i+=length-1;
      }else {out+="\\u00";out+=h[c>>4];out+=h[c&15];}
    }
  }
  return out+'"';
}
std::string Hex(std::string_view input){static constexpr char h[]="0123456789abcdef";std::string out;for(unsigned char c:input){out+=h[c>>4];out+=h[c&15];}return out.empty()?"-":out;}
uint64_t Integer(const std::string& value,uint64_t max=UINT64_MAX){uint64_t n=0;const auto result=std::from_chars(value.data(),value.data()+value.size(),n);if(value.empty()||result.ec!=std::errc{}||result.ptr!=value.data()+value.size()||n>max)throw std::runtime_error("Invalid native runner integer");return n;}
std::vector<std::string> Fields(const std::string& line){std::istringstream in(line);std::vector<std::string> fields;std::string word;while(in>>word)fields.push_back(word);return fields;}
void Size(const std::vector<std::string>& fields,size_t n){if(fields.size()!=n)throw std::runtime_error("Invalid native runner control shape");}
bool Name(std::string_view value){if(value.empty()||value.size()>256)return false;for(size_t i=0;i<value.size();++i){const char c=value[i];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||(i&&c>='0'&&c<='9')))return false;}return true;}
std::vector<std::string> Names(const std::string& value){std::vector<std::string> out;if(value=="-")return out;size_t start=0;for(;;){const auto next=value.find(',',start);const auto word=value.substr(start,next==std::string::npos?next:next-start);if(!Name(word)||out.size()>=256)throw std::runtime_error("Invalid native fixture dependency list");out.push_back(word);if(next==std::string::npos)break;start=next+1;}return out;}
std::string Scalar(const std::string& hex,size_t units){const auto text=sr::DecodeHex(hex,units*6+2);if(!sr::JsonString(text,units))throw std::runtime_error("Invalid native host JSON scalar");return text;}
sr::FixtureLiteral FixtureValue(const std::string& kind,const std::string& encoded){
  if(kind=="U"&&encoded=="-")return {};
  if(kind!="J")throw std::runtime_error("Invalid native fixture value wire kind");
  sr::FixtureLiteral value{sr::FixtureLiteral::Kind::JSON,sr::DecodeHex(encoded,sr::FixtureLiteral::kBytes)};
  if(!value.Valid())throw std::runtime_error("Invalid native fixture JSON literal");
  return value;
}
struct HookSpec {uint64_t callback;std::optional<uint64_t> timeout;std::vector<std::string> fixtures;};
sr::HookKind HookKind(const std::string& value){if(value=="beforeAll")return sr::HookKind::BeforeAll;if(value=="beforeEach")return sr::HookKind::BeforeEach;if(value=="afterEach")return sr::HookKind::AfterEach;if(value=="afterAll")return sr::HookKind::AfterAll;throw std::runtime_error("Unknown native hook metadata kind");}
struct NestedHookSpec { sr::HookKind kind; HookSpec spec; };
struct Metadata {sr::NativeRepeatDeclarations repeat_declarations;sr::NativeSnapshotDeclarations snapshot_paths;sr::NativeProjectOutputRoots project_output_roots;std::optional<size_t> grep_global_tag_count;std::vector<std::u16string> grep_global_tags;sr::GrepConfigDeclarations grep_config;std::map<std::pair<size_t,bool>,size_t> grep_expected;sr::FixtureDefaultTimeouts default_timeouts;std::map<uint64_t,std::optional<uint64_t>> declared_timeouts;sr::GrepTitleComponents grep_components;std::map<uint64_t,std::string> grep_titles;bool source_capability=false;std::optional<bool> fully_parallel;std::map<size_t,bool> project_parallel;std::map<uint64_t,sr::WorkGroupMode> suite_modes;sr::ResourceLockDeclarations::Names file_locks;sr::ResourceLockDeclarations::ByOwner test_locks,suite_locks;std::set<uint64_t> serial_suites;std::vector<sr::ProjectDeclaration> projects;std::vector<std::string> project_metadata;std::map<uint64_t,size_t> project_tests;std::map<uint64_t,sr::SuiteModifiers> suite_modifiers;bool has_suite_focus=false;std::vector<sr::SuiteDeclaration> suites;std::vector<sr::SuiteTestDeclaration> members;std::map<uint64_t,NestedHookSpec> nested_hooks;std::map<uint64_t,std::string> suite_titles;std::vector<sr::FixtureCatalog::Layer> fixture_layers;std::map<uint64_t,sr::FixtureValueDeclaration> fixture_value_declarations;std::vector<sr::FixtureOptionOverride> fixture_option_overrides;std::optional<sr::FixtureOptionCatalog> fixture_values;std::vector<sr::FixtureTimeoutDeclaration> fixture_timeout_declarations;std::optional<sr::FixtureTimeoutCatalog> fixture_timeouts;std::map<uint64_t,std::string> fixture_titles;sr::FixtureDeclarationSources fixture_sources;size_t fixture_declaration_count=0;std::optional<sr::FixtureCatalog> fixture_catalog;std::vector<sr::Fixture> fixtures;std::vector<sr::TestCase> tests;std::map<uint64_t,std::string> titles;std::map<uint64_t,uint64_t> timeouts;std::map<uint64_t,std::string> skipped;std::vector<uint64_t> focused;std::map<sr::HookKind,std::vector<HookSpec>> hooks;std::string signature;};
struct Options {sr::NativeTerminalReporter terminal_reporter=sr::NativeTerminalReporter::Console;sr::GrepPolicy grep;std::string test_directory;std::string node,host,suite,report,junit,html,output_directory;std::optional<sr::PreserveOutput> preserve_output;sr::TestSelection selection;std::vector<std::string> projects;bool no_dependencies=false,list=false,pass_with_no_tests=false,fail_on_flaky_tests=false;unsigned workers=1;unsigned max_failures=0;uint64_t global_timeout=0;std::optional<uint64_t> repeat_each;std::optional<sr::SuiteDiscoveryOptions> discovery;std::shared_ptr<const sr::DirectoryAdmission> directory_admission;};
class Runner;
struct RunState {
  sr::NativeRepeatSelectionUsage repeat_planning_usage;
  explicit RunState(const Options& options):directory_start(sr::DedicatedRealMilliseconds()),deadline(options.global_timeout,directory_start),source_leases(deadline.OriginalDeadline()),max_failures(options.max_failures){}
  // Preserve the serial budget origin: process/subreaper setup precedes the
  // one original suite clock. That setup inventories real /proc ownership.
  sr::ProjectDescendants descendants;sr::NativeInterruptLatch interrupt;double directory_start=0;sr::NativeSuiteDeadline deadline;sr::RetainedSourceLeases source_leases;sr::RunnerReport report;
  sr::SoftAssertions::Budget soft_budget;sr::TestSteps::Budget step_budget;sr::RunnerAttachments::Budget file_budget;sr::RunnerOutputDirectories::Budget output_budget;
  static constexpr size_t kEmergencyActors=sr::ProjectPool::kWorkers+2,kEmergencyFDs=5*(sr::ProjectPool::kWorkers+sr::RunnerAttachments::kPhysical);
  std::array<Runner*,kEmergencyActors> emergency_actors{};std::array<pollfd,kEmergencyFDs> emergency_fds{};
  std::exception_ptr callback_publication_failure;
  const std::string emergency_reason="Native coordinator exception stopped author work";std::optional<double> emergency_time;
  std::unique_ptr<sr::SuiteFileOwner> suite_file;uint64_t suite_file_sequence=0;
  uint64_t attempt_sequence=0;unsigned worker_sequence=0,active_workers=0,failures=0,max_failures;
  size_t directory_declarations=0,directory_protocol_bytes=0,directory_worker_bytes=0;
  size_t metadata_charge=0;std::optional<double> failure_time;std::string policy_reason="Native maximum failure count reached";bool stop_recorded=false;
  uint64_t Attempt(){if(attempt_sequence==UINT64_MAX)throw std::overflow_error("Native shared attempt generation exhausted");return ++attempt_sequence;}
  void Stop(const std::string& reason){if(!failure_time){failure_time=sr::DedicatedRealMilliseconds();policy_reason=reason;}}
  void Failure(unsigned count){failures+=count;if(max_failures&&failures>=max_failures&&!failure_time)failure_time=sr::DedicatedRealMilliseconds();}
};
class Runner {
 public:
  explicit Runner(Options options,std::shared_ptr<RunState> shared={}):shared_(shared?std::move(shared):std::make_shared<RunState>(options)),interrupt_(shared_->interrupt),options_(std::move(options)),suite_deadline_(shared_->deadline),report_(options_.directory_admission?directory_report_:shared_->report),steps_(sr::RunnerSteps::Hooks{[this](const auto& packet){if(host_&&!host_->Exited()&&!host_->ControlClosed())Send(packet);},[this](const auto& error){if(actual_error_.empty())actual_error_=steps_.PendingError().value_or(Quote(error));finish_ok_=false;finish_error_=error;KillHost();},[this](const auto&,const auto& error){if(actual_error_.empty())actual_error_=error;skip_callback_failed_=true;finish_ok_=false;finish_error_="Actual step callback failed after runtime skip";},[this](const auto& owner,uint64_t id,auto forward,double now){CallbackRetryStepFence(owner,id,std::move(forward),now);},[this](const auto& step){attachments_.StepChanged(step,FixtureNow());},[this](const auto& owner,uint64_t id,double now){return callback_retries_.CanSkip(owner,id)&&soft_.CanSkip(owner.callback_token,owner.scope,owner.scope_generation)&&attachments_.CanAcceptLocalSkip(owner,id,now);},[this](const auto& owner,uint64_t id,uint64_t request,double now){attachments_.AcceptLocalSkip(owner,id,request,now);},[this]{return FixtureNow();}},&shared_->step_budget),soft_(sr::RunnerSoftAssertions::Hooks{
      [this](const auto& packet){if(host_&&!host_->Exited()&&!host_->ControlClosed())Send(packet);},
      [this](const auto& record,double now){if(record.step)steps_.NoteSoftFailure(record.owner,record.step,record.id,record.error_json,now);if(record.owner.callback_token&&(!hook_worker_||!hook_worker_->NoteSoftFailure(record.owner.callback_token,record.owner.scope,record.owner.scope_generation,record.owner.test_id,now)))throw std::runtime_error("Native accepted soft failure has no actual associated phase");soft_cleanup_failed_|=record.cleanup;},
      [this](const auto& owner,const auto& error,bool infrastructure){soft_infrastructure_failed_|=infrastructure;soft_hard_failure_=true;soft_cleanup_failed_|=owner.callback_kind=="afterEach";if(soft_hard_error_.empty())soft_hard_error_=error;},
      [this](const auto& error){artifact_cleanup_failed_=true;finish_ok_=false;Global(Quote(error));KillHost();},
      [this](uint64_t id){return steps_.Find(id);}},&shared_->soft_budget),callback_retries_(CallbackRetryHooks(),shared_->step_budget),attachments_(std::filesystem::path(options_.report).parent_path(),sr::RunnerAttachments::Hooks{[this](const auto& packet){if(host_&&!host_->Exited()&&!host_->ControlClosed())Send(packet);},[this](const auto& error){if(actual_error_.empty())actual_error_=Quote(error);finish_ok_=false;finish_error_=error;artifact_cleanup_failed_=true;Global(Quote(error));KillHost();},[this](uint64_t id){return steps_.Find(id);},[this](const auto& owner,const auto& error){
        if(!attachments_.OwnsPendingView(owner))throw std::runtime_error("Native attachment publication cleanup lost its exact pending callback");
        if(actual_error_.empty())actual_error_=Quote(error);
        finish_ok_=false;finish_error_=error;artifact_cleanup_failed_=true;Global(Quote(error));
        if(host_&&host_->Pid()==owner.pid&&worker_index_==owner.worker_index){if(!host_->Exited())KillHost();}
        else Global(Quote("Native attachment publication cleanup has no matching live host; unrelated host preserved"));
      },[this](const auto& owner,uint64_t public_step,const auto& retry,double now,bool continuation){if(!callback_retries_.ValidateRetry(owner,public_step,retry,now,continuation))throw std::runtime_error("Native attachment retry provenance has no actual invocation");},[this](const auto& owner,const auto& retry,const auto& error,bool infrastructure){callback_retries_.FileFailure(owner,retry,error,infrastructure,FixtureNow());}},&shared_->file_budget),outputs_("/proc/self/exe",{[this](const auto& error){artifact_cleanup_failed_=true;finish_ok_=false;if(actual_error_.empty())actual_error_=Quote(error);Global(Quote(error));KillHost();}},shared_->output_budget,{&shared_->file_budget.physical,sr::RunnerAttachments::kPhysical}){
    if(options_.workers>1||options_.list||options_.directory_admission){emergency_close_.emplace(CloseWorker(true));auto found=std::find(shared_->emergency_actors.begin(),shared_->emergency_actors.end(),nullptr);if(found==shared_->emergency_actors.end())throw std::runtime_error("Native emergency actor registry capacity exceeded");*found=this;}
  }
  ~Runner(){if(shared_)for(auto& actor:shared_->emergency_actors)if(actor==this)actor=nullptr;}
  sr::NativeTask<int> RunSerial(){
    std::optional<std::string> caught;
    try {
      if(!options_.directory_admission&&!parallel_actor_&&!FileOwner()){
        if(shared_->suite_file_sequence==UINT64_MAX)throw std::runtime_error("Native suite file identity exhausted");
        FileOwner()=std::make_unique<sr::SuiteFileOwner>(++shared_->suite_file_sequence,options_.suite);options_.suite=FileOwner()->Path();provisional_serial_file_=true;
      }
      co_await StartWorker();
      if(repeat_promoted_){co_await CloseWorker();DirectoryPhysicalFence();repeat_collection_timing_->Complete(sr::DedicatedRealMilliseconds());co_return RunDirectory(true);}
      for(;;){
      const auto project_error_start=report_.global_errors_json.size();
      co_await ExecuteProject();
      if(!project_plan_)break;
      MarkUnrun(plan_->FailureLimitReached()?"Native maximum failure count reached":StopRequested()?StopReason():"Native project stopped before these declarations were admitted");
      FinishProject(project_error_start);
      if(StopRequested())project_plan_->Stop(StopReason());
      else if(options_.max_failures&&project_failures_>=options_.max_failures)project_plan_->Stop("Native maximum failure count reached");
      current_project_=project_plan_->Next();
      if(!current_project_)break;
      metadata_.reset();plan_.reset();suite_plan_.reset();co_await StartWorker();
      }
    } catch(const std::exception& error) {
      caught=error.what();Global(Quote(*caught));
    }
    if(caught){
      try{co_await CloseWorker(true);}catch(const std::exception& cleanup){Global(Quote(cleanup.what()));}
      if(active_&&std::none_of(report_.attempts.begin(),report_.attempts.end(),[this](const auto& attempt){return attempt.generation==active_->generation;})){
        const auto title=metadata_->titles.at(active_->test.id);const auto error_json=Quote(*caught);Charge(title.size()+error_json.size()+288);
        report_.attempts.push_back(sr::ReportAttempt{active_->test.id,active_->generation,active_->retry,worker_index_,last_pid_,title,error_json,"interrupted",false,AttemptChargedDuration(sr::DedicatedRealMilliseconds()),skip_reason_json_,ExpectedStatus(),expected_failure_reason_json_,annotations_.Values()});report_.attempts.back().secondary_errors=attempt_secondary_errors_;RecordAttemptWall();
      }
    }
    if(host_||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical())throw std::runtime_error("Report publication refused without confirmed physical process-tree exit");
    if(project_plan_){
      if(current_project_&&project_plan_->Results()[current_project_->index].state==sr::SerialProjectPlan::State::Running){
        if(project_worker_pid_)throw std::runtime_error("Native project physical worker exit remains unconfirmed");
        project_plan_->Complete(current_project_->generation,StopRequested()?sr::SerialProjectPlan::Outcome::Interrupted:sr::SerialProjectPlan::Outcome::Failed);
      }
      project_plan_->Stop(StopRequested()?StopReason():"Native project scheduling stopped before admission");
      FinalProjectReports();
    }
    if(metadata_)MarkUnrun(plan_&&plan_->FailureLimitReached()?"Native maximum failure count reached":StopRequested()?StopReason():"Native suite stopped before these declared tests were admitted");
    if(StopRequested()&&!stop_recorded_&&!(options_.directory_admission&&DirectoryCompletedExpected())){Global(Quote(StopReason()));stop_recorded_=true;}
    if((!soft_.IdleReady()||!callback_retries_.IdleReady()))throw std::runtime_error("Native soft publication still owns callback work");
    CollectAttachmentViews();CollectCallbackRetries();report_.soft_assertions=soft_.Records();report_.steps=steps_.Records();report_.attachment_declarations=attachments_.Declarations();report_.attachments=attachments_.Records();report_.outputs=outputs_.Records();
    co_return options_.directory_admission&&options_.directory_admission->defer_publication?DirectoryCompletedResult():Publish();
  }
  int Run(){
    if(options_.discovery)return RunDirectory();
    if(options_.directory_admission)PrepareDirectoryFile();
    return options_.directory_admission&&options_.directory_admission->collection?RunList():options_.list?RunList():options_.workers>1?RunParallel():Drive(RunSerial());
  }
 private:
  void TickDescendants(){shared_->descendants.Tick(sr::DedicatedRealMilliseconds());for(const auto& error:shared_->descendants.TakeErrors())Global(Quote(error),options_.workers==1);}
  bool DescendantsPhysical()const{return shared_->descendants.Pending(descendant_generation_);}
#include "fixture_timeout_binding.inc"
#include "runner_callback_retry_binding.inc"
#include "runner_retained_source_binding.inc"
  void Descriptors(std::vector<pollfd>& fds){SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("descriptor-allocation",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);if(host_&&!host_->Exited()){const auto actual=host_->PollDescriptors();fds.insert(fds.end(),actual.begin(),actual.end());}attachments_.ForEachPollDescriptor([&](pollfd fd){fds.push_back(fd);});outputs_.ForEachPollDescriptor([&](pollfd fd){fds.push_back(fd);});if(sources_)sources_->ForEachPollDescriptor([&](pollfd fd){fds.push_back(fd);});}
  void WaitDescriptors(std::vector<pollfd>& fds,double wake){
    const auto now=sr::DedicatedRealMilliseconds();if(const auto original=suite_deadline_.OriginalDeadline();original&&*original>now)wake=std::min(wake,*original);
    const auto timeout=static_cast<int>(std::ceil(std::clamp(wake-now,0.0,10.0)));SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("poll",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);const auto result=poll(fds.data(),fds.size(),timeout);
    if(result<0&&errno!=EINTR)throw std::runtime_error("Native project coordinator poll failed");
  }
  template<class T>T Drive(sr::NativeTask<T> task){try{std::vector<pollfd> fds;while(!task.Done()){task.Resume(continuation_);TickDescendants();if(!task.Done()){fds.clear();Descriptors(fds);WaitDescriptors(fds,continuation_.wake);}}return task.Result();}catch(...){if(options_.directory_admission&&(host_||shared_->active_workers||shared_->descendants.Active()))EmergencyDrain();throw;}}
  #include "runner_output_retention_binding.inc"
  #include "terminal_reporter_binding.inc"
  int Publish(){
    CallbackRetryPublicationFence();
    if(shared_->source_leases.Outstanding()||(!options_.directory_admission&&report_.retained_sources.size()!=shared_->source_leases.Records().size()))throw std::runtime_error("Native Source publication lacks actual complete physical receipt rows");
    if(options_.directory_admission)sr::ValidateDirectorySourceCoverage(shared_->source_leases,report_.workers,report_.retained_sources);
    FinalizeOutputs();
    if(FileOwner()&&FileOwner()->Open()){if(options_.directory_admission)FileOwner()->Validate();FileOwner()->Retire();}
    std::sort(report_.steps.begin(),report_.steps.end(),[](const auto& a,const auto& b){return a.id<b.id;});std::sort(report_.attachments.begin(),report_.attachments.end(),[](const auto& a,const auto& b){return a.id<b.id;});std::sort(report_.attachment_declarations.begin(),report_.attachment_declarations.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    report_.run_policy=sr::FailOnFlakyTestsPolicy::PublicRoot(options_.fail_on_flaky_tests,static_cast<bool>(options_.directory_admission));
    const auto summary=sr::report_detail::Validate(report_);const bool failed=!report_.global_errors_json.empty()||summary.unexpected;
    sr::NativeReportPublication publication;
    sr::WriteRunnerJson(report_,options_.report,TerminalPublication(publication));if(!options_.junit.empty())sr::WriteRunnerJUnit(report_,options_.junit);if(!options_.html.empty())sr::WriteRunnerHTML(report_,options_.html);
    if(options_.terminal_reporter==sr::NativeTerminalReporter::Console){
      sr::WriteRunnerGlobalConsole(std::cout,report_.global_errors_json);
      std::cout<<"Native runner: "<<report_.attempts.size()<<" actual attempts, "<<report_.workers.size()<<" actual workers\n";
      report_.run_policy.Console(std::cout,summary.flaky);
    }
    const int actual_exit=report_.run_policy.FinalExit(interrupt_.Signal()?128+interrupt_.Signal():failed?1:0,summary.flaky);
    EmitTerminalReport(publication,actual_exit);return actual_exit;
  }
  sr::NativeTask<> PrepareParallel(){catalog_only_=true;co_await StartWorker();catalog_only_=false;metadata_.reset();}
#include "project_output_binding.inc"
#include "repeat_directory_binding.inc"
#include "repeat_callback_binding.inc"
#include "runner_directory_segment.inc"
#include "runner_directory.inc"
#include "runner_directory_cohorts.inc"
  static sr::TestListingCatalog ListingCatalog(const Metadata& value){
    sr::TestListingCatalog result;
    for(const auto& test:value.tests)result.tests.push_back({test.id,value.timeouts.at(test.id),test.retries,test.expectedFailure,value.titles.at(test.id),test.fixtures});
    result.repeat_declarations=value.repeat_declarations;result.fully_parallel=value.fully_parallel;result.project_parallel=value.project_parallel;result.suite_modes=value.suite_modes;
    result.project_output_roots=value.project_output_roots.Declarations();result.grep_titles=value.grep_titles;result.projects=value.projects;result.project_metadata=value.project_metadata;result.project_tests=value.project_tests;
    result.suites=value.suites;result.members=value.members;result.suite_titles=value.suite_titles;result.suite_modifiers=value.suite_modifiers;
    result.skipped=value.skipped;result.focused=value.focused;result.has_suite_focus=value.has_suite_focus;result.protocol=value.signature;
    return result;
  }
  void CheckNonemptySelection(const Metadata& value)const{
    if(options_.pass_with_no_tests)return;
    auto unsharded=options_.selection;unsharded.shard_current=unsharded.shard_total=1;
    if(sr::SelectListedTests(ListingCatalog(value),unsharded,options_.projects,options_.no_dependencies).tests.empty())throw std::runtime_error("No tests found");
  }
  int RunList(){
    try{return RunListImpl();}catch(...){if(host_||shared_->active_workers||shared_->descendants.Active())EmergencyDrain();throw;}
  }
  int RunListImpl(){
    sr::CheckTestListingOutput(options_.report,{options_.suite,options_.host,options_.node});
    if(!FileOwner())FileOwner()=std::make_unique<sr::SuiteFileOwner>(++shared_->suite_file_sequence,options_.suite);
    options_.suite=FileOwner()->Path();const auto identity=FileOwner()->Token();
    std::optional<sr::TestListingCatalog> declarations;std::optional<sr::TestListingSelection> selected;
    try{Drive(PrepareParallel());declarations=ListingCatalog(*catalog_);if(!options_.directory_admission)selected=sr::SelectListedTests(*declarations,options_.selection,options_.projects,options_.no_dependencies);}
    catch(const std::exception& error){Global(Quote(error.what()));}
    try{Drive(CloseWorker(!report_.global_errors_json.empty()));}
    catch(const std::exception& error){Global(Quote(error.what()));if(host_||DescendantsPhysical())EmergencyDrain();}
    if(host_||shared_->active_workers||shared_->descendants.Active()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical())
      throw std::runtime_error("Native listing refused without actual collection process-tree retirement");
    if(StopRequested()&&!stop_recorded_){Global(Quote(StopReason()));stop_recorded_=true;}
    if(!options_.directory_admission&&report_.global_errors_json.empty()&&catalog_&&NeedsNativeRepeatCoordinator(*catalog_)){
      repeat_collection_timing_->Complete(sr::DedicatedRealMilliseconds());return RunDirectory(true);
    }
    repeat_collection_timing_.reset();
    // Selection policy must not mask actual collection protocol/EOF failures.
    // Resolve empty-selection status only after the collection owner retired.
    if(report_.global_errors_json.empty()&&catalog_){try{CheckNonemptySelection(*catalog_);}catch(const std::exception& error){Global(Quote(error.what()));}}
    if(!report_.global_errors_json.empty())selected.reset();
    if(options_.directory_admission){directory_catalog_=std::move(declarations);FileOwner()->Validate();FileOwner()->Retire();return interrupt_.Signal()?128+interrupt_.Signal():report_.global_errors_json.empty()?0:1;}
    FileOwner()->Validate();FileOwner()->Retire();
    sr::NativeReportPublication publication;
    sr::WriteTestListing(options_.report,options_.suite,identity,declarations?&*declarations:nullptr,selected?&*selected:nullptr,
      options_.selection,options_.projects,options_.no_dependencies,report_.workers,report_.global_errors_json,options_.grep.include_argument,options_.grep.exclude_argument,options_.pass_with_no_tests,TerminalPublication(publication));
    if(options_.terminal_reporter==sr::NativeTerminalReporter::Console)std::cout<<"Native listing: "<<(declarations?declarations->tests.size():0)<<" declarations, "<<(selected?selected->tests.size():0)<<" selected, no author callbacks admitted\n";
    const int actual_exit=interrupt_.Signal()?128+interrupt_.Signal():report_.global_errors_json.empty()?0:1;
    EmitTerminalReport(publication,actual_exit);return actual_exit;
  }
  void CollectAttachmentViews(){
    const auto& receipts=attachments_.PublicationViewReceipts();
    attachments_.ForEachAttachmentProjection([&](const auto& journal){sr::UpsertAttachmentProjection(report_.attachment_views,journal,receipts);});
  }
  void AppendRecords(){if((!soft_.IdleReady()||!callback_retries_.IdleReady()))throw std::runtime_error("Native soft report append still owns callback work");CollectAttachmentViews();CollectCallbackRetries();const auto& soft=soft_.Records();report_.soft_assertions.insert(report_.soft_assertions.end(),soft.begin(),soft.end());const auto& steps=steps_.Records();const auto& files=attachments_.Records();report_.steps.insert(report_.steps.end(),steps.begin(),steps.end());report_.attachments.insert(report_.attachments.end(),files.begin(),files.end());const auto& declarations=attachments_.Declarations();report_.attachment_declarations.insert(report_.attachment_declarations.end(),declarations.begin(),declarations.end());const auto outputs=outputs_.Records();report_.outputs.insert(report_.outputs.end(),outputs.begin(),outputs.end());}
  sr::NativeTask<> RunActor(){
    std::optional<std::string> caught;const auto errors=local_errors_;
    try{if(shared_->emergency_time)ThrowIfStopped();if(!host_)co_await StartWorker();else if(parallel_actor_&&!metadata_)ConfigureWorker(*catalog_);co_await ExecuteProject();}
    catch(const std::exception& error){caught=error.what();Global(Quote(*caught));}
    if(caught){
      try{co_await CloseWorker(true);}catch(const std::exception& error){Global(Quote(error.what()));}
      if(active_&&std::none_of(report_.attempts.begin(),report_.attempts.end(),[this](const auto& attempt){return attempt.generation==active_->generation;})){
        const auto title=metadata_->titles.at(active_->test.id),error=Quote(*caught);Charge(title.size()+error.size()+256);report_.attempts.push_back({active_->test.id,active_->generation,active_->retry,worker_index_,last_pid_,title,error,"interrupted",false,AttemptChargedDuration(sr::DedicatedRealMilliseconds()),skip_reason_json_,ExpectedStatus(),expected_failure_reason_json_,annotations_.Values()});report_.attempts.back().secondary_errors=attempt_secondary_errors_;RecordAttemptWall();
      }
    }
    if(host_||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical()||project_worker_pid_)throw std::runtime_error("Native parallel project lacks actual physical worker/file/descendant retirement");
    if(metadata_)MarkUnrun(StopRequested()?StopReason():"Native project stopped before these selected tests were admitted");
    AppendRecords();if(plan_)FinishProject(errors);else if(current_file_group_){
      if(StopRequested())file_group_pool_->Stop();
      if(!file_group_pool_->CanceledBeforeStart(*current_file_group_)&&(!file_group_pool_->StartedWorkerExited(*current_file_group_)||!file_group_pool_->Complete(*current_file_group_,StopRequested()?sr::FileGroupPool::Outcome::Interrupted:sr::FileGroupPool::Outcome::Failed)))throw std::runtime_error("Native failed file-group completion lacks exact cancellation or actual worker exit");
    }else if(current_group_){
      if(StopRequested())work_group_pool_->Stop(StopReason());
      if(!work_group_pool_->CanceledBeforeStart(*current_group_)&&(!work_group_pool_->StartedWorkerExited(*current_group_)||!work_group_pool_->Complete(*current_group_,StopRequested()?sr::WorkGroupPool::Outcome::Interrupted:sr::WorkGroupPool::Outcome::Failed)))throw std::runtime_error("Native failed work-group completion lacks exact cancellation or actual worker exit");
    }else{
      if(StopRequested())pool_->Stop(StopReason());
      if(sr::FinishUnplannedProject(*pool_,*current_project_,StopRequested()?sr::ProjectPool::Outcome::Interrupted:sr::ProjectPool::Outcome::Failed)==sr::ProjectActorCompletion::Rejected)throw std::runtime_error("Native failed project completion lacks exact canceled admission or actual worker exit");
    }
  }
  static std::vector<bool> ParallelFlags(const Metadata& value){
    std::vector<bool> flags(value.projects.size(),value.fully_parallel.value_or(false));for(const auto& [project,parallel]:value.project_parallel)flags.at(project)=parallel;return flags;
  }
  static bool HasTestParallel(const Metadata& value){
    const auto flags=ParallelFlags(value);return std::any_of(flags.begin(),flags.end(),[](bool flag){return flag;})||
      std::any_of(value.suite_modes.begin(),value.suite_modes.end(),[](const auto& item){return item.second==sr::WorkGroupMode::Parallel;});
  }
  static sr::TestWorkGroups GroupPlan(const Metadata& value,const sr::ProjectTestSelection& selected){
    std::vector<sr::WorkGroupTest> tests;for(const auto& test:value.tests)tests.push_back({test.id,value.project_tests.at(test.id)});
    return {std::move(tests),value.suites,value.members,value.suite_modes,selected.plan.selected,selected.tests,ParallelFlags(value)};
  }
  static void ValidateParallelModes(const Metadata& value){
    if(std::none_of(value.suite_modes.begin(),value.suite_modes.end(),[](const auto& item){return item.second==sr::WorkGroupMode::Parallel;}))return;
    std::vector<uint64_t> ids;for(const auto& test:value.tests)ids.push_back(test.id);
    (void)sr::NativeTestGroupTree(std::move(ids),value.suites,value.members,value.suite_modes);
  }
  static bool HasResourceLocks(const Metadata& value){return !value.file_locks.empty()||!value.test_locks.empty()||!value.suite_locks.empty();}
  static sr::ResourceLockDeclarations ResourceDeclarations(const Metadata& value){
    std::vector<uint64_t> ids;for(const auto& test:value.tests)ids.push_back(test.id);
    return {std::move(ids),value.suites,value.members,value.test_locks,value.suite_locks,value.file_locks};
  }
  void EmergencyFinalizeOwner(){
    if(host_||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical()||project_worker_pid_)throw std::runtime_error("Native emergency owner remains physically live");
    if(current_file_group_&&file_group_pool_){const auto& owner=*current_file_group_;if(file_group_pool_->Retired(owner)||file_group_pool_->CanceledBeforeStart(owner))return;
      if(!file_group_pool_->StartedWorkerExited(owner)||!file_group_pool_->Complete(owner,sr::FileGroupPool::Outcome::Interrupted))throw std::runtime_error("Native emergency file-group retirement lacks exact actual owner");
    }else if(current_group_&&work_group_pool_){const auto& owner=*current_group_;if(work_group_pool_->Retired(owner)||work_group_pool_->CanceledBeforeStart(owner))return;
      if(!work_group_pool_->StartedWorkerExited(owner)||!work_group_pool_->Complete(owner,sr::WorkGroupPool::Outcome::Interrupted))throw std::runtime_error("Native emergency work-group retirement lacks exact actual owner");
    }else if(current_project_&&pool_){const auto& owner=*current_project_;if(pool_->Retired(owner)||pool_->CanceledBeforeStart(owner))return;
      if(!pool_->StartedWorkerExited(owner)||!pool_->Complete(owner.generation,sr::ProjectPool::Outcome::Interrupted))throw std::runtime_error("Native emergency project retirement lacks exact actual owner");
    }
  }
  void EmergencyDrain(){
    if(!shared_->emergency_time)shared_->emergency_time=sr::DedicatedRealMilliseconds();
    const auto end=*shared_->emergency_time+6000;bool failed=false;
    // Registry, reason, descriptor storage and each force-close continuation
    // were created before its actual host. Keep original suspended author tasks
    // alive, but never resume them as a substitute for emergency cleanup.
    for(auto* actor:shared_->emergency_actors)if(actor){actor->continuation_.ready={};try{if(actor->current_file_group_&&actor->file_group_pool_)actor->file_group_pool_->Stop();else if(actor->current_group_&&actor->work_group_pool_)actor->work_group_pool_->Stop(shared_->emergency_reason);else if(actor->current_project_&&actor->pool_)actor->pool_->Stop(shared_->emergency_reason);}catch(...){failed=true;}}
    for(;;){bool done=true;
      for(auto* actor:shared_->emergency_actors)if(actor){
        try{if(!actor->emergency_close_)throw std::logic_error("Native emergency close continuation absent");if(!actor->emergency_close_->Done())actor->emergency_close_->Resume(actor->continuation_);if(actor->emergency_close_->Done())actor->emergency_close_->Result();else done=false;}
        catch(...){failed=true;done=false;}
      }
      try{shared_->descendants.Tick(sr::DedicatedRealMilliseconds());}catch(...){failed=true;}
      if(done&&!shared_->active_workers&&!shared_->descendants.Active()&&!shared_->file_budget.physical&&!shared_->step_budget.physical&&!shared_->source_leases.Outstanding()){
        for(auto* actor:shared_->emergency_actors)if(actor)actor->EmergencyFinalizeOwner();
        if(failed)throw std::runtime_error("Native coordinator emergency cleanup encountered an unconfirmed receipt");
        return;
      }
      const auto now=sr::DedicatedRealMilliseconds();if(now>=end)throw std::runtime_error("Native coordinator emergency six-second physical cleanup grace expired; publication refused");
      size_t count=0;const auto add=[&](pollfd fd){if(count>=shared_->emergency_fds.size())throw std::logic_error("Native emergency descriptor bound exceeded");shared_->emergency_fds[count++]=fd;};
      for(auto* actor:shared_->emergency_actors)if(actor){if(actor->host_)for(const auto& fd:actor->host_->PollDescriptors())add(fd);actor->attachments_.ForEachPollDescriptor(add);actor->outputs_.ForEachPollDescriptor(add);if(actor->sources_)actor->sources_->ForEachPollDescriptor(add);}
      // No dynamic descriptor vector, formatting, Global(), fresh suite clock,
      // new author callback or synthetic process/rollback acknowledgement.
      const auto timeout=static_cast<int>(std::ceil(std::clamp(end-now,0.0,10.0)));(void)::poll(shared_->emergency_fds.data(),count,timeout);
    }
  }
  int RunTestGroups(sr::ProjectGraph graph,const sr::ProjectTestSelection& selected){
    std::optional<sr::TestWorkGroups> groups;std::vector<std::vector<std::string>> lock_names;
    try{groups.emplace(GroupPlan(*catalog_,selected));if(HasResourceLocks(*catalog_))lock_names=sr::WorkGroupLockNames(*groups,ResourceDeclarations(*catalog_));}
    catch(const std::exception& error){Global(Quote(error.what()));Drive(CloseWorker(true));return Publish();}
    sr::WorkGroupPool pool(std::move(graph),selected.plan,std::move(*groups),options_.workers);
    struct Slot {
      std::unique_ptr<Runner> owned;Runner* actor;sr::WorkGroupPool::Admission admission{0,0,0,false};sr::NativeTask<> task;bool lease_owned=false;
      explicit Slot(Runner& value):actor(&value),task(actor->RunActor()){}
      explicit Slot(std::unique_ptr<Runner> value):owned(std::move(value)),actor(owned.get()),task(actor->RunActor()){}
    };
    std::vector<std::unique_ptr<Slot>> slots;std::unique_ptr<Slot> staged;
    std::vector<std::optional<sr::WorkGroupPool::Admission>> admitted(pool.Plan().Groups().size());
    std::vector<std::vector<unsigned>> completed_workers(pool.Plan().Groups().size());
    std::optional<sr::WorkGroupLocks> locks;
    try{
    slots.reserve(options_.workers);work_group_pool_=&pool;project_selected_tests_=selected.tests;
    for(size_t index=0;index<catalog_->projects.size();++index){const auto& declaration=catalog_->projects[index];sr::ReportProject report;report.id=index+1;report.name_json=Quote(declaration.name);report.metadata_json=catalog_->project_metadata[index];report.default_enabled=declaration.default_enabled;report.selected_test_ids=selected.tests[index];report.selection_role=selected.roles[index];report.dependencies_ignored=options_.no_dependencies;
      for(const auto dependency:pool.Graph().Dependencies(index))report.dependencies.push_back(dependency+1);
      if(declaration.teardown)report.teardown=pool.Graph().Index(*declaration.teardown)+1;
      for(const auto& [test,project]:catalog_->project_tests)if(project==index)report.test_ids.push_back(test);
      report_.projects.push_back(std::move(report));
    }
    if(!lock_names.empty())locks.emplace(pool,std::move(lock_names));
    const auto next=[&](){if(StopRequested())pool.Stop(StopReason());return locks?pool.Next([&](size_t index){return locks->CanAcquire(index);}):pool.Next();};
    const auto reserve=[&](Slot& slot,const sr::WorkGroupPool::Admission& admission){
      slot.admission=admission;slot.actor->current_group_=admission;admitted[admission.index]=admission;
      if(locks){SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("lock-admission",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);if(!locks->Acquire(admission))throw std::runtime_error("Native work-group lock reservation lost exact ready admission");slot.lease_owned=true;}
    };
    // Reuse the genuine catalog host. No second preflight host or new global
    // clock is introduced. Configuration runs inside the actor cleanup scope.
    slots.push_back(std::make_unique<Slot>(*this));auto& initial=slots.back();
    if(const auto first=next()){
      parallel_actor_=true;reserve(*initial,*first);
      if(!pool.WorkerStarted(*first,worker_index_,host_->Pid()))throw std::runtime_error("Native initial catalog work-group worker admission rejected");
      project_worker_pid_=host_->Pid();report_.projects[first->project].workers.push_back(worker_index_);group_workers_.push_back(worker_index_);
    }else {slots.clear();Drive(CloseWorker());}
    std::vector<pollfd> fds;
    while(!pool.Done()||!slots.empty()){
      if(StopRequested())pool.Stop(StopReason());
      if(shared_->descendants.CanEnroll())while(slots.size()<options_.workers){
        // Allocate the suspended actor before reserving an admission. No author
        // callback, PID, attempt or output root exists for this staged object.
        if(!staged){SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("actor-staging",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);auto actor=std::make_unique<Runner>(options_,shared_);actor->options_.fail_on_flaky_tests=false;actor->parallel_actor_=true;actor->work_group_pool_=&pool;actor->catalog_=catalog_;actor->project_selected_tests_=selected.tests;staged=std::make_unique<Slot>(std::move(actor));}
        const auto admission=next();if(!admission)break;
        reserve(*staged,*admission);slots.push_back(std::move(staged));
      }
      for(auto it=slots.begin();it!=slots.end();){auto& slot=**it;
        if(!slot.task.Done()){slot.task.Resume(slot.actor->continuation_);}if(slot.task.Done()){
          slot.task.Result();if(locks){if(!locks->Release(slot.admission))throw std::runtime_error("Native work-group lock release lacks exact physical retirement");slot.lease_owned=false;}
          completed_workers[slot.admission.index]=std::move(slot.actor->group_workers_);it=slots.erase(it);continue;
        }
        ++it;
      }
      TickDescendants();
      if(pool.Done()&&slots.empty())break;
      fds.clear();double wake=sr::DedicatedRealMilliseconds()+10;for(const auto& slot:slots){slot->actor->Descriptors(fds);wake=std::min(wake,slot->actor->continuation_.wake);}WaitDescriptors(fds,wake);
    }
    if(shared_->active_workers||shared_->descendants.Active()||shared_->file_budget.physical||shared_->step_budget.physical||shared_->source_leases.Outstanding()||pool.Physical())throw std::runtime_error("Native work-group report lacks all actual physical retirement receipts");
    if(locks&&locks->Active())throw std::runtime_error("Native work-group locks remain physically owned at report publication");
    for(size_t index=0;index<pool.Plan().Groups().size();++index){const auto& definition=pool.Plan().Groups()[index];sr::ReportWorkGroup group;group.id=index+1;group.project_id=definition.project+1;group.suite=definition.suite;group.mode=definition.mode;group.tests=definition.tests;group.workers=std::move(completed_workers[index]);
      if(admitted[index]){group.generation=admitted[index]->generation;if(pool.Retired(*admitted[index]))group.receipt="physicalRetired";else if(pool.CanceledBeforeStart(*admitted[index]))group.receipt="neverStarted";else throw std::runtime_error("Native work-group report has no exact terminal admission receipt");}
      else group.receipt="notAdmitted";
      using State=sr::WorkGroupPool::State;switch(pool.Groups()[index]){case State::Passed:group.status="passed";break;case State::Failed:group.status="failed";break;case State::Interrupted:group.status="interrupted";break;case State::Blocked:group.status="blocked";break;case State::NotRun:group.status="notRun";break;default:throw std::runtime_error("Native work-group report has no terminal outcome");}
      report_.work_groups.push_back(std::move(group));
    }
    if(locks)for(const auto& record:locks->Records())if(record&&!locks->Names(record->owner.index).empty()){
      sr::ReportResourceLockGroup group;group.project_id=record->owner.project+1;group.generation=record->owner.generation;group.work_group_id=record->owner.index+1;
      if(record->receipt==sr::WorkGroupLocks::Receipt::Active)throw std::runtime_error("Native group-lock report still has an active physical owner");
      group.receipt=record->receipt==sr::WorkGroupLocks::Receipt::PhysicalRetired?"physicalRetired":"neverStarted";group.names=locks->Names(record->owner.index);report_.resource_locks.push_back(std::move(group));
    }
    FinalProjectReports();if(StopRequested()&&!shared_->stop_recorded){Global(Quote(StopReason()));shared_->stop_recorded=true;}work_group_pool_=nullptr;return Publish();
    }catch(...){const auto original=std::current_exception();EmergencyDrain();
      if(locks){for(const auto& slot:slots)if(slot->lease_owned){if(!locks->Release(slot->admission))throw std::runtime_error("Native emergency group lock release lacks actual retirement");slot->lease_owned=false;}if(staged&&staged->lease_owned){if(!locks->Release(staged->admission))throw std::runtime_error("Native emergency staged group lock release lacks actual retirement");staged->lease_owned=false;}}
      std::rethrow_exception(original);
    }
  }
  int RunFileGroups(){
    std::vector<uint64_t> ids,selected;std::optional<sr::FileGroupPool> policy;
    try{
      if(!FileOwner()||!catalog_||!catalog_->projects.empty()||!catalog_->project_tests.empty()||!options_.projects.empty()||options_.no_dependencies)
        throw std::invalid_argument("Native anonymous file catalog/selector identity rejected");
      for(const auto& test:catalog_->tests)ids.push_back(test.id);
      for(const auto position:sr::SelectTestIndices(ids,options_.selection,catalog_->focused,catalog_->has_suite_focus))selected.push_back(ids[position]);
      sr::NativeTestGroupTree tree(ids,catalog_->suites,catalog_->members,catalog_->suite_modes);
      policy.emplace(sr::FileWorkGroups(FileOwner()->Token(),tree,selected,catalog_->fully_parallel.value_or(false)),options_.workers);
    }catch(const std::exception& error){Global(Quote(error.what()));Drive(CloseWorker(true));return Publish();}
    auto& pool=*policy;
    if(EligibleWorkerReuse(*catalog_,pool.Plan()))return RunReusableFileGroups(pool.Plan(),ids,selected);
    struct Slot {
      std::unique_ptr<Runner> owned;Runner* actor;sr::FileGroupPool::Admission admission{{},0,0};sr::NativeTask<> task;bool lease_owned=false;
      explicit Slot(Runner& value):actor(&value),task(actor->RunActor()){}
      explicit Slot(std::unique_ptr<Runner> value):owned(std::move(value)),actor(owned.get()),task(actor->RunActor()){}
    };
    std::vector<std::unique_ptr<Slot>> slots;std::unique_ptr<Slot> staged;
    std::vector<std::optional<sr::FileGroupPool::Admission>> admitted(pool.Plan().Groups().size());
    std::vector<std::vector<unsigned>> completed_workers(pool.Plan().Groups().size());std::optional<sr::FileGroupLocks> locks;
    try{
      slots.reserve(options_.workers);file_group_pool_=&pool;
      if(HasResourceLocks(*catalog_))locks.emplace(pool,sr::FileGroupLockNames(pool.Plan(),ResourceDeclarations(*catalog_)));
      const auto next=[&](){if(StopRequested())pool.Stop();return locks?pool.Next([&](size_t i){return locks->CanAcquire(i);}):pool.Next();};
      const auto reserve=[&](Slot& slot,const sr::FileGroupPool::Admission& owner){
        slot.admission=owner;slot.actor->current_file_group_=owner;admitted[owner.index]=owner;
        if(locks){SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("lock-admission",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);if(!locks->Acquire(owner))throw std::runtime_error("Native file-group lock lost exact ready admission");slot.lease_owned=true;}
      };
      slots.push_back(std::make_unique<Slot>(*this));
      if(const auto first=next()){
        parallel_actor_=true;reserve(*slots.back(),*first);
        if(!pool.WorkerStarted(*first,worker_index_,host_->Pid()))throw std::runtime_error("Native initial catalog file-group worker admission rejected");
        project_worker_pid_=host_->Pid();group_workers_.push_back(worker_index_);
      }else{slots.clear();Drive(CloseWorker());}
      std::vector<pollfd> fds;
      while(!pool.Done()||!slots.empty()){
        if(StopRequested())pool.Stop();
        if(shared_->descendants.CanEnroll())while(slots.size()<options_.workers){
          if(!staged){SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("actor-staging",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);auto actor=std::make_unique<Runner>(options_,shared_);actor->options_.fail_on_flaky_tests=false;actor->parallel_actor_=true;actor->file_group_pool_=&pool;actor->catalog_=catalog_;staged=std::make_unique<Slot>(std::move(actor));}
          const auto owner=next();if(!owner)break;reserve(*staged,*owner);slots.push_back(std::move(staged));
        }
        for(auto it=slots.begin();it!=slots.end();){auto& slot=**it;
          if(!slot.task.Done())slot.task.Resume(slot.actor->continuation_);
          if(slot.task.Done()){
            slot.task.Result();if(locks){if(!locks->Release(slot.admission))throw std::runtime_error("Native file-group lock release lacks exact physical retirement");slot.lease_owned=false;}
            completed_workers[slot.admission.index]=std::move(slot.actor->group_workers_);it=slots.erase(it);continue;
          }++it;
        }
        TickDescendants();if(pool.Done()&&slots.empty())break;
        fds.clear();double wake=sr::DedicatedRealMilliseconds()+10;for(const auto& slot:slots){slot->actor->Descriptors(fds);wake=std::min(wake,slot->actor->continuation_.wake);}WaitDescriptors(fds,wake);
      }
      if(shared_->active_workers||shared_->descendants.Active()||shared_->file_budget.physical||shared_->step_budget.physical||shared_->source_leases.Outstanding()||pool.Physical()||(locks&&locks->Active()))
        throw std::runtime_error("Native file-group report lacks actual physical retirement receipts");
      bool failed=false,interrupted=false,not_run=false;std::set<uint64_t> reported;
      for(const auto& row:report_.attempts)reported.insert(row.test_id);
      for(const auto& row:report_.unrun)reported.insert(row.test_id);
      for(const auto& row:report_.skipped)reported.insert(row.test_id);
      for(const auto& row:report_.runtime_skipped)reported.insert(row.test_id);
      for(size_t i=0;i<pool.Plan().Groups().size();++i){const auto& definition=pool.Plan().Groups()[i];sr::ReportFileWorkGroup group;
        group.id=i+1;group.file_id=pool.Plan().File().id;group.suite=definition.suite;group.mode=definition.mode;group.tests=definition.tests;group.workers=std::move(completed_workers[i]);
        if(admitted[i]){group.generation=admitted[i]->generation;if(pool.Retired(*admitted[i]))group.receipt="physicalRetired";else if(pool.CanceledBeforeStart(*admitted[i]))group.receipt="neverStarted";else throw std::runtime_error("Native file-group has no exact terminal admission receipt");}else group.receipt="notAdmitted";
        using State=sr::FileGroupPool::State;switch(pool.Group(i)){case State::Passed:group.status="passed";break;case State::Failed:group.status="failed";failed=true;break;case State::Interrupted:group.status="interrupted";interrupted=true;break;case State::NotRun:group.status="notRun";not_run=true;break;default:throw std::runtime_error("Native file-group has no terminal outcome");}
        for(const auto id:group.tests)if(reported.insert(id).second){if(group.status=="passed")throw std::runtime_error("Native passed file-group omitted a selected declaration");const auto reason=Quote(StopRequested()?StopReason():"Native file-group stopped before this selected declaration was admitted");Charge(catalog_->titles.at(id).size()+reason.size()+128);report_.unrun.push_back({id,catalog_->titles.at(id),reason});}
        report_.file_work_groups.push_back(std::move(group));
      }
      if(locks)for(const auto& record:locks->Records())if(record&&!locks->Names(record->owner.index).empty()){
        if(record->receipt==sr::FileGroupLocks::Receipt::Active)throw std::runtime_error("Native file lock report still has active physical owner");
        report_.file_resource_locks.push_back({record->owner.file.id,record->owner.index+1,record->owner.generation,record->receipt==sr::FileGroupLocks::Receipt::PhysicalRetired?"physicalRetired":"neverStarted",locks->Names(record->owner.index)});
      }
      const auto opened=std::chrono::duration<double,std::milli>(FileOwner()->Opened().time_since_epoch()).count();
      const auto retired=std::chrono::duration<double,std::milli>(FileOwner()->Retire().time_since_epoch()).count();
      report_.files.push_back({pool.Plan().File(),FileOwner()->Path(),failed?"failed":interrupted?"interrupted":not_run?"notRun":"passed",std::move(ids),std::move(selected),opened,retired});
      if(StopRequested()&&!shared_->stop_recorded){Global(Quote(StopReason()));shared_->stop_recorded=true;}file_group_pool_=nullptr;return Publish();
    }catch(...){const auto original=std::current_exception();EmergencyDrain();
      if(locks){for(const auto& slot:slots)if(slot->lease_owned){if(!locks->Release(slot->admission))throw std::runtime_error("Native emergency file lock lacks actual retirement");slot->lease_owned=false;}if(staged&&staged->lease_owned){if(!locks->Release(staged->admission))throw std::runtime_error("Native emergency staged file lock lacks actual retirement");staged->lease_owned=false;}}
      std::rethrow_exception(original);
    }
  }
#include "worker_reuse_runner.inc"
  int RunParallel(){try{return RunParallelImpl();}catch(...){if(!shared_->emergency_time)EmergencyDrain();throw;}}
  int RunParallelImpl(){
    report_.workers.reserve(8192);report_.projects.reserve(128);
    std::optional<sr::ProjectGraph> graph;std::optional<sr::ProjectTestSelection> selection;
    std::vector<std::vector<std::string>> lock_names;
    try{if(shared_->suite_file_sequence==UINT64_MAX)throw std::overflow_error("Native suite file identity exhausted");FileOwner()=std::make_unique<sr::SuiteFileOwner>(++shared_->suite_file_sequence,options_.suite);options_.suite=FileOwner()->Path();Drive(PrepareParallel());if(!catalog_->projects.empty()){graph.emplace(catalog_->projects);std::vector<sr::ProjectTestDeclaration> declarations;for(const auto& test:catalog_->tests)declarations.push_back({test.id,catalog_->project_tests.at(test.id)});selection=sr::SelectProjectTests(*graph,declarations,options_.selection,options_.projects,options_.no_dependencies,catalog_->focused,sr::ProjectSuiteFocus(catalog_->projects.size(),catalog_->suites,catalog_->suite_modifiers));if(HasResourceLocks(*catalog_)&&!HasTestParallel(*catalog_)){const auto declarations=ResourceDeclarations(*catalog_);for(const auto& ids:selection->tests)lock_names.push_back(declarations.Resolve(std::set<uint64_t>(ids.begin(),ids.end())));}}}
    catch(const std::exception& error){Global(Quote(error.what()));try{Drive(CloseWorker(true));}catch(const std::exception& cleanup){Global(Quote(cleanup.what()));}if(host_||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical())throw;return Publish();}
    if(NeedsNativeRepeatCoordinator(*catalog_)){Drive(CloseWorker());DirectoryPhysicalFence();repeat_collection_timing_->Complete(sr::DedicatedRealMilliseconds());return RunDirectory(true);}
    repeat_collection_timing_.reset();
    if(catalog_->projects.empty())return RunFileGroups();
    if(HasTestParallel(*catalog_))return RunTestGroups(std::move(*graph),*selection);
    const auto& selected=*selection;project_selected_tests_=selected.tests;
    sr::ProjectPool pool(std::move(*graph),selected.plan,options_.workers);
    struct Slot {std::unique_ptr<Runner> owned;Runner* actor;sr::ProjectPool::Admission admission{0,0,false};sr::NativeTask<> task;bool lease_owned=false;explicit Slot(Runner& value):actor(&value),task(actor->RunActor()){}explicit Slot(std::unique_ptr<Runner> value):owned(std::move(value)),actor(owned.get()),task(actor->RunActor()){}};
    std::vector<std::unique_ptr<Slot>> slots;std::unique_ptr<Slot> staged;std::optional<sr::ResourceLocks> locks;
    try{slots.reserve(options_.workers);pool_=&pool;
    for(size_t index=0;index<catalog_->projects.size();++index){const auto& declaration=catalog_->projects[index];sr::ReportProject report;report.id=index+1;report.name_json=Quote(declaration.name);report.metadata_json=catalog_->project_metadata[index];report.default_enabled=declaration.default_enabled;report.selected_test_ids=selected.tests[index];report.selection_role=selected.roles[index];report.dependencies_ignored=options_.no_dependencies;
      for(const auto dependency:pool.Graph().Dependencies(index))report.dependencies.push_back(dependency+1);
      if(declaration.teardown)report.teardown=pool.Graph().Index(*declaration.teardown)+1;
      for(const auto& [test,project]:catalog_->project_tests)if(project==index)report.test_ids.push_back(test);
      report_.projects.push_back(std::move(report));
    }
    if(!lock_names.empty())locks.emplace(pool,std::move(lock_names));
    const auto next=[&](){return locks?pool.Next([&](size_t index){return locks->CanAcquire(index);}):pool.Next();};
    const auto reserve=[&](Slot& slot,const auto& admission){slot.admission=admission;slot.actor->current_project_=admission;if(locks){SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("lock-admission",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);if(!locks->Acquire(admission))throw std::runtime_error("Native project resource-lock reservation lost exact ready admission");slot.lease_owned=true;}};
    slots.push_back(std::make_unique<Slot>(*this));
    if(const auto first=next()){
      reserve(*slots.back(),*first);parallel_actor_=true;current_project_=first;
      if(!pool.WorkerStarted(first->generation,worker_index_,host_->Pid()))throw std::runtime_error("Native initial catalog worker pool admission rejected");
      project_worker_pid_=host_->Pid();report_.projects[first->index].workers.push_back(worker_index_);
    }else {slots.clear();Drive(CloseWorker());}
    std::vector<pollfd> fds;
    while(!pool.Done()||!slots.empty()){
      if(StopRequested())pool.Stop(StopReason());
      if(shared_->descendants.CanEnroll())while(slots.size()<options_.workers){
        if(!staged){SUITECUT_NATIVE_COORDINATOR_TEST_HOOK("actor-staging",host_?host_->Pid():0,shared_->active_workers,shared_->file_budget.physical);auto actor=std::make_unique<Runner>(options_,shared_);actor->options_.fail_on_flaky_tests=false;actor->parallel_actor_=true;actor->pool_=&pool;actor->catalog_=catalog_;actor->project_selected_tests_=selected.tests;staged=std::make_unique<Slot>(std::move(actor));}
        const auto admission=next();if(!admission)break;reserve(*staged,*admission);slots.push_back(std::move(staged));
      }
      for(auto it=slots.begin();it!=slots.end();){auto& slot=**it;
        if(!slot.task.Done()){slot.task.Resume(slot.actor->continuation_);}if(slot.task.Done()){slot.task.Result();if(locks){if(!locks->Release(slot.admission))throw std::runtime_error("Native resource-lock release lacks exact actual project retirement");slot.lease_owned=false;}it=slots.erase(it);continue;}
        ++it;
      }
      TickDescendants();
      if(pool.Done()&&slots.empty())break;
      fds.clear();double wake=sr::DedicatedRealMilliseconds()+10;for(const auto& slot:slots){slot->actor->Descriptors(fds);wake=std::min(wake,slot->actor->continuation_.wake);}WaitDescriptors(fds,wake);
    }
    if(shared_->active_workers||shared_->descendants.Active()||shared_->file_budget.physical||shared_->step_budget.physical||shared_->source_leases.Outstanding())throw std::runtime_error("Native parallel report lacks all actual physical retirement receipts");
    if(locks&&locks->Active())throw std::runtime_error("Native resource locks remain physically owned at report publication");
    if(locks)for(const auto& record:locks->Records())if(record&&!locks->Names(record->owner.index).empty()){
      sr::ReportResourceLockGroup group;group.project_id=record->owner.index+1;group.generation=record->owner.generation;
      if(record->receipt==sr::ResourceLocks::Receipt::Active)throw std::runtime_error("Native lock report still has an active physical owner");
      group.receipt=record->receipt==sr::ResourceLocks::Receipt::PhysicalRetired?"physicalRetired":"neverStarted";group.names=locks->Names(record->owner.index);report_.resource_locks.push_back(std::move(group));
    }
    FinalProjectReports();if(StopRequested()&&!shared_->stop_recorded){Global(Quote(StopReason()));shared_->stop_recorded=true;}pool_=nullptr;return Publish();
    }catch(...){const auto original=std::current_exception();EmergencyDrain();
      if(locks){for(const auto& slot:slots)if(slot->lease_owned){if(!locks->Release(slot->admission))throw std::runtime_error("Native emergency project lock release lacks actual retirement");slot->lease_owned=false;}if(staged&&staged->lease_owned){if(!locks->Release(staged->admission))throw std::runtime_error("Native emergency staged project lock release lacks actual retirement");staged->lease_owned=false;}}
      std::rethrow_exception(original);
    }
  }
  sr::NativeTask<> ExecuteProject(bool retain_reusable_worker=false){
      const auto remaining_failures=!parallel_actor_&&options_.max_failures?options_.max_failures-project_failures_:0;
      plan_=std::make_unique<sr::RunnerRetryPlan>(metadata_->tests,remaining_failures,attempt_nonce_,SerialGroups());groups_reported_=false;
      if(!metadata_->suites.empty())co_await RunNestedHooks();
      else if(!metadata_->hooks.empty())co_await RunHooks();
      else {
      while(!plan_->Done()) {
        ThrowIfStopped();
        if(!host_)co_await StartWorker();
        const auto next=NextAttempt(static_cast<uint64_t>(host_->Pid()));
        if(!next)throw std::runtime_error("Native retry physical worker fence did not open");
        active_=*next;body_physical_=false;body_started_=false;finished_=false;timeout_=false;interrupted_=false;actual_error_.clear();attempt_secondary_errors_.clear();attempt_timing_.reset();fixture_mutation_failed_=false;fixture_mutation_error_.clear();originating_step_=0;primary_soft_error_=0;soft_hard_failure_=false;soft_cleanup_failed_=false;soft_infrastructure_failed_=false;soft_hard_error_.clear();callback_infrastructure_failed_=false;callback_error_.clear();finish_error_.clear();runtime_skip_=false;annotations_.Clear();before_all_skip_cause_=0;skip_callback_failed_=false;artifact_cleanup_failed_=false;skip_reason_json_.reset();runtime_expected_failure_=false;expected_failure_reason_json_.reset();
        const double start=sr::DedicatedRealMilliseconds();
        attempt_started_=start;
        deadline_=start+(metadata_->timeouts.at(next->test.id)?metadata_->timeouts.at(next->test.id):3153600000000.0);
        const bool output_ready=co_await PrepareOutput();
        if(output_ready){const auto admitted=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,admitted);
          // Preserve the original attempt origin, including native intervals
          // around output preparation and its actual readiness receipt.
          const auto charged=admitted-attempt_started_;
          attempt_timing_=std::make_shared<sr::FixtureAttemptClocks>(sr::FixtureAttemptClockOwner{next->test.id,next->generation,worker_index_,host_->Pid()},metadata_->timeouts.at(next->test.id),admitted,charged,charged);
          if(!owners_->Begin(next->test.id,next->generation,next->test.fixtures,deadline_,admitted,attempt_timing_))throw std::runtime_error("Native fixture ownership admission rejected");}
        while(!finished_||body_physical_) {
          co_await Pump();
          {const double now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
          TickFixtureClocks(now);ObserveStop(now);
          if(now>=deadline_&&!finished_) {
            timeout_=true;AbortBody("Test original deadline expired");
            owners_->Cancel("Test original deadline expired",now);
          }
          owners_->Tick(now);
          if(finished_&&(!finish_ok_||runtime_skip_)&&body_physical_){AbortBody("Test attempt failed");if(!body_cleanup_deadline_)body_cleanup_deadline_=now+1000;}
          if(body_cleanup_deadline_&&now>=body_cleanup_deadline_){if(runtime_skip_){finish_ok_=false;finish_error_="Runtime skip raw callback physical cleanup deadline expired";}KillHost();}
          }if(host_->Exited())co_await HandleExit();
        }
        if(host_control_failed_){finish_ok_=false;if(actual_error_.empty())actual_error_=host_control_error_;}
      ApplyFixtureMutationOutcome();ApplySoftOutcome();ApplyCallbackRetryOutcome();const auto status=AttemptOutcome();
        const auto worker_id=static_cast<uint64_t>(host_?host_->Pid():last_pid_);
        if(!plan_->Finish(worker_id,next->generation,status,finish_error_,(runtime_skip_&&status!=sr::Outcome::Skipped)||skip_callback_failed_||owners_->InfrastructureFailure()||artifact_cleanup_failed_||fixture_mutation_failed_||soft_cleanup_failed_||soft_infrastructure_failed_||callback_infrastructure_failed_||host_control_failed_))throw std::runtime_error("Native result admission rejected");
        AccountFailures();const auto& result=plan_->Results().back();
        if(status==sr::Outcome::Passed&&!result.expected&&std::string_view(ExpectedStatus())=="failed"){actual_error_=Quote("Expected to fail, but passed.");}
        if(report_.attempts.size()>=8192)throw std::runtime_error("Native attempt report capacity8192 exceeded");
        const auto error=actual_error_.empty()?Quote(finish_error_):actual_error_;
        Charge(metadata_->titles.at(next->test.id).size()+error.size()+288);
        report_.attempts.push_back(sr::ReportAttempt{next->test.id,next->generation,next->retry,worker_index_,static_cast<int64_t>(worker_id),metadata_->titles.at(next->test.id),error,AttemptStatus(),result.expected,AttemptChargedDuration(sr::DedicatedRealMilliseconds()),skip_reason_json_,ExpectedStatus(),expected_failure_reason_json_,annotations_.Values(),originating_step_,PrimarySoftError()});report_.attempts.back().secondary_errors=attempt_secondary_errors_;RecordAttemptWall();
        if(!finish_ok_||plan_->NeedsWorkerRetirement()) {
          if(reuse_pool_)CompleteReusableGroup();
          co_await CloseWorker();
          plan_->WorkerExited(worker_id,worker_index_);
        }
        active_.reset();body_cleanup_deadline_=0;
        if(StopRequested())break;
      }
      }
      if(!retain_reusable_worker||!host_||StopRequested()){
        if(reuse_pool_)CompleteReusableGroup();
        co_await CloseWorker();
      }
  }
  std::optional<sr::Attempt> NextAttempt(uint64_t pid){return plan_->Next(pid,(parallel_actor_||options_.directory_admission)?shared_->Attempt():0,worker_index_);}
  void AccountFailures(){if(parallel_actor_||options_.directory_admission){const auto count=plan_->UnexpectedTestFailures();shared_->Failure(count-failure_accounted_);failure_accounted_=count;}}
  void FinishProject(size_t error_start){
    if(!current_project_&&!current_group_&&!current_file_group_)return;
    if(host_||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical()||project_worker_pid_)throw std::runtime_error("Native project completion lacks physical process-tree exit");
    bool passed=(parallel_actor_?local_errors_:report_.global_errors_json.size())==error_start;std::map<uint64_t,bool> finals;
    for(const auto& result:plan_->Results()){finals[result.attempt.test.id]=result.expected;attempt_nonce_=std::max(attempt_nonce_,result.attempt.generation);}
    for(const auto& [id,expected]:finals){(void)expected;passed&=plan_->FinalExpected(id);}
    project_failures_+=plan_->UnexpectedTestFailures();
    bool actual_interrupted=false;for(const auto& result:plan_->Results())actual_interrupted|=result.outcome==sr::Outcome::Interrupted;
    const bool interrupted=parallel_actor_?(actual_interrupted||(StopRequested()&&!plan_->Done()&&passed)):StopRequested();
    const auto outcome=interrupted?sr::SerialProjectPlan::Outcome::Interrupted:passed?sr::SerialProjectPlan::Outcome::Passed:sr::SerialProjectPlan::Outcome::Failed;
    if(current_file_group_){const auto file_outcome=interrupted?sr::FileGroupPool::Outcome::Interrupted:passed?sr::FileGroupPool::Outcome::Passed:sr::FileGroupPool::Outcome::Failed;if(!file_group_pool_->Complete(*current_file_group_,file_outcome))throw std::runtime_error("Native file-group completion receipt rejected");}
    else if(current_group_){if(!work_group_pool_->Complete(*current_group_,outcome))throw std::runtime_error("Native work-group completion receipt rejected");}
    else if(!(pool_?pool_->Complete(current_project_->generation,outcome):project_plan_->Complete(current_project_->generation,outcome)))throw std::runtime_error("Native project completion receipt rejected");
  }
  void ProjectWorkerExited(){
    if(!project_worker_pid_)return;
    if(reuse_pool_){RetireReusableHost();return;}
    if(current_file_group_){
      if(!host_||host_->Pid()!=project_worker_pid_||!host_->Exited()||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical()||!file_group_pool_->WorkerExited(*current_file_group_,worker_index_,project_worker_pid_))throw std::runtime_error("Native file-group physical worker exit receipt rejected");
      project_worker_pid_=0;return;
    }
    if(current_group_){
      if(!host_||host_->Pid()!=project_worker_pid_||!host_->Exited()||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical()||!work_group_pool_->WorkerExited(*current_group_,worker_index_,project_worker_pid_))throw std::runtime_error("Native work-group physical worker exit receipt rejected");
      project_worker_pid_=0;return;
    }
    if(!host_||host_->Pid()!=project_worker_pid_||!host_->Exited()||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical()||!current_project_||!(pool_?pool_->WorkerExited(current_project_->generation,worker_index_,project_worker_pid_):project_plan_->WorkerExited(current_project_->generation,project_worker_pid_)))throw std::runtime_error("Native project worker physical exit receipt rejected");
    project_worker_pid_=0;
  }
  void FinalProjectReports(){
    using State=sr::SerialProjectPlan::State;
    std::set<uint64_t> reported;
    for(const auto& attempt:report_.attempts)reported.insert(attempt.test_id);
    for(const auto& declaration:report_.unrun)reported.insert(declaration.test_id);
    for(const auto& declaration:report_.skipped)reported.insert(declaration.test_id);
    for(const auto& declaration:report_.runtime_skipped)reported.insert(declaration.test_id);
    for(size_t index=0;index<report_.projects.size();++index){auto& report=report_.projects[index];const auto& result=work_group_pool_?work_group_pool_->Projects()[index]:pool_?pool_->Results()[index]:project_plan_->Results()[index];
      switch(result.state){case State::NotSelected:report.status="notSelected";break;case State::Passed:report.status="passed";break;case State::Failed:report.status="failed";break;case State::Blocked:report.status="blocked";break;case State::Interrupted:report.status="interrupted";break;case State::NotRun:report.status="notRun";break;default:throw std::runtime_error("Native project has no terminal scheduling receipt");}
      if(result.dependency)report.failed_dependency=*result.dependency+1;
      if(result.state!=State::NotSelected)for(const auto id:project_selected_tests_.at(index))if(reported.insert(id).second){
        if(result.state==State::Passed)throw std::runtime_error("Native passed project has an unaccounted selected declaration");
        const auto reason=Quote(result.reason.empty()?"Native project stopped before this selected declaration was admitted":result.reason);
        Charge(catalog_->titles.at(id).size()+reason.size()+128);report_.unrun.push_back({id,catalog_->titles.at(id),reason});
      }
    }
  }
  uint64_t HookScope(){if(hook_scope_nonce_==UINT64_MAX)throw std::runtime_error("Native hook scope identity exhausted");return ++hook_scope_nonce_;}
  std::string HookError(const std::string& error){return sr::JsonString(error,8192)?error:Quote(error);}
  void RememberHookErrors(const sr::HookPhases::Result& result){
    if(!result.error_sources.empty()&&result.error_sources.size()!=result.errors.size())throw std::runtime_error("Native hook error/source ledger mismatch");
    const auto current=active_?std::optional<sr::AttemptErrorOwner>(sr::AttemptErrorOwner{active_->test.id,active_->generation,worker_index_,host_?host_->Pid():last_pid_}):std::nullopt;
    const auto source=phase_error_attribution_.Consume(result.scope,result.generation,current);
    for(size_t i=0;i<result.errors.size();++i){const auto scalar=HookError(result.errors[i]);const auto step=result.error_sources.empty()?0:result.error_sources[i];
      if(!source){Global(scalar);continue;}
      if(actual_error_.empty()){actual_error_=scalar;originating_step_=step;}
      else{if(attempt_secondary_errors_.size()>=256)throw std::length_error("Native attempt secondary error capacity256 exceeded");Charge(scalar.size()+128);attempt_secondary_errors_.push_back({*source,scalar,step});}
    }
  }
  sr::NativeTask<sr::HookPhases::Result> RunHookGroup(sr::HookWorker::Context context,std::vector<sr::HookCall> calls,uint64_t suite=0){
    const auto count=hook_results_.size();
    if(count>=8192)throw std::runtime_error("Native hook result capacity8192 exceeded");
    {const auto now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
    context.observation={interrupted_?sr::PhaseStatus::Interrupted:timeout_?sr::PhaseStatus::TimedOut:!actual_error_.empty()?sr::PhaseStatus::Failed:runtime_skip_?sr::PhaseStatus::Skipped:sr::PhaseStatus::Passed,std::max(0.0,now-attempt_started_)};
    active_hook_suite_=suite;
    if(context.kind==sr::FixtureScopeKind::Test)context.budget_elapsed=output_preparation_elapsed_;
    context.timing=std::make_shared<sr::FixtureAttemptClocks>(sr::FixtureAttemptClockOwner{context.scope,context.generation,worker_index_,host_->Pid()},context.timeout,now,context.budget_elapsed,context.kind==sr::FixtureScopeKind::Test?std::optional<double>(now-attempt_started_):std::nullopt);
    if(context.kind==sr::FixtureScopeKind::Test){attempt_timing_=context.timing;context.observation.duration=context.budget_elapsed;}
    hook_group_deadline_=now+(context.timeout?context.timeout:3153600000000.0)-context.budget_elapsed;
    phase_error_attribution_.Begin(context.scope,context.generation,active_?std::optional<sr::AttemptErrorOwner>(sr::AttemptErrorOwner{active_->test.id,active_->generation,worker_index_,host_->Pid()}):std::nullopt);
    if(!hook_worker_->Begin(context,std::move(calls),now)){phase_error_attribution_.CancelBeforeAdmission();throw std::runtime_error("Native hook group admission rejected");}
    hook_group_active_=true;}
    while(hook_results_.size()==count){co_await PumpHooks();TickHookWorker();if(host_->Exited())co_await HandleExit();}
    hook_group_active_=false;
    co_return hook_results_.back();
  }
  void RecordBeforeAllSkip(const sr::HookPhases::Result& result,uint64_t suite){
    if(!result.skipped)return;
    if(!active_||result.skip_kind!=sr::HookKind::BeforeAll||!result.skip_token||!result.skip_callback||!result.scope||result.generation!=worker_index_||before_all_skip_cause_||report_.skip_causes.size()>=8192)throw std::runtime_error("Native beforeAll skip cause identity/capacity rejected");
    Charge(result.skip_reason_json.size()+256);
    report_.skip_causes.push_back({result.scope,result.generation,result.skip_callback,result.skip_token,active_->test.id,active_->generation,suite,worker_index_,static_cast<int64_t>(host_->Pid()),result.skip_reason_json,sr::PhaseStatusName(result.observation.status),annotations_.Values().empty()?std::nullopt:std::optional<sr::ModifierAnnotation>(annotations_.Values().back())});
    before_all_skip_cause_=result.scope;runtime_skip_=true;skip_reason_json_=result.skip_reason_json;
  }
  void CommitBeforeAllSkip(const std::set<uint64_t>& omitted){
    if(!before_all_skip_cause_||!active_||AttemptOutcome()!=sr::Outcome::Skipped||!plan_->Results().back().expected)throw std::runtime_error("Native beforeAll skip propagation lacks clean actual attempt");
    plan_->ExcludeQueued(omitted);
    for(const auto id:omitted){if(id==active_->test.id)throw std::runtime_error("Actual beforeAll associated attempt cannot become an unadmitted declaration");
      const auto& title=metadata_->titles.at(id);Charge(title.size()+128);report_.runtime_skipped.push_back({id,before_all_skip_cause_,title});
    }
  }
  sr::NativeTask<bool> RunAllHooks(sr::HookKind kind){
    bool ok=true;
    const auto found=metadata_->hooks.find(kind);if(found==metadata_->hooks.end())co_return true;
    for(const auto& hook:found->second){
      if(host_->Exited()){ok=false;break;}
      if(!active_)throw std::runtime_error("Native all-hook lacks an actual associated attempt");
      sr::HookWorker::Context context{HookScope(),worker_index_,0,active_->retry,hook.timeout.value_or(FixtureDefaultTimeout()),kind==sr::HookKind::BeforeAll?sr::FixtureScopeKind::BeforeAll:sr::FixtureScopeKind::AfterAll,active_->test.id};
      std::vector<sr::HookCall> calls{{kind,hook.callback,hook.fixtures}};const auto result=co_await RunHookGroup(context,std::move(calls));
      ok&=result.ok;timeout_|=result.timed_out;RememberHookErrors(result);
      if(kind==sr::HookKind::BeforeAll&&result.skipped)break;
    }
    co_return ok;
  }
  void MarkUnrun(const std::string& reason){
    AppendGroupReports();
    std::set<uint64_t> attempted;for(const auto& attempt:report_.attempts)attempted.insert(attempt.test_id);
    for(const auto& entry:report_.unrun)attempted.insert(entry.test_id);
    for(const auto& entry:report_.runtime_skipped)attempted.insert(entry.test_id);
    for(const auto& test:metadata_->tests)if(!attempted.contains(test.id)){
      Charge(metadata_->titles.at(test.id).size()+reason.size()+64);
      report_.unrun.push_back(sr::ReportUnrun{test.id,metadata_->titles.at(test.id),Quote(reason)});
    }
  }
  sr::NativeTask<> RunHooks(){
    while(!plan_->Done()){
      ThrowIfStopped();if(!host_)co_await StartWorker();
      const auto next=NextAttempt(static_cast<uint64_t>(host_->Pid()));if(!next)throw std::runtime_error("Native hook retry physical fence did not open");
      active_=*next;finished_=false;finish_ok_=false;timeout_=false;interrupted_=false;actual_error_.clear();attempt_secondary_errors_.clear();attempt_timing_.reset();fixture_mutation_failed_=false;fixture_mutation_error_.clear();originating_step_=0;primary_soft_error_=0;soft_hard_failure_=false;soft_cleanup_failed_=false;soft_infrastructure_failed_=false;soft_hard_error_.clear();callback_infrastructure_failed_=false;callback_error_.clear();finish_error_.clear();runtime_skip_=false;annotations_.Clear();before_all_skip_cause_=0;skip_callback_failed_=false;artifact_cleanup_failed_=false;skip_reason_json_.reset();runtime_expected_failure_=false;expected_failure_reason_json_.reset();
      attempt_started_=sr::DedicatedRealMilliseconds();hook_started_=true;
      const bool output_ready=co_await PrepareOutput();
      bool before_ok=output_ready,infrastructure_failure=!output_ready;
      if(output_ready&&!hook_before_done_){hook_before_done_=true;before_ok=co_await RunAllHooks(sr::HookKind::BeforeAll);infrastructure_failure=!before_ok;}
      if(before_ok&&before_all_skip_cause_)finish_ok_=true;
      if(before_ok&&!before_all_skip_cause_&&!StopRequested()&&!host_->Exited()){
        std::vector<sr::HookCall> calls;
        for(const auto kind:{sr::HookKind::BeforeEach,sr::HookKind::Body,sr::HookKind::AfterEach}){
          if(kind==sr::HookKind::Body)calls.push_back({kind,next->test.id,next->test.fixtures});
          else if(const auto found=metadata_->hooks.find(kind);found!=metadata_->hooks.end())for(const auto& hook:found->second)calls.push_back({kind,hook.callback,hook.fixtures});
        }
        const auto result=co_await RunHookGroup({HookScope(),next->generation,next->test.id,next->retry,metadata_->timeouts.at(next->test.id),sr::FixtureScopeKind::Test},std::move(calls));
        finish_ok_=result.ok&&!skip_callback_failed_;timeout_|=result.timed_out;infrastructure_failure|=result.infrastructure_failure;runtime_skip_=result.skipped;if(result.skipped)skip_reason_json_=result.skip_reason_json;RememberHookErrors(result);
      }
      const bool last=next->test.id==metadata_->tests.back().id;
      if(before_all_skip_cause_||!finish_ok_||next->test.expectedFailure||last||StopRequested()){
        hook_after_done_=true;
        if(output_ready&&!host_->Exited()&&!co_await RunAllHooks(sr::HookKind::AfterAll)){finish_ok_=false;infrastructure_failure=true;}
      }
      if(StopRequested()&&!timeout_)interrupted_=true;
      if(host_control_failed_){finish_ok_=false;if(actual_error_.empty())actual_error_=host_control_error_;}
      ApplyFixtureMutationOutcome();ApplySoftOutcome();ApplyCallbackRetryOutcome();const auto status=AttemptOutcome();
      const auto pid=static_cast<uint64_t>(host_->Pid());
      finish_error_=finish_ok_?"":"Actual native hook/test execution failed";
      if(!plan_->Finish(pid,next->generation,status,finish_error_,infrastructure_failure||(runtime_skip_&&status!=sr::Outcome::Skipped)||skip_callback_failed_||artifact_cleanup_failed_||fixture_mutation_failed_||soft_cleanup_failed_||soft_infrastructure_failed_||callback_infrastructure_failed_||host_control_failed_))throw std::runtime_error("Native hook result admission rejected");
      AccountFailures();const auto& result=plan_->Results().back();
        if(status==sr::Outcome::Passed&&!result.expected&&std::string_view(ExpectedStatus())=="failed"){actual_error_=Quote("Expected to fail, but passed.");}
      const auto error=actual_error_.empty()?Quote(finish_error_):actual_error_;
      if(report_.attempts.size()>=8192)throw std::runtime_error("Native attempt report capacity8192 exceeded");
      Charge(metadata_->titles.at(next->test.id).size()+error.size()+288);
      report_.attempts.push_back(sr::ReportAttempt{next->test.id,next->generation,next->retry,worker_index_,static_cast<int64_t>(pid),metadata_->titles.at(next->test.id),error,AttemptStatus(),result.expected,AttemptChargedDuration(sr::DedicatedRealMilliseconds()),skip_reason_json_,ExpectedStatus(),expected_failure_reason_json_,annotations_.Values(),originating_step_,PrimarySoftError()});report_.attempts.back().secondary_errors=attempt_secondary_errors_;RecordAttemptWall();
      finished_=true;
      const bool clean_before_skip=before_all_skip_cause_&&status==sr::Outcome::Skipped&&result.expected;
      if(clean_before_skip){std::set<uint64_t> omitted;std::set<uint64_t> attempted;for(const auto& attempt:report_.attempts)attempted.insert(attempt.test_id);for(const auto& test:metadata_->tests)if(!attempted.contains(test.id))omitted.insert(test.id);CommitBeforeAllSkip(omitted);}
      if(before_all_skip_cause_||!finish_ok_||next->test.expectedFailure||last||plan_->NeedsWorkerRetirement()){
        co_await CloseWorker();if(plan_->NeedsWorkerRetirement())plan_->WorkerExited(pid,worker_index_);
      }
      const bool exhausted_before=(!before_ok||(before_all_skip_cause_&&!clean_before_skip))&&next->retry>=next->test.retries;
      active_.reset();
      if(exhausted_before){MarkUnrun("beforeAll failed before these declared tests were admitted");break;}
      if(StopRequested()){MarkUnrun(StopReason());break;}
    }
  }
  std::vector<sr::RunnerRetryPlan::Group> SerialGroups(){
    std::vector<sr::RunnerRetryPlan::Group> groups;
    if(metadata_->serial_suites.empty())return groups;
    const sr::NativeSuiteTree tree(metadata_->suites,metadata_->members);
    std::set<uint64_t> selected;for(const auto& test:metadata_->tests)selected.insert(test.id);
    std::map<uint64_t,size_t> positions;
    for(const auto& route:tree.Plan(selected))for(const auto suite:route.ancestors)if(metadata_->serial_suites.contains(suite)){
      const auto [found,inserted]=positions.emplace(suite,groups.size());
      if(inserted)groups.push_back({suite,{}});
      groups[found->second].tests.push_back(route.test);break;
    }
    return groups;
  }
  void ConfirmPlanPhysicalExit(){
    if(!plan_||!host_||!host_->Exited()||DescendantsPhysical()||attachments_.Physical()||outputs_.Physical()||soft_.Physical()||callback_retries_.Physical()||SourcesPhysical())return;
    plan_->ActualWorkerExited(static_cast<uint64_t>(host_->Pid()),worker_index_);AccountFailures();
  }
  void AppendGroupReports(){
    if(!plan_||groups_reported_)return;
    for(const auto& group:plan_->Groups()){
      sr::ReportSerialGroup value;value.id=group.id;value.tests=group.tests;value.retries=group.policy->MaxRetries();value.omissions=group.policy->Omissions();
      const auto state=group.policy->Current();
      value.status=group.policy->Results().empty()?"notRun":state==sr::SerialGroup::State::Passed?"passed":state==sr::SerialGroup::State::Failed?"failed":"interrupted";
      report_.serial_groups.push_back(std::move(value));
    }
    groups_reported_=true;
  }
  bool SerialExpected(bool infrastructure){
    if(infrastructure||skip_callback_failed_||artifact_cleanup_failed_||fixture_mutation_failed_||soft_cleanup_failed_||soft_infrastructure_failed_||callback_infrastructure_failed_||host_control_failed_)return false;
    const auto status=AttemptOutcome();
    return status==sr::Outcome::Skipped||((active_->test.expectedFailure||runtime_expected_failure_)?status==sr::Outcome::Failed:status==sr::Outcome::Passed);
  }
  sr::HookCall NestedCall(uint64_t callback){
    const auto& hook=metadata_->nested_hooks.at(callback);
    return {hook.kind,callback,hook.spec.fixtures};
  }
  sr::NativeTask<bool> RunNestedAll(const sr::NativeSuiteWorkerPlan::Operation& operation){
    bool ok=true;
    for(const auto callback:operation.callbacks){
      if(host_->Exited()){ok=false;break;}
      const auto& hook=metadata_->nested_hooks.at(callback);
      if(!active_||operation.test!=active_->test.id)throw std::runtime_error("Native nested all-hook associated attempt mismatch");
      sr::HookWorker::Context context{HookScope(),worker_index_,0,active_->retry,hook.spec.timeout.value_or(FixtureDefaultTimeout()),hook.kind==sr::HookKind::BeforeAll?sr::FixtureScopeKind::BeforeAll:sr::FixtureScopeKind::AfterAll,active_->test.id};
      std::vector<sr::HookCall> calls{NestedCall(callback)};const auto result=co_await RunHookGroup(context,std::move(calls),operation.suite);
      ok&=result.ok;timeout_|=result.timed_out;RememberHookErrors(result);
      if(hook.kind==sr::HookKind::BeforeAll&&result.skipped)break;
    }
    co_return ok;
  }
  sr::NativeTask<> RunNestedHooks(){
    std::set<uint64_t> finalized;
    const sr::NativeSuiteTree tree(metadata_->suites,metadata_->members);
    while(!plan_->Done()){
      ThrowIfStopped();if(!host_)co_await StartWorker();
      const auto next=NextAttempt(static_cast<uint64_t>(host_->Pid()));
      if(!next)throw std::runtime_error("Native nested retry physical fence did not open");
      if(!suite_plan_){std::set<uint64_t> selected;for(const auto& test:metadata_->tests)if(!finalized.contains(test.id))selected.insert(test.id);
        suite_plan_=std::make_unique<sr::NativeSuiteWorkerPlan>(tree,selected,worker_index_,host_->Pid());}
      active_=*next;finished_=false;finish_ok_=false;timeout_=false;interrupted_=false;actual_error_.clear();attempt_secondary_errors_.clear();attempt_timing_.reset();fixture_mutation_failed_=false;fixture_mutation_error_.clear();originating_step_=0;primary_soft_error_=0;soft_hard_failure_=false;soft_cleanup_failed_=false;soft_infrastructure_failed_=false;soft_hard_error_.clear();callback_infrastructure_failed_=false;callback_error_.clear();finish_error_.clear();runtime_skip_=false;annotations_.Clear();before_all_skip_cause_=0;skip_callback_failed_=false;artifact_cleanup_failed_=false;skip_reason_json_.reset();runtime_expected_failure_=false;expected_failure_reason_json_.reset();
      attempt_started_=sr::DedicatedRealMilliseconds();hook_started_=true;const bool output_ready=co_await PrepareOutput();bool before_ok=output_ready,infrastructure_failure=!output_ready;uint64_t failed_before_suite=0;
      std::optional<sr::NativeSuiteWorkerPlan::Operation> operation;
      for(;;){
        operation=suite_plan_->Next();
        if(!operation||operation->test!=next->test.id)throw std::runtime_error("Native nested test order disagrees with retry selection");
        if(operation->kind==sr::NativeSuiteWorkerPlan::Kind::Test||operation->kind==sr::NativeSuiteWorkerPlan::Kind::SkippedTest)break;
        if(operation->kind!=sr::NativeSuiteWorkerPlan::Kind::BeforeAll)throw std::runtime_error("Native suite entered a close phase before its selected test");
        const bool ok=output_ready&&co_await RunNestedAll(*operation);
        const bool accepted=ok&&before_all_skip_cause_?suite_plan_->SkipBeforeAll(*operation,before_all_skip_cause_):suite_plan_->Complete(*operation,ok);
        if(!accepted)throw std::runtime_error("Native suite entry ACK rejected");
        if(!ok){before_ok=false;infrastructure_failure=true;failed_before_suite=operation->suite;break;}
      }
      if(before_ok&&operation->kind==sr::NativeSuiteWorkerPlan::Kind::SkippedTest&&!StopRequested()&&!host_->Exited()){
        if(operation->skip_cause!=before_all_skip_cause_)throw std::runtime_error("Native skipped operation lost actual beforeAll cause");
        finish_ok_=true;
        if(!suite_plan_->Complete(*operation,true))throw std::runtime_error("Native nested skipped test ACK rejected");
      }else if(before_ok&&!StopRequested()&&!host_->Exited()){
        std::vector<sr::HookCall> calls;for(const auto id:operation->before_each)calls.push_back(NestedCall(id));
        calls.push_back({sr::HookKind::Body,next->test.id,next->test.fixtures});
        for(const auto id:operation->after_each)calls.push_back(NestedCall(id));
        const auto result=co_await RunHookGroup({HookScope(),next->generation,next->test.id,next->retry,metadata_->timeouts.at(next->test.id),sr::FixtureScopeKind::Test},std::move(calls));
        finish_ok_=result.ok&&!skip_callback_failed_;timeout_|=result.timed_out;infrastructure_failure|=result.infrastructure_failure;runtime_skip_=result.skipped;if(result.skipped)skip_reason_json_=result.skip_reason_json;RememberHookErrors(result);
        if(!suite_plan_->Complete(*operation,plan_->InSerialGroup(next->test.id)?SerialExpected(infrastructure_failure):finish_ok_))throw std::runtime_error("Native nested test ACK rejected");
      }else if(before_ok){
        if(!suite_plan_->Complete(*operation,false))throw std::runtime_error("Native interrupted suite operation ACK rejected");
      }
      if((!plan_->InSerialGroup(next->test.id)&&next->test.expectedFailure)||StopRequested()||
         (plan_->InSerialGroup(next->test.id)&&(plan_->LastInSerialGroup(next->test.id)||!SerialExpected(infrastructure_failure))))suite_plan_->RetireAfterCurrentTest();
      while(output_ready&&suite_plan_->HasClosures()){
        const auto closing=suite_plan_->Next();if(!closing||closing->kind!=sr::NativeSuiteWorkerPlan::Kind::AfterAll)throw std::runtime_error("Native suite close operation is absent");
        const bool ok=co_await RunNestedAll(*closing);if(!suite_plan_->Complete(*closing,ok))throw std::runtime_error("Native nested close ACK rejected");
        if(!ok){finish_ok_=false;infrastructure_failure=true;}
      }
      if(StopRequested()&&!timeout_)interrupted_=true;
      if(host_control_failed_){finish_ok_=false;if(actual_error_.empty())actual_error_=host_control_error_;}
      ApplyFixtureMutationOutcome();ApplySoftOutcome();ApplyCallbackRetryOutcome();const auto status=AttemptOutcome();
      const auto pid=static_cast<uint64_t>(host_->Pid());finish_error_=finish_ok_?"":"Actual native nested hook/test execution failed";
      if(!plan_->Finish(pid,next->generation,status,finish_error_,infrastructure_failure||(runtime_skip_&&status!=sr::Outcome::Skipped)||skip_callback_failed_||artifact_cleanup_failed_||fixture_mutation_failed_||soft_cleanup_failed_||soft_infrastructure_failed_||callback_infrastructure_failed_||host_control_failed_))throw std::runtime_error("Native nested result admission rejected");
      AccountFailures();const auto& result=plan_->Results().back();
        if(status==sr::Outcome::Passed&&!result.expected&&std::string_view(ExpectedStatus())=="failed"){actual_error_=Quote("Expected to fail, but passed.");}const auto error=actual_error_.empty()?Quote(finish_error_):actual_error_;
      if(report_.attempts.size()>=8192)throw std::runtime_error("Native attempt report capacity8192 exceeded");
      Charge(metadata_->titles.at(next->test.id).size()+error.size()+288);
      report_.attempts.push_back(sr::ReportAttempt{next->test.id,next->generation,next->retry,worker_index_,static_cast<int64_t>(pid),metadata_->titles.at(next->test.id),error,AttemptStatus(),result.expected,AttemptChargedDuration(sr::DedicatedRealMilliseconds()),skip_reason_json_,ExpectedStatus(),expected_failure_reason_json_,annotations_.Values(),originating_step_,PrimarySoftError()});report_.attempts.back().secondary_errors=attempt_secondary_errors_;RecordAttemptWall();
      finished_=true;
      if(!plan_->InSerialGroup(next->test.id)&&(result.expected||next->retry>=next->test.retries||interrupted_))finalized.insert(next->test.id);
      if(before_all_skip_cause_&&status==sr::Outcome::Skipped&&result.expected){
        const std::set<uint64_t> omitted(operation->skipped_tests.begin(),operation->skipped_tests.end());CommitBeforeAllSkip(omitted);finalized.insert(omitted.begin(),omitted.end());
      }else if(before_all_skip_cause_&&!failed_before_suite&&!finish_ok_){
        const auto cause=std::find_if(report_.skip_causes.begin(),report_.skip_causes.end(),[&](const auto& entry){return entry.scope_id==before_all_skip_cause_;});failed_before_suite=cause->suite_id;
      }
      if(failed_before_suite&&next->retry>=next->test.retries){
        std::set<uint64_t> blocked;const auto subtree=tree.SuiteTests(failed_before_suite);for(const auto& test:metadata_->tests)if(subtree.contains(test.id))blocked.insert(test.id);plan_->ExcludeQueued(blocked);
        for(const auto id:blocked)if(!finalized.contains(id)){
          finalized.insert(id);const auto title=metadata_->titles.at(id),reason=Quote("Ancestor beforeAll failed before this declared test was admitted");Charge(title.size()+reason.size()+64);report_.unrun.push_back({id,title,reason});
        }
      }
      const bool retire=(!plan_->InSerialGroup(next->test.id)&&(!finish_ok_||next->test.expectedFailure))||suite_plan_->NeedsRetirement()||plan_->NeedsWorkerRetirement()||plan_->Done();
      if(retire){co_await CloseWorker();suite_plan_.reset();if(plan_->NeedsWorkerRetirement())plan_->WorkerExited(pid,worker_index_);}
      for(const auto& test:metadata_->tests)if(plan_->Finalized(test.id))finalized.insert(test.id);
      active_.reset();if(StopRequested())break;
    }
  }
  void ObserveHookStop(double now){
    sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
    if(StopRequested()&&!hook_stop_seen_&&hook_group_active_){
      hook_stop_seen_=true;const auto at=StopTime();
      const auto expired=hook_worker_->TimeoutAt();
      const bool timeout_before_stop=expired&&(at?*expired<=*at:*expired<=now);
      if(!timeout_&&!timeout_before_stop&&(at?*at<hook_group_deadline_:now<hook_group_deadline_))interrupted_=true;
      hook_stop_cleanup_deadline_=(at?*at:now)+1000;
      hook_worker_->Cancel(StopReason(),now,interrupted_);
    }
    if(hook_stop_cleanup_deadline_&&now>=hook_stop_cleanup_deadline_)KillHost();
  }
  bool SoftFailed()const{return active_&&(soft_hard_failure_||soft_.FailedAttempt(active_->generation));}
  void ApplySoftOutcome(){if(!SoftFailed())return;finish_ok_=false;if(actual_error_.empty()&&!soft_hard_error_.empty()&&!timeout_&&!interrupted_)actual_error_=soft_hard_error_;if(actual_error_.empty()&&!timeout_&&!interrupted_)if(const auto* r=soft_.FirstFailure(active_->generation)){actual_error_=r->error_json;primary_soft_error_=r->id;}}
  uint64_t PrimarySoftError()const{if(!active_||timeout_||interrupted_||originating_step_||soft_hard_failure_)return 0;return primary_soft_error_;}
  sr::Outcome AttemptOutcome()const{return interrupted_?sr::Outcome::Interrupted:timeout_?sr::Outcome::TimedOut:(!finish_ok_||SoftFailed())?sr::Outcome::Failed:runtime_skip_?sr::Outcome::Skipped:sr::Outcome::Passed;}
  const char* AttemptStatus()const{return interrupted_?"interrupted":timeout_?"timedOut":(!finish_ok_||SoftFailed())?"failed":runtime_skip_?"skipped":"passed";}
  const char* ExpectedStatus()const{return runtime_skip_?"skipped":runtime_expected_failure_||(active_&&active_->test.expectedFailure)?"failed":"passed";}
  void ExpectedFailurePacket(const std::vector<std::string>& fields,double now,bool hooks){
    Size(fields,10);const auto request=Integer(fields[1]),token=Integer(fields[2]),owner=Integer(fields[3]),generation=Integer(fields[4]);const auto annotation=sr::ModifierAnnotations::Prepare("fail",fields[5],Integer(fields[6],1)!=0,fields[7],Integer(fields[8],2147483647),Integer(fields[9],2147483647));const auto& reason=annotation.reason_json;
    bool ok=request&&mutation_nonce_<UINT64_MAX&&request==mutation_nonce_+1;
    if(ok)++mutation_nonce_;
    ok=ok&&steps_.AdmitsCallback(token,owner,generation)&&active_&&!StopRequested()&&!closing_worker_&&!physical_kill_requested_&&!runtime_skip_&&annotations_.CanAppend(annotation);
    if(ok&&hooks)ok=hook_worker_->CanExpectFailure(token,owner,generation,active_->test.id,now);
    else if(ok)ok=!token&&active_->test.id==owner&&active_->generation==generation&&body_physical_&&!finished_&&!timeout_&&!interrupted_&&now<deadline_&&owners_->CanExpectFailure(owner,generation,now);
    if(ok)Charge(sr::ModifierAnnotationJSON(annotation).size()+128);
    if(ok)ok=plan_->ExpectFailure(static_cast<uint64_t>(host_->Pid()),active_->generation);
    if(ok){if(!runtime_expected_failure_){expected_failure_reason_json_=reason;Charge(reason.size()+32);}runtime_expected_failure_=true;annotations_.Append(annotation);}
    Send("FAIL_RESULT "+std::to_string(request)+" "+std::to_string(token)+" "+std::to_string(owner)+" "+std::to_string(generation)+" "+(ok?"1 failed "+sr::ModifierAnnotationWire(annotation):"0 "+Hex("Native expected failure rejected: unsupported phase, stale, expired or stopped")));
  }
  void SkipPacket(const std::vector<std::string>& fields,double now,bool hooks){
    Size(fields,10);const auto request=Integer(fields[1]),token=Integer(fields[2]),owner=Integer(fields[3]),generation=Integer(fields[4]);const auto annotation=sr::ModifierAnnotations::Prepare("skip",fields[5],Integer(fields[6],1)!=0,fields[7],Integer(fields[8],2147483647),Integer(fields[9],2147483647));const auto& reason=annotation.reason_json;
    bool ok=request&&mutation_nonce_<UINT64_MAX&&request==mutation_nonce_+1;
    if(ok)++mutation_nonce_;
    ok=ok&&steps_.AdmitsCallback(token,owner,generation)&&(!steps_.CallbackOwner(token,owner,generation)||callback_retries_.CanSkip(*steps_.CallbackOwner(token,owner,generation)))&&soft_.CanSkip(token,owner,generation)&&(!attachments_.Pending(token,owner,generation)||attachments_.CanAcceptTestSkip(token,owner,generation,request,now))&&!host_control_failed_&&!StopRequested()&&!closing_worker_&&!physical_kill_requested_&&annotations_.CanAppend(annotation);
    const bool pending_attachment=ok&&attachments_.Pending(token,owner,generation);
    if(ok)Charge(sr::ModifierAnnotationJSON(annotation).size()+128);
    if(ok&&hooks){ok=hook_worker_->RequestSkip(token,owner,generation,reason,now,[&]{if(attachments_.Pending(token,owner,generation))attachments_.AcceptTestSkip(token,owner,generation,request,reason,now);steps_.AcceptTestSkip(token,owner,generation,request,reason,now);},pending_attachment);if(ok)hook_group_deadline_=hook_worker_->ActiveDeadline().value_or(hook_group_deadline_);}
    else if(ok){
      ok=!token&&active_&&active_->test.id==owner&&active_->generation==generation&&body_physical_&&!finished_&&!timeout_&&!interrupted_&&!runtime_skip_&&now<deadline_;
      if(ok){ok=owners_->Skip(owner,generation,now,pending_attachment);if(ok){runtime_skip_=true;skip_reason_json_=reason;if(attachments_.Pending(token,owner,generation))attachments_.AcceptTestSkip(token,owner,generation,request,reason,now);steps_.AcceptTestSkip(token,owner,generation,request,reason,now);AbortBody("Native runtime skip requested");}}
    }
    if(ok){Charge(reason.size()+32);annotations_.Append(annotation);}
    Send("SKIP_RESULT "+std::to_string(request)+" "+std::to_string(token)+" "+std::to_string(owner)+" "+std::to_string(generation)+" "+(ok?"1 skipped "+sr::ModifierAnnotationWire(annotation):"0 "+Hex("Native runtime skip rejected: unsupported phase, stale, expired or stopped")));
  }
  void AmendTimeoutPacket(const std::vector<std::string>& fields,double now,bool hooks){
    Size(fields,6);const auto request=Integer(fields[1]),token=Integer(fields[2]),owner=Integer(fields[3]),generation=Integer(fields[4]),timeout=Integer(fields[5],120000);
    bool ok=request&&mutation_nonce_<UINT64_MAX&&request==mutation_nonce_+1;
    if(ok)++mutation_nonce_;
    ok=ok&&steps_.AdmitsCallback(token,owner,generation)&&!StopRequested()&&!closing_worker_&&!physical_kill_requested_;
    if(ok&&hooks){const auto actual=hook_worker_->AmendTimeout(token,owner,generation,timeout,now);ok=actual.has_value();if(actual)hook_group_deadline_=*actual;}
    else if(ok){
      ok=!token&&active_&&active_->test.id==owner&&active_->generation==generation&&body_physical_&&!finished_&&!timeout_&&!interrupted_&&now<deadline_;
      if(ok){const auto deadline=attempt_started_+(timeout?static_cast<double>(timeout):3153600000000.0);ok=owners_->AmendTimeout(owner,generation,timeout,now);if(ok)deadline_=owners_->ActiveDeadline(now).value_or(deadline);}
    }
    if(ok){const auto deadline=hooks?hook_worker_->ActiveDeadline().value_or(hook_group_deadline_):deadline_;steps_.Amend(token,owner,generation,deadline,FixtureNow());soft_.Amend(token,owner,generation,deadline,FixtureNow(),suite_deadline_.OriginalDeadline(),hook_stop_cleanup_deadline_?std::make_optional(hook_stop_cleanup_deadline_):std::nullopt);attachments_.Amend(token,owner,generation,deadline,FixtureNow());CallbackRetryAmend(token,owner,generation,deadline,FixtureNow());}
    Send("TIMEOUT_RESULT "+std::to_string(request)+" "+std::to_string(token)+" "+std::to_string(owner)+" "+std::to_string(generation)+" "+(ok?"1 "+std::to_string(timeout):"0 "+Hex("Native live timeout amendment rejected: stale, expired, stopped or capacity exceeded")));
  }
  void SettledWithAttachments(uint64_t token,uint64_t scope,uint64_t generation,std::function<void(const std::optional<std::string>&,uint64_t)> forward,double now,const std::string& error={},uint64_t source=0){
    steps_.ObserveRawResult(token,scope,generation,error,source);
    attachments_.CloseAdmission(token,scope,generation);
    const auto* owner=steps_.CallbackOwner(token,scope,generation);
    if(!owner)throw std::runtime_error("Native callback retry outer fence lost its actual owner");
    callback_retries_.Fence(*owner,[this,token,scope,generation,forward=std::move(forward),error,source](const auto& retry_failure){
      soft_.Fence(token,scope,generation,[this,token,scope,generation,forward,error,source,retry_failure]{
        steps_.SettledWithSource(token,scope,generation,[this,token,scope,generation,forward,retry_failure](const auto& failure,uint64_t actual_source){
          attachments_.FenceWithAppendFailure(token,scope,generation,[this,forward,failure,actual_source,retry_failure](const auto& append_failure){if(!host_control_failed_)forward(failure?failure:append_failure?append_failure:retry_failure,failure?actual_source:0);},FixtureNow());
        },FixtureNow(),error,source);
      },FixtureNow(),error);
    },now);
  }
  sr::NativeTask<> PumpHooks(){
    co_await continuation_.Yield(CallbackRetryWake(FixtureNow()+10));
    for(const auto& line:host_->Poll(0)){
      const auto fields=Fields(line);if(fields.empty())throw std::runtime_error("Empty native hook host packet");
      const auto now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
      TickFixtureClocks(now);ObserveHookStop(now);
      if(sources_&&sources_->Packet(fields,now,StopRequested()||closing_worker_||physical_kill_requested_)){}
      else if(FixtureTimeoutPacket(fields,now)){}
      else if(callback_retries_.Packet(fields,FixtureNow(),StopRequested()||closing_worker_||physical_kill_requested_,StopRequested()&&!closing_worker_&&!physical_kill_requested_)){}
      else if(soft_.Packet(fields,FixtureNow(),StopRequested()||closing_worker_||physical_kill_requested_,StopRequested()&&!closing_worker_&&!physical_kill_requested_)){}
      else if(attachments_.Packet(fields,FixtureNow(),StopRequested()||closing_worker_||physical_kill_requested_)){}
      else if(steps_.Packet(fields,FixtureNow(),StopRequested()||closing_worker_||physical_kill_requested_)){}
      else if(fields[0]=="FAIL_REQUEST"){ExpectedFailurePacket(fields,now,true);}
      else if(fields[0]=="SKIP_REQUEST"){SkipPacket(fields,now,true);}
      else if(fields[0]=="CALL_SKIPPED"){Size(fields,4);const auto token=Integer(fields[1]),scope=Integer(fields[2]),generation=Integer(fields[3]);SettledWithAttachments(token,scope,generation,[this,token,scope,generation](const auto& failure,uint64_t){FixtureTransition([&](double now){hook_worker_->CallbackSettled(token,scope,generation,!failure,failure.value_or(""),now,!failure);});},now);}
      else if(fields[0]=="SET_TIMEOUT"){AmendTimeoutPacket(fields,now,true);}
      else if(fields[0]=="READY"||fields[0]=="STARTED"){Size(fields,4);if(fields[0]=="READY"){SourceSetupClosed(fields);if(FixtureReadyClock(Integer(fields[1]),Integer(fields[2]),Integer(fields[3]),now))hook_worker_->FixtureReady(Integer(fields[1]),Integer(fields[2]),Integer(fields[3]),now);}}
      else if(fields[0]=="SETTLED"||fields[0]=="CALL_RESULT"||fields[0]=="CALL_RESULT_UTF16"||fields[0]=="CALL_RESULT_FROM_STEP_UTF16"){
        const bool from_step=fields[0]=="CALL_RESULT_FROM_STEP_UTF16";Size(fields,from_step?7:6);const bool ok=Integer(fields[4],1)!=0;
        const auto error=ok?"":(fields[0]=="CALL_RESULT_UTF16"||from_step)?sr::RuntimeSkipReason(fields[5]):Scalar(fields[5],8192);
        if(fields[0]=="SETTLED"){SourceSetupClosed(fields);if(!ok&&hook_worker_->OwnsFixture(Integer(fields[1]),Integer(fields[2]),Integer(fields[3]))){
          if(actual_error_.empty()&&!hook_worker_->HasPhaseErrors(Integer(fields[2]),Integer(fields[3]))){actual_error_=error;originating_step_=0;}
          else if(active_&&phase_error_attribution_.Pending(Integer(fields[2]),Integer(fields[3]))){const auto source=phase_error_attribution_.Current(Integer(fields[2]),Integer(fields[3]),sr::AttemptErrorOwner{active_->test.id,active_->generation,worker_index_,host_->Pid()});if(source){if(attempt_secondary_errors_.size()>=256)throw std::length_error("Native attempt secondary error capacity256 exceeded");Charge(error.size()+128);attempt_secondary_errors_.push_back({*source,error,0,true});}}
        }FixtureSettledClock(Integer(fields[1]),Integer(fields[2]),Integer(fields[3]),now);hook_worker_->FixtureSettled(Integer(fields[1]),Integer(fields[2]),Integer(fields[3]),ok,error,now);}
        else{const auto token=Integer(fields[1]),scope=Integer(fields[2]),generation=Integer(fields[3]);const auto source=from_step?Integer(fields[6]):0;
          if(from_step){if(ok||!source)throw std::runtime_error("Invalid native callback originating-step failure receipt");steps_.FailureSource(token,scope,generation,source);}
          SettledWithAttachments(token,scope,generation,[this,token,scope,generation,ok,error](const auto& failure,uint64_t actual_source){FixtureTransition([&](double now){hook_worker_->CallbackSettled(token,scope,generation,ok&&!failure,failure.value_or(error),now,false,actual_source);});},now,error,source);
        }
      }
      else if(fields[0]=="HOST_ERROR"){Size(fields,2);throw std::runtime_error("Actual hook host failed: "+Scalar(fields[1],8192));}
      else throw std::runtime_error("Unknown native hook host packet");
    }
    {const auto now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
    if(sources_)sources_->Tick(now);
    TickFixtureClocks(now);ObserveHookStop(now);soft_.Tick(now);steps_.Tick(now);}
    // Actual file exits can be observed after the preceding pinned event.
    // Public step callbacks need their genuine physical observation too.
    attachments_.Tick(sr::DedicatedRealMilliseconds());
    FixtureTransition([&](double now){callback_retries_.Tick(now);});
    if(host_->ControlClosed()&&!closing_worker_&&!physical_kill_requested_&&!host_->Exited())co_await RetireClosedControl(true,"Actual hook host control closed before native retirement");
  }
  sr::NativeTask<> CloseHookWorker(bool force){
    closing_worker_=true;
    if(force)KillHost();
    else {
      if(metadata_->suites.empty()&&hook_started_&&!hook_after_done_&&!host_->Exited()){
        hook_after_done_=true;if(!co_await RunAllHooks(sr::HookKind::AfterAll))Global(Quote("Actual afterAll failed during native worker retirement"));
      }
      FixtureTransition([&](double now){if(!host_->Exited()&&!hook_worker_->Retire(now+1000,now))KillHost();});
    }
    const auto end=FixtureNow()+1500;double close_ready_deadline=0;
    while(!host_->Exited()){
      if(force){co_await continuation_.Yield(CallbackRetryWake(FixtureNow()+10));try{host_->Poll(0);}catch(const std::exception&){KillHost();}}
      else co_await PumpHooks();
      const auto now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);TickFixtureClocks(now);hook_worker_->Tick(now);
      const auto state=hook_worker_->Inspect();
      if(!force&&!close_sent_&&!state.fixtures&&!state.callbacks){Send("CLOSE");close_sent_=true;close_ready_deadline=now+1000;}
      if((close_ready_deadline&&now>=close_ready_deadline)||(!close_sent_&&now>=end&&(!fixture_runtime_||!fixture_runtime_->Physical())))KillHost();
    }
    try{host_->Poll(0);}catch(const std::exception& error){Global(Quote(error.what()));}
    co_await RetireDescendants();
    if(suite_plan_&&!suite_plan_->WorkerClosed(*host_))throw std::runtime_error("Native suite retirement lacks actual worker exit");
    hook_worker_->WorkerClosed();co_await CaptureWorker();ConfirmPlanPhysicalExit();ProjectWorkerExited();host_.reset();hook_worker_.reset();closing_worker_=false;
  }
  bool StopRequested(){suite_deadline_.Observe(FixtureNow());return shared_->emergency_time.has_value()||interrupt_.Signal()||suite_deadline_.Expired()||((options_.workers>1||options_.directory_admission)&&shared_->failure_time.has_value());}
  std::optional<double> StopTime(){
    StopRequested();const auto signal=interrupt_.TimeMilliseconds(),deadline=suite_deadline_.Time();
    auto at=signal&&deadline?std::optional<double>(std::min(*signal,*deadline)):signal?signal:deadline;
    if((options_.workers>1||options_.directory_admission)&&shared_->failure_time&&(!at||*shared_->failure_time<*at))at=shared_->failure_time;
    if(shared_->emergency_time&&(!at||*shared_->emergency_time<*at))at=shared_->emergency_time;
    return at;
  }
  std::string StopReason(){
    StopRequested();const auto signal=interrupt_.TimeMilliseconds(),deadline=suite_deadline_.Time();
    if(shared_->emergency_time&&(!deadline||*shared_->emergency_time<*deadline)&&(!signal||*shared_->emergency_time<*signal)&&(!shared_->failure_time||*shared_->emergency_time<*shared_->failure_time))return shared_->emergency_reason;
    if((options_.workers>1||options_.directory_admission)&&shared_->failure_time&&(!deadline||*shared_->failure_time<*deadline)&&(!signal||*shared_->failure_time<*signal))return shared_->policy_reason;
    if(deadline&&(!interrupt_.Signal()||(signal&&*deadline<=*signal)))return suite_deadline_.Reason();
    return interrupt_.Reason();
  }
  void ObserveStop(double now){sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);const auto at=StopTime();if(active_&&StopRequested()&&(!finished_||(runtime_skip_&&body_physical_))&&!timeout_&&!interrupted_&&((runtime_skip_&&body_physical_)||(at?*at<deadline_:now<deadline_))){interrupted_=true;AbortBody(StopReason());if(runtime_skip_){finish_ok_=false;finish_error_=StopReason();}owners_->Cancel(StopReason(),now);}}
  void ThrowIfStopped(){if(StopRequested()){if(plan_)plan_->Stop();stop_recorded_=true;throw std::runtime_error(StopReason());}}
  void Charge(size_t bytes){if(bytes>67108864-shared_->metadata_charge)throw std::runtime_error("Native runner report metadata budget64MiB exceeded");shared_->metadata_charge+=bytes;}
  void Global(const std::string& error,bool project_error=true){if(report_.global_errors_json.size()>=256)throw std::runtime_error("Native runner global error capacity256 exceeded");Charge(error.size());if(project_error)++local_errors_;report_.global_errors_json.push_back(error);}
  void KillHost(){physical_kill_requested_=true;callback_retries_.CancelAll(FixtureNow());if(sources_)sources_->Cancel(FixtureNow());soft_.CancelAll(Quote("Native test worker retirement requested"),FixtureNow());if(shared_->emergency_time){attachments_.CancelAll(shared_->emergency_reason,FixtureNow());outputs_.CancelAll(shared_->emergency_reason,FixtureNow());}else{attachments_.CancelAll("Native test worker retirement requested",FixtureNow());outputs_.CancelAll("Native test worker retirement requested",FixtureNow());}if(host_)host_->Kill();}
  void Send(const std::string& line){
    const auto now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
    if(line.starts_with("BODY ")||line.starts_with("CALL ")){
      if(!active_||!host_)throw std::runtime_error("Native step callback has no actual attempt/worker");
      const auto fields=Fields(line);const bool hooks=fields[0]=="CALL";
      const auto token=hooks?Integer(fields[1]):0,scope=Integer(fields[hooks?2:1]),generation=Integer(fields[hooks?3:2]);
      const auto deadline=hooks?hook_worker_->ActiveDeadline().value_or(hook_group_deadline_):deadline_;
      const sr::StepOwner callback_owner{active_->test.id,active_->generation,token,scope,generation,worker_index_,host_->Pid(),hooks?fields[4]:"body"};
      SendNativeRepeatTest(active_->test.id);SendSnapshotContext(active_->test.id);
      steps_.BeginCallback(callback_owner,deadline,now);attachments_.BeginCallback(callback_owner,deadline,now);soft_.Open(callback_owner,deadline,now,suite_deadline_.OriginalDeadline(),hook_stop_cleanup_deadline_?std::make_optional(hook_stop_cleanup_deadline_):std::nullopt);callback_retries_.Open(callback_owner,sr::SoftCallbackDeadline(callback_owner.callback_kind,deadline,suite_deadline_.OriginalDeadline(),hook_stop_cleanup_deadline_?std::make_optional(hook_stop_cleanup_deadline_):std::nullopt),now);
    }else if(line.starts_with("ABORT_CALL ")||line.starts_with("ABORT ")){
      const auto fields=Fields(line);const bool hooks=fields[0]=="ABORT_CALL";
      const auto token=hooks?Integer(fields[1]):0,scope=Integer(fields[hooks?2:1]),generation=Integer(fields[hooks?3:2]);
      // A native live timeout can expire during HookWorker::AmendTimeout before
      // it returns. Synchronize that genuine phase cap before its abort event.
      steps_.Amend(token,scope,generation,hooks?hook_worker_->ActiveDeadline().value_or(hook_group_deadline_):deadline_,now);
      attachments_.Amend(token,scope,generation,hooks?hook_worker_->ActiveDeadline().value_or(hook_group_deadline_):deadline_,now);
      soft_.Cancel(token,scope,generation,Quote(sr::DecodeHex(fields[hooks?4:3],8192)),now);CallbackRetryCancel(token,scope,generation,now);
      steps_.Cancel(token,scope,generation,Quote(sr::DecodeHex(fields[hooks?4:3],8192)),FixtureNow());
      attachments_.Cancel(token,scope,generation,sr::DecodeHex(fields[hooks?4:3],8192),FixtureNow());
    }
    if(line.starts_with("SETUP ")||line.starts_with("TEARDOWN ")||line.starts_with("CANCEL "))FixtureCommandClock(Fields(line),now);
    if(line.starts_with("SETUP "))SourceSetup(line,now);
    else if(line.starts_with("TEARDOWN ")||line.starts_with("CANCEL "))SourceSetupClosed(Fields(line));
    if(!host_||!host_->Send(line))throw std::runtime_error("Native host control admission failed");
  }
  sr::NativeTask<> StartWorker(){
    ThrowIfStopped();
    if(!options_.directory_admission&&!parallel_actor_&&!catalog_&&!metadata_&&!repeat_collection_timing_)repeat_collection_timing_=std::make_shared<sr::DirectoryActorTiming>(sr::DedicatedRealMilliseconds());
    while(!shared_->descendants.CanEnroll()){co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+5);ThrowIfStopped();}
    if(shared_->worker_sequence==UINT32_MAX)throw std::runtime_error("Native worker index exhausted");
    size_t bytes=shared_->directory_worker_bytes;for(const auto& worker:report_.workers)if(!options_.directory_admission)bytes+=worker.stdout_bytes.size()+worker.stderr_bytes.size();
    if(shared_->worker_sequence>=8192||bytes>sr::kRunnerReportStreamBytes-(shared_->active_workers+1)*2*1048576)throw std::runtime_error("Native worker report byte reservation exhausted");
    worker_index_=++shared_->worker_sequence;mutation_nonce_=0;host_control_failed_=false;host_control_error_.clear();steps_.WorkerStarted();soft_.WorkerStarted();callback_retries_.WorkerStarted();callback_source_owner_.reset();attachments_.WorkerStarted();
    if(FileOwner()&&FileOwner()->Open()&&!provisional_serial_file_)FileOwner()->Validate();
    if(sources_&&(SourcesPhysical()||!sources_->ChannelDrained()))throw std::runtime_error("Native Source channel still owned at fresh worker admission");
    sources_=std::make_unique<sr::RunnerRetainedSources>(shared_->source_leases,sr::RunnerRetainedSources::Hooks{[this](const auto& packet){Send(packet);},[this](const auto& owner){return callback_retries_.HasSourceFenceOwner(owner.callback,owner.request,owner.assertion,owner.invocation,owner.iteration_step,owner.outer_step);}});source_bound_=false;source_retired_=false;source_retire_end_.reset();source_cleanup_reported_=false;source_reported_=false;
    host_=std::make_unique<sr::DedicatedHostProcess>(options_.node,std::vector<std::string>{options_.host,options_.suite,std::to_string(worker_index_),"--suitecut-source-endpoint=4","--suitecut-source-work=2"},sources_->PeerDescriptor());
    ++shared_->active_workers;descendant_generation_=shared_->descendants.Enroll(worker_index_,*host_);descendant_arrived_=false;
    DirectoryHostStarted(host_->Pid());
    if(reuse_pool_){
      if(!current_file_group_||!reuse_key_)throw std::logic_error("Native reusable worker lacks native group/key");
      reuse_borrow_=reuse_pool_->Started(*reuse_key_,*current_file_group_,worker_index_,host_->Pid());
      if(!reuse_borrow_)throw std::runtime_error("Native reusable host admission rejected");
      project_worker_pid_=host_->Pid();
    }else if(current_file_group_){
      if(!file_group_pool_->WorkerStarted(*current_file_group_,worker_index_,host_->Pid()))throw std::runtime_error("Native file-group actual worker admission rejected");
      project_worker_pid_=host_->Pid();group_workers_.push_back(worker_index_);
    }else if(current_group_){
      if(!work_group_pool_->WorkerStarted(*current_group_,worker_index_,host_->Pid()))throw std::runtime_error("Native work-group actual worker admission rejected");
      project_worker_pid_=host_->Pid();report_.projects[current_group_->project].workers.push_back(worker_index_);group_workers_.push_back(worker_index_);
    }else if(current_project_){
      if(!(pool_?pool_->WorkerStarted(current_project_->generation,worker_index_,host_->Pid()):project_plan_->WorkerStarted(current_project_->generation,host_->Pid())))throw std::runtime_error("Native project worker admission rejected");
      project_worker_pid_=host_->Pid();report_.projects[current_project_->index].workers.push_back(worker_index_);
    }
    worker_captured_=false;close_sent_=false;abort_sent_=false;close_failure_=false;physical_kill_requested_=false;
    const auto source_host=sr::RetainedSourceProcess::ReadStat(host_->Pid());source_host_={worker_index_,worker_index_,host_->Pid(),source_host.start};sources_->HostLaunched(source_host_,*host_);source_bound_=true;
    Metadata value;bool loaded=false;const double deadline=sr::DedicatedRealMilliseconds()+10000;
    while(!loaded) {
      co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+10);
      ThrowIfStopped();
      for(const auto& line:host_->Poll(0)){
        const auto f=Fields(line);if(f.empty())throw std::runtime_error("Empty native host packet");
        if(f[0]=="SOURCE_CAP"){Size(f,2);if((f[1]!="1"&&f[1]!="2")||value.source_capability||!sources_||!source_bound_)throw std::runtime_error("Native Source capability has no unique actual host endpoint");sources_->Enable(static_cast<unsigned>(Integer(f[1],2)));value.source_capability=true;}
        else if(f[0]=="FIXTURE") {Size(f,5);if(!value.fixture_layers.empty()||!Name(f[1])||(f[2]!="test"&&f[2]!="worker")||value.fixtures.size()>=256)throw std::runtime_error("Invalid fixture metadata");value.fixtures.push_back({f[1],f[2]=="worker"?sr::Scope::Worker:sr::Scope::Test,Names(f[4]),Integer(f[3],1)!=0});}
        else if(f[0]=="FLAYER"){
          Size(f,2);if(!value.fixtures.empty()||value.fixture_layers.size()>=sr::FixtureCatalog::kLayers||Integer(f[1],32)!=value.fixture_layers.size()+1)throw std::runtime_error("Invalid native fixture layer metadata");value.fixture_layers.emplace_back();
        }
        else if(f[0]=="FDECL"){
          Size(f,6);if(value.fixture_layers.empty()||++value.fixture_declaration_count>sr::FixtureCatalog::kDeclarations||Integer(f[1],256)!=value.fixture_declaration_count||!Name(f[2])||(f[3]!="-"&&f[3]!="test"&&f[3]!="worker")||(f[4]!="-"&&f[4]!="0"&&f[4]!="1"))throw std::runtime_error("Invalid native raw fixture declaration");
          value.fixture_layers.back().push_back({value.fixture_declaration_count,f[2],Names(f[5]),f[3]=="-"?std::nullopt:std::optional<sr::Scope>(f[3]=="worker"?sr::Scope::Worker:sr::Scope::Test),f[4]=="-"?std::nullopt:std::optional<bool>(f[4]=="1")});
        }
        else if(f[0]=="FTITLE"||f[0]=="FLOCATION"){
          Size(f,f[0]=="FTITLE"?3:5);const auto id=Integer(f[1],256);
          if(!id||id>(value.fixture_layers.empty()?value.fixtures.size():value.fixture_declaration_count))throw std::runtime_error("Native fixture diagnostic metadata has no actual declaration");
          if(f[0]=="FTITLE"){const auto title=sr::RuntimeSkipReason(f[2]);if(!sr::JsonString(title,4096)||!value.fixture_titles.emplace(id,title).second)throw std::runtime_error("Invalid or duplicate native fixture display title");}
          else value.fixture_sources.Capture({id,sr::DecodeHex(f[2],16384),static_cast<uint32_t>(Integer(f[3],2147483647)),static_cast<uint32_t>(Integer(f[4],2147483647))});
        }
        else if(f[0]=="FTIMEOUT"){
          Size(f,3);const auto id=Integer(f[1],256),timeout=Integer(f[2],120000);
          if(!id||id>(value.fixture_layers.empty()?value.fixtures.size():value.fixture_declaration_count)||value.fixture_timeout_declarations.size()>=256)throw std::runtime_error("Native fixture timeout has no actual declaration");
          value.fixture_timeout_declarations.push_back({id,timeout});
        }
        else if(f[0]=="FOPTION"||f[0]=="FLITERAL"){
          Size(f,f[0]=="FOPTION"?3:4);const auto id=Integer(f[1],256);if(!id||id>value.fixture_declaration_count)throw std::runtime_error("Native fixture value metadata has no actual declaration");
          auto& record=value.fixture_value_declarations[id];record.declaration=id;
          if(f[0]=="FOPTION"){if(record.option)throw std::runtime_error("Duplicate native fixture option flag");record.option=Integer(f[2],1)!=0;}
          else{if(record.literal)throw std::runtime_error("Duplicate native fixture literal declaration");record.literal=FixtureValue(f[2],f[3]);}
        }
        else if(f[0]=="FCONFIG"){
          Size(f,4);if(value.fixture_layers.empty()||value.fixture_option_overrides.size()>=256||!Name(f[1]))throw std::runtime_error("Native fixture configuration metadata rejected");value.fixture_option_overrides.push_back({f[1],FixtureValue(f[2],f[3])});
        }
        else if(f[0]=="PROJECT"){
          Size(f,5);const auto id=Integer(f[1],128);if(id!=value.projects.size()+1)throw std::runtime_error("Native project declaration order rejected");
          const auto name=sr::DecodeHex(f[2],256),metadata=sr::DecodeHex(f[4],16384);
          if(!sr::JsonString(Quote(name),256)||!sr::ProjectMetadataJSON(metadata))throw std::runtime_error("Native project name or metadata JSON rejected");
          value.projects.push_back({name,{},std::nullopt,Integer(f[3],1)!=0});value.project_metadata.push_back(metadata);
        }
        else if(f[0]=="PROJECT_OUTPUT_DIR"){Size(f,3);const auto id=Integer(f[1],128);if(!id)throw std::runtime_error("Native project output root has no actual declaration");value.project_output_roots.Declare(id-1,value.projects.size(),sr::DecodeHex(f[2],7900),options_.suite);}
        else if(f[0]=="REPEAT_EACH"){Size(f,3);value.repeat_declarations.Declare(Integer(f[1],128),value.projects.size(),sr::ParseNativeRepeatCount(f[2],0));}
        else if(SnapshotDeclaration(value,f)){}
        else if(f[0]=="ROOT_TIMEOUT"){Size(f,2);value.default_timeouts.Root(Integer(f[1],120000));}
        else if(f[0]=="PROJECT_TIMEOUT"){Size(f,3);const auto id=Integer(f[1],128);if(!id||id>value.projects.size())throw std::runtime_error("Native project timeout has no actual declaration");value.default_timeouts.Project(id-1,Integer(f[2],120000));}
        else if(f[0]=="FULLY_PARALLEL"){
          Size(f,2);if(value.fully_parallel.has_value())throw std::runtime_error("Duplicate native fully-parallel declaration");value.fully_parallel=Integer(f[1],1)!=0;
        }
        else if(f[0]=="PROJECT_PARALLEL"){
          Size(f,3);const auto id=Integer(f[1],128);if(!id||id>value.projects.size()||!value.project_parallel.emplace(id-1,Integer(f[2],1)!=0).second)throw std::runtime_error("Invalid native project fully-parallel declaration");
        }
        else if(f[0]=="PROJECT_DEP"){
          Size(f,3);const auto id=Integer(f[1],128);if(!id||id>value.projects.size()||value.projects[id-1].dependencies.size()>=128)throw std::runtime_error("Native project dependency owner/capacity rejected");value.projects[id-1].dependencies.push_back(sr::DecodeHex(f[2],256));
        }
        else if(f[0]=="PROJECT_TEARDOWN"){
          Size(f,3);const auto id=Integer(f[1],128);if(!id||id>value.projects.size()||value.projects[id-1].teardown)throw std::runtime_error("Native project teardown owner rejected");value.projects[id-1].teardown=sr::DecodeHex(f[2],256);
        }
        else if(f[0]=="PROJECT_TEST"){
          Size(f,3);const auto project=Integer(f[1],128),test=Integer(f[2]);if(!project||project>value.projects.size()||!value.titles.contains(test)||!value.project_tests.emplace(test,project-1).second)throw std::runtime_error("Native project test membership rejected");
        }
        else if(f[0]=="GREP_GLOBAL_TAGS"){
          Size(f,2);if(value.grep_global_tag_count)throw std::runtime_error("Duplicate native global grep tag declaration");value.grep_global_tag_count=Integer(f[1],64);
        }
        else if(f[0]=="GREP_GLOBAL_TAG"){
          Size(f,3);const auto index=Integer(f[1],64);const auto tag=sr::GrepUnits(Scalar(f[2],256),256);
          if(!value.grep_global_tag_count||index!=value.grep_global_tags.size()||index>=*value.grep_global_tag_count||tag.empty()||tag.front()!='@')throw std::runtime_error("Native global grep tag owner/order/capacity rejected");
          value.grep_global_tags.push_back(tag);
        }
        else if(f[0]=="GREP_CONFIG"||f[0]=="GREP_REGEXP"){
          const bool declaration=f[0]=="GREP_CONFIG";Size(f,declaration?4:5);const auto owner=Integer(f[1],128);
          if(owner>value.projects.size()||(f[2]!="include"&&f[2]!="exclude"))throw std::runtime_error("Native grep config owner/kind rejected");
          const bool include=f[2]=="include";auto& layer=owner?value.grep_config.projects[owner-1]:value.grep_config.root;
          auto& patterns=include?layer.include:layer.exclude;const auto key=std::pair(static_cast<size_t>(owner),include);
          if(declaration){
            if(patterns||!value.grep_expected.emplace(key,Integer(f[3],128)).second)throw std::runtime_error("Duplicate native grep config field");
            patterns.emplace();
          }else{
            const auto expected=value.grep_expected.find(key);
            if(!patterns||expected==value.grep_expected.end()||patterns->size()>=expected->second)throw std::runtime_error("Native grep RegExp lacks actual config declaration");
            const auto flags=f[4]=="-"?std::string{}:f[4];if(flags.size()>8)throw std::runtime_error("Native grep RegExp flag capacity exceeded");
            patterns->push_back({sr::GrepUnits(Scalar(f[3],4096)),flags});
          }
        }
        else if(f[0]=="GREP_COMPONENT"){
          Size(f,5);const auto id=Integer(f[1]),position=Integer(f[2],64);
          if(!value.titles.contains(id))throw std::runtime_error("Native grep component lacks actual test declaration");
          auto& components=value.grep_components[id];
          if(position!=components.size()||components.size()>=65)throw std::runtime_error("Native grep component order/capacity rejected");
          components.push_back({sr::GrepUnits(Scalar(f[3],4096)),static_cast<size_t>(Integer(f[4],64)),{}});
        }
        else if(f[0]=="GREP_TAG"){
          Size(f,4);const auto id=Integer(f[1]),position=Integer(f[2],64);const auto found=value.grep_components.find(id);
          if(found==value.grep_components.end()||position>=found->second.size())throw std::runtime_error("Native grep tag lacks actual declaring component");
          auto& part=found->second[position];const auto tag=sr::GrepUnits(Scalar(f[3],256),256);
          if(tag.empty()||tag.front()!='@'||part.tags.size()>=part.expected_tags)throw std::runtime_error("Native grep declared tag rejected");
          part.tags.push_back(tag);
        }
        else if(f[0]=="TEST") {if(options_.directory_admission&&options_.directory_admission->collection&&shared_->directory_declarations>=4096)throw std::runtime_error("Native global directory declaration capacity4096 exceeded");Size(f,7);const auto id=Integer(f[1]);if(!id||value.tests.size()>=4096||value.titles.contains(id))throw std::runtime_error("Invalid test metadata");value.titles[id]=Scalar(f[5],4096);value.declared_timeouts[id]=f[3]=="-"?std::nullopt:std::optional<uint64_t>(Integer(f[3],120000));value.tests.push_back({id,std::to_string(id),Names(f[6]),static_cast<unsigned>(Integer(f[2],100)),Integer(f[4],1)!=0});if(options_.directory_admission&&options_.directory_admission->collection)++shared_->directory_declarations;}
        else if(f[0]=="ONLY"){Size(f,2);const auto id=Integer(f[1]);if(!value.titles.contains(id)||std::find(value.focused.begin(),value.focused.end(),id)!=value.focused.end())throw std::runtime_error("Invalid native focused declaration metadata");value.focused.push_back(id);}
        else if(f[0]=="SKIP"){Size(f,3);const auto id=Integer(f[1]);if(!value.titles.contains(id)||!value.skipped.emplace(id,Scalar(f[2],8192)).second)throw std::runtime_error("Invalid native static skip metadata");}
        else if(f[0]=="SUITE"){Size(f,5);const auto titleId=Integer(f[1]);if(!titleId||!value.suite_titles.emplace(titleId,Scalar(f[4],4096)).second)throw std::runtime_error("Invalid native suite title metadata");if(value.suites.size()>=256)throw std::runtime_error("Native suite declaration capacity exceeded");value.suites.push_back({Integer(f[1]),Integer(f[2]),static_cast<uint32_t>(Integer(f[3],4096)),{}});}
        else if(f[0]=="FLOCK"||f[0]=="TLOCK"||f[0]=="SLOCK"){
          Size(f,f[0]=="FLOCK"?2:3);auto* names=&value.file_locks;
          if(f[0]!="FLOCK"){const auto owner=Integer(f[1]);if(f[0]=="TLOCK"){if(!value.titles.contains(owner))throw std::runtime_error("Unknown native test lock declaration owner");names=&value.test_locks[owner];}else{if(!value.suite_titles.contains(owner))throw std::runtime_error("Unknown native suite lock declaration owner");names=&value.suite_locks[owner];}}
          if(names->size()>=64)throw std::runtime_error("Native resource-lock declaration capacity64 exceeded");
          names->push_back(sr::ResourceLockName(f.back()));
        }
        else if(f[0]=="SMODE"){
          Size(f,3);const auto id=Integer(f[1]);const auto mode=f[2]=="default"?sr::WorkGroupMode::Default:f[2]=="parallel"?sr::WorkGroupMode::Parallel:sr::WorkGroupMode::Serial;
          if((f[2]!="default"&&f[2]!="serial"&&f[2]!="parallel")||!value.suite_titles.contains(id)||!value.suite_modes.emplace(id,mode).second)throw std::runtime_error("Invalid native suite mode declaration");
          if(mode==sr::WorkGroupMode::Serial)value.serial_suites.insert(id);
        }
        else if(f[0]=="SONLY"||f[0]=="SSKIP"){
          Size(f,f[0]=="SONLY"?2:3);const auto id=Integer(f[1]);
          if(!value.suite_titles.contains(id))throw std::runtime_error("Unknown native suite modifier owner");
          auto& modifiers=value.suite_modifiers[id];
          if(f[0]=="SONLY"){if(modifiers.only)throw std::runtime_error("Duplicate native focused suite modifier");modifiers.only=true;}
          else{if(modifiers.skip)throw std::runtime_error("Duplicate native skipped suite modifier");modifiers.skip=Scalar(f[2],8192);}
        }
        else if(f[0]=="MEMBER"){Size(f,4);if(value.members.size()>=4096)throw std::runtime_error("Native suite member capacity exceeded");value.members.push_back({Integer(f[1]),Integer(f[2]),static_cast<uint32_t>(Integer(f[3],4096))});}
        else if(f[0]=="SHOOK"){
          Size(f,6);const auto owner=Integer(f[1]),callback=Integer(f[3],8192);const auto kind=HookKind(f[2]);
          if(!callback||value.nested_hooks.size()>=8192||!value.nested_hooks.emplace(callback,NestedHookSpec{kind,{callback,f[4]=="-"?std::nullopt:std::optional<uint64_t>(Integer(f[4],120000)),Names(f[5])}}).second)throw std::runtime_error("Invalid native nested hook metadata");
          const auto suite=std::find_if(value.suites.begin(),value.suites.end(),[&](const auto& entry){return entry.id==owner;});
          if(suite==value.suites.end())throw std::runtime_error("Unknown native nested hook owner");
          suite->hooks.push_back({kind==sr::HookKind::BeforeAll?sr::SuiteHookKind::BeforeAll:kind==sr::HookKind::BeforeEach?sr::SuiteHookKind::BeforeEach:kind==sr::HookKind::AfterEach?sr::SuiteHookKind::AfterEach:sr::SuiteHookKind::AfterAll,callback});
        }
        else if(f[0]=="HOOK"){Size(f,5);auto& hooks=value.hooks[HookKind(f[1])];const auto callback=Integer(f[2],256);if(callback!=hooks.size()+1)throw std::runtime_error("Invalid native hook callback order");hooks.push_back({callback,f[3]=="-"?std::nullopt:std::optional<uint64_t>(Integer(f[3],120000)),Names(f[4])});}
        else if(f[0]=="LOADED"){Size(f,1);loaded=true;}
        else if(f[0]=="HOST_ERROR"){Size(f,2);throw std::runtime_error("Native suite module failed: "+Scalar(f[1],8192));}
        else throw std::runtime_error("Unexpected native metadata packet");
        if(value.signature.size()+line.size()+1>16777216)throw std::runtime_error("Native metadata capacity16MiB exceeded");
        if(options_.directory_admission&&options_.directory_admission->collection){if(line.size()+1>16777216-shared_->directory_protocol_bytes)throw std::runtime_error("Native global directory protocol capacity16MiB exceeded");shared_->directory_protocol_bytes+=line.size()+1;}
        value.signature+=line+'\n';
      }
      if(host_->Exited())throw std::runtime_error("Actual host exited during module loading");
      if(host_->ControlClosed())throw std::runtime_error("Actual host control socket closed during module loading");
      if(sr::DedicatedRealMilliseconds()>=deadline)throw std::runtime_error("Native module load original deadline expired");
    }
    if(FileOwner()&&FileOwner()->Open()&&!provisional_serial_file_)FileOwner()->Validate();
    if(!value.fixture_layers.empty()){
      std::vector<sr::FixtureValueDeclaration> values;for(auto&[id,record]:value.fixture_value_declarations){(void)id;values.push_back(std::move(record));}
      value.fixture_values.emplace(std::move(value.fixture_layers),std::move(values),std::move(value.fixture_option_overrides));value.fixture_catalog=value.fixture_values->Graph();value.fixtures=value.fixture_catalog->Definitions();
      for(auto& test:value.tests)test.fixtures=value.fixture_catalog->Roots(test.fixtures);
      for(auto&[kind,hooks]:value.hooks){(void)kind;for(auto& hook:hooks)hook.fixtures=value.fixture_catalog->Roots(hook.fixtures);}
      for(auto&[id,hook]:value.nested_hooks){(void)id;hook.spec.fixtures=value.fixture_catalog->Roots(hook.spec.fixtures);}
    }
    if(value.fixture_catalog){
      value.fixture_timeouts.emplace(*value.fixture_catalog,value.fixture_timeout_declarations);
      std::map<uint64_t,std::string> resolved_titles;
      for(const auto& version:value.fixture_catalog->Versions()){
        const auto raw=value.fixture_titles.find(version.id);
        resolved_titles.emplace(version.id,raw!=value.fixture_titles.end()?raw->second:version.previous?resolved_titles.at(version.previous):Quote(version.name));
        for(auto& fixture:value.fixtures)if(fixture.name==version.key){fixture.timeout=value.fixture_timeouts->Get(version.id).timeout;fixture.title_json=resolved_titles.at(version.id);}
      }
    }else{
      for(const auto& timing:value.fixture_timeout_declarations){auto& fixture=value.fixtures.at(timing.declaration-1);if(fixture.timeout)throw std::runtime_error("Duplicate native fixture timeout declaration");fixture.timeout=timing.timeout;}
      for(const auto&[id,title]:value.fixture_titles)value.fixtures.at(id-1).title_json=title;
    }
    for(const auto& test:value.tests){const auto member=value.project_tests.find(test.id);if(!value.projects.empty()&&member==value.project_tests.end())throw std::runtime_error("Native test timeout lacks actual project membership");value.timeouts.emplace(test.id,value.default_timeouts.Test(value.declared_timeouts.at(test.id),member==value.project_tests.end()?std::nullopt:std::optional<size_t>(member->second)));}
    sr::FixtureGraph graph(value.fixtures);for(const auto& test:value.tests)graph.Plan(test.fixtures);
    size_t hook_count=0;for(const auto&[kind,hooks]:value.hooks){(void)kind;hook_count+=hooks.size();for(const auto& hook:hooks)graph.Plan(hook.fixtures);}if(hook_count>256)throw std::runtime_error("Native hook declaration capacity256 exceeded");
    if(!value.suites.empty()){
      if(!value.hooks.empty()||value.members.size()!=value.tests.size())throw std::runtime_error("Native nested declarations cannot mix flat hooks or omit memberships");
      sr::NativeSuiteTree tree(value.suites,value.members);
      const auto modifiers=tree.ResolveModifiers(value.suite_modifiers,std::set<uint64_t>(value.focused.begin(),value.focused.end()),value.skipped);
      value.has_suite_focus=modifiers.has_focus;value.focused=modifiers.focused;value.skipped=modifiers.skipped;
      for(const auto& [id,hook]:value.nested_hooks){(void)id;graph.Plan(hook.spec.fixtures);}
      std::map<uint64_t,sr::TestCase> ordered;for(const auto& test:value.tests)ordered.emplace(test.id,test);
      value.tests.clear();for(const auto id:tree.TestOrder()){const auto test=ordered.find(id);if(test==ordered.end())throw std::runtime_error("Native suite membership has no genuine declaration");value.tests.push_back(test->second);}
    }else if(!value.members.empty()||!value.nested_hooks.empty())throw std::runtime_error("Native suite root is absent");
    value.project_output_roots.Freeze(value.projects.size());value.repeat_declarations.Freeze(value.projects.size());
    ValidateParallelModes(value);
    if(HasResourceLocks(value))(void)ResourceDeclarations(value);
    ValidateDirectorySegment(value);
    const auto* previous=catalog_?&*catalog_:metadata_?&*metadata_:nullptr;
    if(previous&&previous->signature!=value.signature)throw std::runtime_error("Actual retry worker suite metadata changed");
    if(value.grep_global_tag_count&&value.grep_global_tags.size()!=*value.grep_global_tag_count)throw std::runtime_error("Native global grep tag declaration is incomplete");
    for(const auto& [key,expected]:value.grep_expected){
      const auto& layer=key.first?value.grep_config.projects.at(key.first-1):value.grep_config.root;
      const auto& patterns=key.second?layer.include:layer.exclude;
      if(!patterns||patterns->size()!=expected)throw std::runtime_error("Native grep config declaration is incomplete");
    }
    if(!value.grep_components.empty()||options_.grep.Active()||value.grep_config.Active()){
      std::vector<uint64_t> ids;for(const auto& test:value.tests)ids.push_back(test.id);sr::ValidateGrepComponents(value.grep_components,ids);
      const auto file=sr::GrepUTF8(sr::GrepFileComponent(options_.suite,options_.test_directory));
      size_t grep_title_bytes=0;
      for(const auto id:ids){std::u16string project;
        if(!value.projects.empty()){
          const auto member=value.project_tests.find(id);if(member==value.project_tests.end()||member->second>=value.projects.size())throw std::runtime_error("Native grep project membership incomplete");
          project=sr::GrepUTF8(value.projects[member->second].name);
        }
        auto composed=sr::GrepJSON(sr::ComposeGrepTitle(project,file,value.grep_components.at(id),value.grep_global_tags));
        if(composed.size()>16777216-grep_title_bytes)throw std::runtime_error("Native composed grep metadata capacity16MiB exceeded");
        grep_title_bytes+=composed.size();
        value.grep_titles.emplace(id,std::move(composed));
      }
      if(!previous&&(options_.grep.Active()||value.grep_config.Active())){
        std::optional<sr::GrepConfiguration> config;
        if(value.grep_config.Active()){config.emplace(value.grep_config);options_.selection.catalog_eligible.emplace();}
        if(options_.grep.Active())options_.selection.eligible.emplace();
        sr::GrepPattern::Work work;work.check_stopped=[this]{ThrowIfStopped();};
        for(const auto id:ids){
          ThrowIfStopped();const auto title=sr::GrepUnits(value.grep_titles.at(id),32768);
          const auto project=value.projects.empty()?std::nullopt:std::optional<size_t>(value.project_tests.at(id));
          if(config&&!config->Matches(title,project,work))continue;
          if(config)options_.selection.catalog_eligible->insert(id);
          if(options_.grep.Active()&&options_.grep.Matches(title,work))options_.selection.eligible->insert(id);
        }
        ThrowIfStopped();
      }
    }
    RecordNativeRepeatConfiguration(value);
    if(!options_.directory_admission&&repeat_collection_timing_&&host_)repeat_collection_timing_->Metadata(worker_index_,host_->Pid(),sr::DedicatedRealMilliseconds());
    if(!catalog_only_&&!options_.directory_admission&&!parallel_actor_&&NeedsNativeRepeatCoordinator(value)){
      FileOwner()->Validate();provisional_serial_file_=false;
      repeat_promoted_=true;catalog_=value;metadata_=std::move(value);metadata_->tests.clear();co_return;
    }
    if(!catalog_only_&&!options_.directory_admission&&!parallel_actor_){
      repeat_collection_timing_.reset();
      if(provisional_serial_file_){FileOwner()->Retire();provisional_serial_file_=false;}
    }
    if(catalog_only_){
      if(!options_.list&&value.projects.empty()&&(!value.project_tests.empty()||!options_.projects.empty()||options_.no_dependencies))throw std::runtime_error("Native project selector requires declared projects");
      catalog_=value;if(!options_.list)CheckNonemptySelection(*catalog_);metadata_=std::move(value);metadata_->tests.clear();co_return;
    }
    if(!previous&&!parallel_actor_)CheckNonemptySelection(value);
    ConfigureWorker(std::move(value));
  }
#include "snapshot_path_binding.inc"
  void ConfigureWorker(Metadata value,bool retained_reusable_worker=false){
    sr::FixtureGraph graph(value.fixtures);
    if(value.fixture_catalog&&!retained_reusable_worker){
      size_t active=0;for(const auto& version:value.fixture_catalog->Versions())if(version.active){
        ++active;std::string aliases;for(const auto& binding:version.bindings){if(!aliases.empty())aliases+=',';aliases+=binding.name+":"+binding.key;}
        if(value.fixture_values){const auto& source=value.fixture_values->Get(version.id);if(source.constant)Send("FCONST "+std::to_string(version.id)+" "+version.key+" "+(source.constant->kind==sr::FixtureLiteral::Kind::Undefined?"U":"J")+" "+(source.origin==sr::FixtureOptionCatalog::Origin::DeclaredLiteral?"L":"C")+" "+Hex(source.constant->json));}
        Send("FVERSION "+std::to_string(version.id)+" "+version.key+" "+(version.scope==sr::Scope::Worker?"worker":"test")+" "+(aliases.empty()?"-":aliases));
      }
      for(const auto&[name,id]:value.fixture_catalog->Visible())Send("FROOT "+name+" "+value.fixture_catalog->Get(id).key);
      Send("FPLAN_READY "+std::to_string(active)+" "+std::to_string(value.fixture_catalog->Visible().size()));
    }

    if(options_.directory_admission){ConfigureDirectorySegment(value,retained_reusable_worker);}
    else if(parallel_actor_&&current_file_group_){
      if(!FileOwner()||current_file_group_->file!=FileOwner()->Token()||!catalog_||!value.projects.empty()||!value.project_tests.empty())throw std::runtime_error("Native anonymous file catalog identity rejected");
      if(!retained_reusable_worker)Send("FILE_CONTEXT "+std::to_string(current_file_group_->file.id)+" "+Hex(sr::AttachmentPathJSON(FileOwner()->Path())));
    }else if(parallel_actor_){
      const auto project=current_group_?std::optional<size_t>(current_group_->project):current_project_?std::optional<size_t>(current_project_->index):std::nullopt;
      if(!project||!catalog_||value.projects.empty()||value.project_tests.size()!=value.tests.size())throw std::runtime_error("Native parallel project catalog identity rejected");
      SendProjectContext(value,*project);
    }else if(!value.projects.empty()){
      if(value.project_tests.size()!=value.tests.size())throw std::runtime_error("Native project declarations omit test membership");
      if(!catalog_){
        std::vector<sr::ProjectTestDeclaration> declarations;for(const auto& test:value.tests)declarations.push_back({test.id,value.project_tests.at(test.id)});
        sr::ProjectGraph project_graph(value.projects);
        const auto selection=sr::SelectProjectTests(project_graph,declarations,options_.selection,options_.projects,options_.no_dependencies,value.focused,sr::ProjectSuiteFocus(value.projects.size(),value.suites,value.suite_modifiers));
        catalog_=value;project_plan_=std::make_unique<sr::SerialProjectPlan>(std::move(project_graph),selection.plan);
        project_selected_tests_=selection.tests;
        for(size_t index=0;index<value.projects.size();++index){const auto& declaration=value.projects[index];sr::ReportProject report;report.id=index+1;report.name_json=Quote(declaration.name);report.metadata_json=value.project_metadata[index];report.default_enabled=declaration.default_enabled;report.selected_test_ids=project_selected_tests_[index];report.selection_role=selection.roles[index];report.dependencies_ignored=options_.no_dependencies;
          for(const auto dependency:project_plan_->Graph().Dependencies(index))report.dependencies.push_back(dependency+1);
          if(declaration.teardown)report.teardown=project_plan_->Graph().Index(*declaration.teardown)+1;
          for(const auto& [test,project]:value.project_tests)if(project==index)report.test_ids.push_back(test);
          report_.projects.push_back(std::move(report));
        }
        current_project_=project_plan_->Next();
        if(current_project_){if(!(pool_?pool_->WorkerStarted(current_project_->generation,worker_index_,host_->Pid()):project_plan_->WorkerStarted(current_project_->generation,host_->Pid())))throw std::runtime_error("Native initial project worker admission rejected");project_worker_pid_=host_->Pid();report_.projects[current_project_->index].workers.push_back(worker_index_);}
      }
      if(current_project_)SendProjectContext(value,current_project_->index);
    }else if(!value.project_tests.empty()||!options_.projects.empty()||options_.no_dependencies)throw std::runtime_error("Native project selector requires declared projects");
    if(!retained_reusable_worker)for(const auto&[id,timeout]:value.timeouts)Send("TEST_TIMEOUT_CONTEXT "+std::to_string(id)+" "+std::to_string(timeout));
    if(!retained_reusable_worker)snapshot_contexts_.clear();
    if(!metadata_){
      std::vector<uint64_t> ids;for(const auto& test:value.tests)ids.push_back(test.id);
      std::vector<size_t> positions;
      if(options_.directory_admission){const auto& requested=options_.directory_admission->tests;const std::set<uint64_t> selected_ids(requested.begin(),requested.end());for(size_t p=0;p<value.tests.size();++p)if(selected_ids.contains(value.tests[p].id))positions.push_back(p);}
      else if(project_plan_||parallel_actor_){if(current_file_group_||current_group_||current_project_){const auto& admitted=current_file_group_?(reuse_plan_?reuse_plan_->Groups().at(current_file_group_->index).tests:file_group_pool_->Plan().Groups().at(current_file_group_->index).tests):current_group_?work_group_pool_->Plan().Groups().at(current_group_->index).tests:project_selected_tests_.at(current_project_->index);const std::set<uint64_t> selected_ids(admitted.begin(),admitted.end());for(size_t position=0;position<value.tests.size();++position)if(selected_ids.contains(value.tests[position].id))positions.push_back(position);}}
      else positions=sr::SelectTestIndices(ids,options_.selection,value.focused,value.has_suite_focus);
      std::vector<sr::TestCase> selected;
      for(const auto position:positions){
        const auto& test=value.tests[position];
        if(project_plan_&&(!current_project_||value.project_tests.at(test.id)!=current_project_->index))continue;
        const auto skipped=value.skipped.find(test.id);
        if(skipped==value.skipped.end())selected.push_back(test);
        else{Charge(value.titles.at(test.id).size()+skipped->second.size()+128);report_.skipped.push_back({test.id,value.titles.at(test.id),skipped->second});}
      }
      value.tests=std::move(selected);metadata_=std::move(value);
    }
    if(retained_reusable_worker){if(!reuse_pool_||!owners_||!owners_->IdleReady()||hook_worker_)throw std::runtime_error("Native retained worker lost ready fixture ownership");return;}
    ConfigureFixtureClocks();
    hook_before_done_=false;hook_after_done_=false;hook_started_=false;hook_stop_seen_=false;hook_stop_cleanup_deadline_=0;hook_results_.clear();
    if(!metadata_->hooks.empty()||!metadata_->suites.empty()){
      hook_worker_=std::make_unique<sr::HookWorker>(metadata_->fixtures,sr::HookWorker::Callbacks{
        [this]{return FixtureNow();},[this](const auto& line){Send(line.starts_with("CALL ")?line+" "+ExpectedStatus()+" "+annotations_.Snapshot():line);},
        [this](const auto& result){if(result.skipped&&result.skip_kind==sr::HookKind::BeforeAll)RecordBeforeAllSkip(result,active_hook_suite_);hook_results_.push_back(result);},
        [this](const auto& error){if(closing_worker_&&!close_failure_){Global(Quote(error));close_failure_=true;}KillHost();},[this](uint64_t token){return FixtureRetirementDeadline(token);},
        [this](uint64_t scope,uint64_t generation){return callback_retries_.CleanupReady(scope,generation);}});
      return;
    }
    owners_=std::make_unique<sr::FixtureOwners>(std::move(graph),sr::FixtureOwners::Hooks{
      [this](const auto& event){const std::string command=event.kind==sr::FixtureOwners::EventKind::Setup?"SETUP":event.kind==sr::FixtureOwners::EventKind::ResumeTeardown?"TEARDOWN":"CANCEL";Send(command+" "+std::to_string(event.token)+" "+std::to_string(event.test)+" "+std::to_string(event.generation)+" "+event.fixture);},
      [this](uint64_t id,uint64_t generation){if(!active_||active_->test.id!=id||active_->generation!=generation||body_started_)throw std::runtime_error("Invalid native body transition");body_started_=true;body_physical_=true;Send("BODY "+std::to_string(id)+" "+std::to_string(generation)+" "+std::to_string(active_->retry)+" passed "+sr::PhaseMilliseconds(std::max(0.0,sr::DedicatedRealMilliseconds()-attempt_started_))+" "+ExpectedStatus()+" "+annotations_.Snapshot());},
      [this](uint64_t id,uint64_t generation,bool ok,const std::string& error){if(active_&&active_->test.id==id&&active_->generation==generation){finished_=true;finish_ok_=ok&&!skip_callback_failed_;if(!skip_callback_failed_)finish_error_=error;}},
      [this](const std::string& error){if(closing_worker_&&!close_failure_){Global(Quote(error));close_failure_=true;}KillHost();},[this](uint64_t token){return FixtureRetirementDeadline(token);}
    });
  }
  void AbortBody(const std::string& error){if(body_physical_&&!abort_sent_){Send("ABORT "+std::to_string(active_->test.id)+" "+std::to_string(active_->generation)+" "+Hex(error));abort_sent_=true;}}
  sr::NativeTask<> Pump(){
    co_await continuation_.Yield(CallbackRetryWake(FixtureNow()+10));
    if(hook_worker_){co_await PumpHooks();co_return;}
    for(const auto& line:host_->Poll(0)){
      const auto f=Fields(line);if(f.empty())throw std::runtime_error("Empty native host control packet");
      const double now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
      TickFixtureClocks(now);ObserveStop(now);
      if(active_&&!finished_&&now>=deadline_){timeout_=true;AbortBody("Test original deadline expired");owners_->Cancel("Test original deadline expired",now);}
      if(sources_&&sources_->Packet(f,now,StopRequested()||closing_worker_||physical_kill_requested_)){}
      else if(FixtureTimeoutPacket(f,now)){}
      else if(callback_retries_.Packet(f,FixtureNow(),StopRequested()||closing_worker_||physical_kill_requested_)){}
      else if(soft_.Packet(f,FixtureNow(),StopRequested()||closing_worker_||physical_kill_requested_)){}
      else if(attachments_.Packet(f,FixtureNow(),StopRequested()||closing_worker_||physical_kill_requested_)){}
      else if(steps_.Packet(f,FixtureNow(),StopRequested()||closing_worker_||physical_kill_requested_)){}
      else if(f[0]=="WORKER_IDLE_READY"){ReusableIdlePacket(f,now);}
      else if(f[0]=="FAIL_REQUEST"){ExpectedFailurePacket(f,now,false);}
      else if(f[0]=="SKIP_REQUEST"){SkipPacket(f,now,false);}
      else if(f[0]=="BODY_SKIPPED"){Size(f,3);const auto id=Integer(f[1]),generation=Integer(f[2]);if(!active_||active_->test.id!=id||active_->generation!=generation||!body_physical_||!runtime_skip_)throw std::runtime_error("Runtime skip settlement has no accepted native request");SettledWithAttachments(0,id,generation,[this](const auto& failure,uint64_t){body_physical_=false;FixtureTransition([&](double now){owners_->Finish(!failure,failure.value_or(""),now);});},now);}
      else if(f[0]=="SET_TIMEOUT"){AmendTimeoutPacket(f,now,false);}
      else if(f[0]=="READY"||f[0]=="STARTED"){Size(f,4);const auto token=Integer(f[1]),id=Integer(f[2]),generation=Integer(f[3]);if(f[0]=="READY"){SourceSetupClosed(f);if(FixtureReadyClock(token,id,generation,now))owners_->Ready(token,id,generation,now);}}
      else if(f[0]=="SETTLED"){Size(f,6);SourceSetupClosed(f);const bool ok=Integer(f[4],1)!=0;const std::string error=ok?"":Scalar(f[5],8192);if(!ok)RememberFlatFixtureError(Integer(f[1]),Integer(f[2]),Integer(f[3]),error);FixtureSettledClock(Integer(f[1]),Integer(f[2]),Integer(f[3]),now);owners_->Settled(Integer(f[1]),Integer(f[2]),Integer(f[3]),ok,error,now);}
      else if(f[0]=="BODY_RESULT"||f[0]=="BODY_RESULT_UTF16"||f[0]=="BODY_RESULT_FROM_STEP_UTF16"){
        const bool from_step=f[0]=="BODY_RESULT_FROM_STEP_UTF16";Size(f,from_step?6:5);const auto id=Integer(f[1]),generation=Integer(f[2]);const bool ok=Integer(f[3],1)!=0;const auto error=ok?"":(f[0]=="BODY_RESULT_UTF16"||from_step)?sr::RuntimeSkipReason(f[4]):Scalar(f[4],8192);const auto source=from_step?Integer(f[5]):0;
        if(from_step){if(ok||!source||!active_||active_->test.id!=id||active_->generation!=generation||!body_physical_)throw std::runtime_error("Invalid native body originating-step failure receipt");steps_.FailureSource(0,id,generation,source);}
        if(active_&&active_->test.id==id&&active_->generation==generation&&body_physical_){if(!ok){actual_error_=error;originating_step_=source;if(runtime_skip_){skip_callback_failed_=true;finish_ok_=false;finish_error_="Actual callback failed after runtime skip";}}SettledWithAttachments(0,id,generation,[this,ok](const auto& failure,uint64_t actual_source){if(failure&&actual_error_.empty()){actual_error_=*failure;originating_step_=actual_source;}body_physical_=false;FixtureTransition([&](double now){owners_->Finish(ok&&!failure,failure.value_or(ok?"":"Actual test callback failed"),now);});},now,error,source);}
      }
      else if(f[0]=="HOST_ERROR"){Size(f,2);throw std::runtime_error("Native host callback protocol failed: "+Scalar(f[1],8192));}
      else throw std::runtime_error("Unknown native host control packet");
    }
    FixtureTransition([&](double now){if(sources_)sources_->Tick(now);soft_.Tick(now);steps_.Tick(now);});
    attachments_.Tick(sr::DedicatedRealMilliseconds());
    FixtureTransition([&](double now){callback_retries_.Tick(now);});
    if(host_->ControlClosed()&&!closing_worker_&&!physical_kill_requested_&&!host_->Exited())co_await RetireClosedControl(false,"Actual host control socket closed before native worker retirement");
  }
  // EOF is not an OS exit receipt. Retain the real worker while it exits,
  // then use the same controller transition as any observed unexpected exit.
  // No new fixture/hook invocation is admitted while this bounded drain runs.
  sr::NativeTask<> RetireClosedControl(bool hooks,const std::string& error){
    host_control_failed_=true;host_control_error_=Quote(error);
    const auto started=sr::DedicatedRealMilliseconds();
    const auto phase_deadline=hooks?hook_group_deadline_:deadline_;
    const auto cleanup_end=std::min(started+1000,phase_deadline);
    attachments_.CancelAll(error,started);
    bool cleanup_kill_requested=false;
    for(;;){
      const auto now=sr::DedicatedRealMilliseconds();
      if(StopRequested()){
        const auto at=StopTime();
        if(!timeout_&&(at?*at<phase_deadline:now<phase_deadline))interrupted_=true;
      }
      if(!interrupted_&&now>=phase_deadline)timeout_=true;
      if(host_->Exited())break;
      if(now>=cleanup_end){
        if(!timeout_&&!interrupted_&&now<phase_deadline)cleanup_kill_requested=true;
        KillHost();
      }
      co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+5);
      if(!host_->Poll(0).empty())throw std::runtime_error("Native host emitted a complete control packet after confirmed EOF");
      attachments_.Tick(sr::DedicatedRealMilliseconds());
    }
    if(!host_->Poll(0).empty())throw std::runtime_error("Native host emitted a complete control packet after confirmed EOF");
    const auto exit=host_->Exited();
    if(cleanup_kill_requested&&exit&&exit->signaled&&exit->code==SIGKILL){
      // A real normal process exit after EOF remains an ordinary failed attempt.
      // Only confirmed signal exit after our bounded cleanup kill is a native
      // infrastructure interruption. Earlier original timeout/SIGINT still win.
      if(!timeout_)interrupted_=true;
      Global(Quote(error+"; actual host exit confirmed SIGKILL after native bounded control cleanup request"));
    }
    co_await HandleExit();
  }
  sr::NativeTask<> RetireOutputs(){
    outputs_.CancelAll("Actual native host worker exited",sr::DedicatedRealMilliseconds());
    const auto end=sr::DedicatedRealMilliseconds()+6000;
    while(outputs_.Physical()){
      outputs_.Tick(sr::DedicatedRealMilliseconds());
      if(sr::DedicatedRealMilliseconds()>=end)throw std::runtime_error("Native output directory physical exit remains unconfirmed at worker retirement");
      if(outputs_.Physical())co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+1);
    }
  }
  sr::NativeTask<bool> PrepareOutput(){
    if(!active_||!host_||host_->Exited())throw std::runtime_error("Native output preparation has no actual attempt/worker");
    const auto start=sr::DedicatedRealMilliseconds();output_preparation_elapsed_=0;
    const sr::OutputDirectoryOwner owner{active_->test.id,active_->generation,worker_index_,host_->Pid()};
    const auto timeout=metadata_->timeouts.at(active_->test.id);
    const auto test_end=attempt_started_+(timeout?static_cast<double>(timeout):3153600000000.0);
    auto end=std::min(test_end,start+120000);if(const auto cap=suite_deadline_.OriginalDeadline())end=std::min(end,*cap);
    deadline_=test_end;bool started=false,context_sent=false,acknowledged=false;std::optional<std::filesystem::path> native_root;
    for(;;){
      const auto now=sr::DedicatedRealMilliseconds();
      const bool stop=StopRequested(),expired=now>=end;
      if(stop||expired||host_->Exited()||host_->ControlClosed()){
        outputs_.Cancel(owner,stop?StopReason():expired?"Native output directory original deadline expired":"Actual host exited before output directory admission",now);
        if(stop)interrupted_=true;else if(expired&&now>=test_end)timeout_=true;
        if(actual_error_.empty())actual_error_=Quote(stop?StopReason():expired?"Native output directory original deadline expired":"Actual host exited before output directory admission");
        artifact_cleanup_failed_=true;finish_ok_=false;finished_=true;
        const auto cleanup_end=now+6000;
        while(outputs_.Physical()){outputs_.Tick(sr::DedicatedRealMilliseconds());if(sr::DedicatedRealMilliseconds()>=cleanup_end)throw std::runtime_error("Native output preparation physical cleanup remains unconfirmed");if(outputs_.Physical())co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+1);}
        output_preparation_elapsed_=sr::DedicatedRealMilliseconds()-start;co_return false;
      }
      if(!context_sent&&!host_->Poll(0).empty())throw std::runtime_error("Actual host sent callback control before native output readiness admission");
      if(!started){if(!native_root)native_root=ActualAttemptOutputRoot();started=outputs_.Begin(owner,*native_root,end,now);}
      outputs_.Tick(sr::DedicatedRealMilliseconds());
      if(const auto* record=outputs_.Find(owner)){
        if(record->status!=sr::OutputDirectoryStatus::Ready){artifact_cleanup_failed_=true;finish_ok_=false;finished_=true;timeout_|=record->status==sr::OutputDirectoryStatus::TimedOut;if(actual_error_.empty())actual_error_=Quote(record->error);output_preparation_elapsed_=sr::DedicatedRealMilliseconds()-start;co_return false;}
        if(!context_sent){Send("OUTPUT_CONTEXT "+std::to_string(record->id)+" "+std::to_string(owner.test_id)+" "+std::to_string(owner.generation)+" "+std::to_string(owner.worker_index)+" "+std::to_string(owner.pid)+" "+Hex(record->path)+" "+std::to_string(record->device)+" "+std::to_string(record->inode));context_sent=true;}
        for(const auto& line:host_->Poll(0)){
          const auto fields=Fields(line);Size(fields,4);
          if(fields[0]!="OUTPUT_CONTEXT_READY"||!context_sent||acknowledged||Integer(fields[1])!=record->id||Integer(fields[2])!=owner.test_id||Integer(fields[3])!=owner.generation)throw std::runtime_error("Native output directory readiness ACK has wrong actual attempt/worker");
          acknowledged=true;
        }
        if(acknowledged){const auto received=sr::DedicatedRealMilliseconds();if(received<end&&outputs_.Assign(owner,received)){output_preparation_elapsed_=received-start;co_return true;}}
      }
      co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+1);
    }
  }
  sr::NativeTask<> RetireAttachments(){
    soft_.WorkerExited(worker_index_,host_->Pid(),sr::DedicatedRealMilliseconds());attachments_.WorkerExited(worker_index_,host_->Pid(),sr::DedicatedRealMilliseconds());
    const auto artifact_cleanup_end=sr::DedicatedRealMilliseconds()+6000;
    while(attachments_.Physical()){
      attachments_.Tick(sr::DedicatedRealMilliseconds());
      if(sr::DedicatedRealMilliseconds()>=artifact_cleanup_end)throw std::runtime_error("Native attachment physical exit remains unconfirmed at worker retirement");
      if(attachments_.Physical())co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+1);
    }
    if(!host_->Exited())throw std::runtime_error("Native callback retry retirement lacks actual host waitpid");
    const auto received=sr::DedicatedRealMilliseconds();
    steps_.WorkerExited(worker_index_,host_->Pid(),received);
    callback_retries_.WorkerExited(worker_index_,host_->Pid(),received);
  }
  sr::NativeTask<> RetireDescendants(){
    FixtureWorkerExited(sr::DedicatedRealMilliseconds());
    co_await RetireAttachments();co_await RetireOutputs();
    if(sources_&&!source_retire_end_)source_retire_end_=sr::DedicatedRealMilliseconds()+6000;
    const auto generation=descendant_generation_;
    if(DescendantsPhysical()){
      if(!descendant_arrived_){if(!shared_->descendants.Arrive(generation,worker_index_,*host_,!attachments_.Physical()&&!outputs_.Physical()&&!soft_.Physical()&&!callback_retries_.Physical(),sr::DedicatedRealMilliseconds()))throw std::runtime_error("Native descendant batch arrival lacks actual host/file retirement");descendant_arrived_=true;if(sources_&&source_bound_)sources_->DomainArrived(shared_->descendants,generation);}
      else if(!shared_->descendants.Arrived(generation,worker_index_,*host_))throw std::runtime_error("Native resumed descendant arrival lost exact generation/index/host/PID receipt");
      while(shared_->descendants.Pending(generation)){if(sources_)sources_->Drain(sr::DedicatedRealMilliseconds());co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+5);}
    }
    co_await RetireSources();
  }
  sr::NativeTask<> CaptureWorker(){
    if(worker_captured_)co_return;
    const auto exit=host_->Exited();if(!exit)throw std::runtime_error("Cannot retire host without actual OS exit");
    FixtureWorkerExited(sr::DedicatedRealMilliseconds());
    co_await RetireAttachments();co_await RetireOutputs();
    if(closing_worker_&&(!active_||finish_ok_)&&!close_failure_&&(exit->signaled||exit->code!=0)){Global(Quote("Actual host worker failed during final teardown"));close_failure_=true;}
    if(!shared_->active_workers)throw std::logic_error("Native active worker reservation underflow");
    if(options_.directory_admission)shared_->directory_worker_bytes+=host_->Stdout().size()+host_->Stderr().size();
    --shared_->active_workers;report_.workers.push_back(sr::ReportWorker{worker_index_,host_->Pid(),exit->signaled,exit->code,host_->Stdout(),host_->Stderr()});worker_captured_=true;last_pid_=host_->Pid();DirectoryHostCaptured();
    size_t bytes=shared_->directory_worker_bytes;for(const auto& worker:report_.workers)if(!options_.directory_admission)bytes+=worker.stdout_bytes.size()+worker.stderr_bytes.size();if(bytes>16777216)throw std::runtime_error("Native worker captured byte budget16MiB exceeded");
  }
  sr::NativeTask<> HandleExit(){if((runtime_skip_||steps_.HasLocalSkipPhysical())&&(body_physical_||steps_.Physical())){skip_callback_failed_=true;finish_ok_=false;if(finish_error_.empty())finish_error_="Actual worker exited before runtime-skip raw callback cleanup receipt";if(actual_error_.empty())actual_error_=Quote(finish_error_);}if(body_physical_)body_physical_=false;if(hook_worker_)hook_worker_->WorkerClosed();else owners_->WorkerClosed();if(active_&&!finished_){finished_=true;finish_ok_=false;finish_error_="Actual host worker exited";}co_await CaptureWorker();}
  sr::NativeTask<> CloseWorker(bool force=false){
    if(!host_)co_return;
    DirectoryHostClosing();
    if(hook_worker_){co_await CloseHookWorker(force);co_return;}
    closing_worker_=true;
    const double end=sr::DedicatedRealMilliseconds()+1500;double close_ready_deadline=0;
    if(force)KillHost();else if(owners_)FixtureTransition([&](double now){owners_->StopWorker(now);});
    while(!host_->Exited()) {
      if(!force&&options_.list){
        // Collection has no callback owners. Never route an unsolicited packet
        // through execution handlers, even while the actual host is closing.
        co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+10);
        for(const auto& line:host_->Poll(0)){
          const auto fields=Fields(line);
          if(!fields.empty()&&fields[0]=="HOST_ERROR"){Size(fields,2);throw std::runtime_error("Native collection close failed: "+Scalar(fields[1],8192));}
          throw std::runtime_error("Unexpected native collection packet after declarations");
        }
      }else if(!force)co_await Pump();else {co_await continuation_.Yield(sr::DedicatedRealMilliseconds()+10);try{host_->Poll(0);}catch(const std::exception&){KillHost();}}
      const double now=FixtureNow();sr::FixtureTransitionTime::Scope event(fixture_event_time_,now);
      TickFixtureClocks(now);if(owners_)owners_->Tick(now);
      if(!force&&!close_sent_&&(!owners_||owners_->State().physical==0)&&!body_physical_){Send("CLOSE");close_sent_=true;close_ready_deadline=now+1000;}
      if((close_ready_deadline&&now>=close_ready_deadline)||(!close_sent_&&now>=end&&(!fixture_runtime_||!fixture_runtime_->Physical())))KillHost();
    }
    try{host_->Poll(0);}catch(const std::exception& error){Global(Quote(error.what()));}
    if(owners_)owners_->WorkerClosed();
    co_await RetireDescendants();
    co_await CaptureWorker();ProjectWorkerExited();host_.reset();owners_.reset();closing_worker_=false;
  }
  sr::WorkerReusePool* reuse_pool_=nullptr;const sr::FileWorkGroups* reuse_plan_=nullptr;std::optional<sr::WorkerReusePool::Key> reuse_key_;std::optional<sr::WorkerReusePool::Borrow> reuse_borrow_;std::optional<sr::WorkerReusePool::IdleRequest> reuse_request_;bool reuse_idle_ack_observed_=false;
  size_t reuse_soft_cursor_=0,reuse_step_cursor_=0,reuse_file_cursor_=0,reuse_declaration_cursor_=0,reuse_output_cursor_=0;
  sr::FileGroupPool* file_group_pool_=nullptr;std::optional<sr::FileGroupPool::Admission> current_file_group_;
  sr::WorkGroupPool* work_group_pool_=nullptr;std::optional<sr::WorkGroupPool::Admission> current_group_;std::vector<unsigned> group_workers_;
  std::shared_ptr<RunState> shared_;std::optional<sr::NativeTask<>> emergency_close_;bool descendant_arrived_=false;sr::NativeContinuation continuation_;sr::ProjectPool* pool_=nullptr;bool parallel_actor_=false,catalog_only_=false;size_t local_errors_=0;unsigned failure_accounted_=0;
  std::unique_ptr<sr::NativeSuiteWorkerPlan> suite_plan_;
  std::unique_ptr<sr::HookWorker> hook_worker_;std::vector<sr::HookPhases::Result> hook_results_;uint64_t hook_scope_nonce_=0,active_hook_suite_=0;double hook_group_deadline_=0,hook_stop_cleanup_deadline_=0;bool hook_before_done_=false,hook_after_done_=false,hook_started_=false,hook_stop_seen_=false,hook_group_active_=false;
  std::vector<std::vector<uint64_t>> project_selected_tests_;
  std::optional<Metadata> catalog_;
  std::unique_ptr<sr::SerialProjectPlan> project_plan_;
  std::optional<sr::SerialProjectPlan::Admission> current_project_;
  int64_t project_worker_pid_=0;uint64_t attempt_nonce_=0;unsigned project_failures_=0;
  uint64_t mutation_nonce_=0;bool groups_reported_=false;
  bool provisional_serial_file_=false,repeat_promoted_=false;std::shared_ptr<sr::DirectoryActorTiming> repeat_collection_timing_;
  sr::PhaseErrorAttribution phase_error_attribution_;std::vector<sr::AttemptSecondaryError> attempt_secondary_errors_;
  uint64_t descendant_generation_=0;sr::NativeInterruptLatch& interrupt_;Options options_;sr::NativeSuiteDeadline& suite_deadline_;sr::RunnerReport directory_report_;std::optional<sr::TestListingCatalog> directory_catalog_;std::unique_ptr<sr::SuiteFileOwner> directory_file_;sr::RunnerReport& report_;sr::RunnerSteps steps_;sr::RunnerSoftAssertions soft_;sr::RunnerCallbackRetries callback_retries_;sr::RunnerAttachments attachments_;sr::RunnerOutputDirectories outputs_;sr::FixtureTransitionTime fixture_event_time_;std::unique_ptr<sr::FixtureTimeoutRuntime> fixture_runtime_;std::map<uint64_t,sr::FixtureClockOwner> fixture_slot_owners_;std::shared_ptr<sr::FixtureAttemptClocks> attempt_timing_;bool fixture_mutation_failed_=false;std::string fixture_mutation_error_;double output_preparation_elapsed_=0;sr::ModifierAnnotations annotations_;std::optional<Metadata> metadata_;std::unique_ptr<sr::RunnerRetryPlan> plan_;std::unique_ptr<sr::DedicatedHostProcess> host_;std::unique_ptr<sr::RunnerRetainedSources> sources_;sr::RetainedSourceLeases::Host source_host_{};bool source_bound_=false,source_retired_=false,source_cleanup_reported_=false,source_reported_=false;std::optional<double> source_retire_end_;std::unique_ptr<sr::FixtureOwners> owners_;std::optional<sr::Attempt> active_;unsigned worker_index_=0;int64_t last_pid_=0;uint64_t before_all_skip_cause_=0;bool soft_hard_failure_=false,soft_cleanup_failed_=false,soft_infrastructure_failed_=false;bool runtime_skip_=false,skip_callback_failed_=false,artifact_cleanup_failed_=false,runtime_expected_failure_=false;std::optional<std::string> skip_reason_json_,expected_failure_reason_json_;bool host_control_failed_=false;std::string host_control_error_;bool worker_captured_=false,close_sent_=false,abort_sent_=false,body_started_=false,body_physical_=false,finished_=false,finish_ok_=false,timeout_=false,closing_worker_=false,close_failure_=false,physical_kill_requested_=false,interrupted_=false,stop_recorded_=false;double deadline_=0,body_cleanup_deadline_=0,attempt_started_=0;std::string actual_error_,finish_error_,soft_hard_error_;uint64_t originating_step_=0,primary_soft_error_=0;bool callback_infrastructure_failed_=false;std::string callback_error_;size_t callback_record_cursor_=0;std::optional<sr::RunnerRetainedSources::CallbackIterationOwner> callback_source_owner_;
};
}
int main(int argc,char** argv){
  if(argc==2&&std::string_view(argv[1])=="--native-output-retention-worker")return sr::output_retention_files::Worker();
  if(argc==2&&std::string_view(argv[1])=="--native-output-directory-worker")return sr::output_directory_files::Worker();
  if(argc==2&&std::string_view(argv[1])=="--native-output-directory-cleanup")return sr::output_directory_files::Cleanup();
  if(argc==2&&std::string_view(argv[1])=="--native-attachment-worker")return sr::attachment_files::Worker();
  if(argc==2&&std::string_view(argv[1])=="--native-attachment-cleanup")return sr::attachment_files::Cleanup();
  try {
    Options options;
    std::map<std::string,std::string*> paths{{"--node",&options.node},{"--host-script",&options.host},
      {"--suite",&options.suite},{"--test-directory",&options.test_directory},{"--report",&options.report},{"--junit",&options.junit},{"--html",&options.html}};
    std::set<std::string> seen;
    for(int i=1;i<argc;++i){
      const std::string key=argv[i];
      if(key=="--fail-on-flaky-tests"){
        if(!seen.insert(key).second)throw std::runtime_error("Duplicate native runner option");
        options.fail_on_flaky_tests=true;continue;
      }
      if(key=="--pass-with-no-tests"){
        if(!seen.insert(key).second)throw std::runtime_error("Duplicate native runner option");
        options.pass_with_no_tests=true;continue;
      }
      if(key=="--list"){
        if(!seen.insert(key).second)throw std::runtime_error("Duplicate native runner option");
        options.list=true;continue;
      }
      if(key=="--no-deps"){
        if(!seen.insert(key).second)throw std::runtime_error("Duplicate native runner option");
        options.no_dependencies=true;continue;
      }
      if(key=="--forbid-only"){
        if(!seen.insert(key).second)throw std::runtime_error("Duplicate native runner option");
        options.selection.forbid_only=true;continue;
      }
      if(i+1>=argc)throw std::runtime_error("Missing native runner option value");
      const std::string value=argv[++i];
      if(key=="--grep"||key=="-g"||key=="--grep-invert"||key=="-G"){
        if(value.size()>16384)throw std::runtime_error("Native grep CLI UTF8 argument capacity exceeded");
        (key=="--grep"||key=="-g"?options.grep.include_argument:options.grep.exclude_argument)=value;continue;
      }
      if(key=="--project"){
        if(options.projects.size()>=128||value.empty()||value.size()>256)throw std::runtime_error("Native project selector name/capacity rejected");
        options.projects.push_back(value);continue;
      }
      if(key=="--test-id"){
        if(options.selection.ids.size()>=4096)throw std::runtime_error("Native selected identity capacity4096 exceeded");
        options.selection.ids.push_back(Integer(value));continue;
      }
      if(!seen.insert(key).second)throw std::runtime_error("Duplicate native runner option");
      if(key=="--reporter"){options.terminal_reporter=sr::ParseNativeTerminalReporter(value);continue;}
      if(key=="--preserve-output"){options.preserve_output=sr::NativePreserveOutput(value);continue;}
      if(key=="--test-dir"){if(!options.discovery)options.discovery.emplace();options.discovery->directory=sr::NativeDiscoveryDirectory(value);continue;}
      if(key=="--test-match-json"||key=="--test-ignore-json"||key=="--file-filters-json"){if(!options.discovery)options.discovery.emplace();sr::NativeDiscoveryConfiguration(*options.discovery,key,value);continue;}
      if(key=="--output-directory"){options.output_directory=sr::NativeOutputDirectoryRoot(value);continue;}
      if(key=="--repeat-each"){options.repeat_each=sr::ParseNativeRepeatCount(value,0);continue;}
      if(key=="--workers"){options.workers=static_cast<unsigned>(Integer(value,8));if(!options.workers)throw std::runtime_error("Native parallel project workers require1..8");continue;}
      if(key=="--global-timeout"){options.global_timeout=Integer(value,2147483647);continue;}
      if(key=="--max-failures"){options.max_failures=static_cast<unsigned>(Integer(value,4096));continue;}
      if(key=="--shard"){
        const auto slash=value.find('/');
        if(slash==std::string::npos)throw std::runtime_error("Native shard must be current/total");
        options.selection.shard_current=static_cast<uint32_t>(Integer(value.substr(0,slash),65536));
        options.selection.shard_total=static_cast<uint32_t>(Integer(value.substr(slash+1),65536));
        if(!options.selection.shard_current||options.selection.shard_current>options.selection.shard_total)
          throw std::runtime_error("Native shard must satisfy 1<=current<=total<=65536");
        continue;
      }
      const auto path=paths.find(key);
      if(path==paths.end())throw std::runtime_error("Invalid native runner option");
      *path->second=value;
      if(value.empty()||value.front()!='/')throw std::runtime_error("Native runner paths must be absolute");
    }
    if(options.node.empty()||options.host.empty()||(!options.discovery&&options.suite.empty())||options.report.empty())
      throw std::runtime_error("Required: --node --host-script --suite --report");
    if(options.list&&(!options.junit.empty()||!options.html.empty()||!options.output_directory.empty()||seen.contains("--max-failures")))
      throw std::runtime_error("Native --list cannot use execution reports, output directories or max-failures");
    std::set<std::filesystem::path> outputs;
    for(const auto* path:{&options.report,&options.junit,&options.html})
      if(!path->empty()&&!outputs.insert(std::filesystem::path(*path).lexically_normal()).second)
        throw std::runtime_error("Native report output paths must be distinct");
    if(options.discovery){if(options.discovery->directory.empty()||!options.suite.empty())throw std::runtime_error("Native discovery requires --test-dir and excludes --suite");if(options.test_directory.empty())options.test_directory=options.discovery->directory;}
    options.grep.Compile();
    return Runner(std::move(options)).Run();
  }
  catch(const std::exception& error){std::cerr<<"Native runner: "<<error.what()<<'\n';return 2;}
}
