// SPDX-License-Identifier: MIT
#pragma once
#include "runner_report.h"

namespace suitecut::runner {
namespace html_detail {
inline void Text(report_detail::AtomicFile& file, std::string_view text) {
  std::string buffer;
  buffer.reserve(4096);
  for (const char c : text) {
    if (c == '&') buffer += "&amp;";
    else if (c == '<') buffer += "&lt;";
    else if (c == '>') buffer += "&gt;";
    else if (c == '"') buffer += "&quot;";
    else if (c == '\'') buffer += "&#39;";
    else buffer += c;
    if (buffer.size() >= 4090) { file.Put(buffer); buffer.clear(); }
  }
  file.Put(buffer);
}
inline void Field(report_detail::AtomicFile& file, std::string_view name,
                  const std::string& checked_json) {
  const auto visible = report_detail::XML(checked_json);
  file.Put("<div class=\"field\"><strong>"); Text(file, name);
  file.Put("</strong><pre>"); file.Put(visible.text); file.Put("</pre>");
  if (visible.sanitized) {
    file.Put("<p>Display replacements: "); file.NumberValue(visible.sanitized);
    file.Put(". Original UTF16 JSON string:</p><pre class=\"original-json\">");
    Text(file, checked_json); file.Put("</pre>");
  }
  file.Put("</div>");
}
inline void Number(report_detail::AtomicFile& file, std::string_view name, auto value) {
  file.Put("<dt>"); Text(file, name); file.Put("</dt><dd>");
  file.NumberValue(value); file.Put("</dd>");
}
} // namespace html_detail

// Standalone native report. Browser details widgets expand real attempts and
// actual worker byte streams. No scripts, remote assets, or authored HTML run.
inline void WriteRunnerHTML(const RunnerReport& report, const std::filesystem::path& path) {
  using namespace report_detail;
  const auto summary = Validate(report);
  const auto step_index = IndexSteps(report.steps);
  AtomicFile file(path);
  file.Put(R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'"><title>SuiteCut native test report</title><style>body{font:16px system-ui,sans-serif;max-width:1000px;margin:32px auto;padding:0 20px;color:#202124;background:#fff}h1{font-size:1.8rem}h2{font-size:1.3rem}details{border:1px solid #ccc;border-radius:6px;padding:12px;margin:12px 0}summary{cursor:pointer;overflow-wrap:anywhere}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f4f5f7;padding:12px}dl{display:grid;grid-template-columns:minmax(150px,1fr) 2fr;gap:6px}dd{margin:0}dt{font-weight:600}.unexpected{border-left:5px solid #b3261e}.expected{border-left:5px solid #137333}.skipped{border-left:5px solid #747775}.field{margin-top:12px}.status{font-weight:600}small{color:#555}</style></head><body><h1>SuiteCut native test report</h1><dl id="summary">)");
  html_detail::Number(file, "Tests", summary.total_tests());
  html_detail::Number(file, "Attempts", report.attempts.size());
  html_detail::Number(file, "Expected", summary.expected);
  html_detail::Number(file, "Unexpected", summary.unexpected);
  html_detail::Number(file, "Skipped", summary.skipped);
  html_detail::Number(file, "Did not run", summary.did_not_run);
  html_detail::Number(file, "Flaky", summary.flaky);
  html_detail::Number(file, "Global errors", report.global_errors_json.size());
  html_detail::Number(file, "Total attempt duration (ms)", summary.duration_ms);
  file.Put("</dl>");
  report.run_policy.HTML(file,summary.flaky);
  if(report.repeat_configuration){file.Put("<h2>Native repeat configuration</h2><pre>");html_detail::Text(file,report.repeat_configuration->JSON());file.Put("</pre><p>Configuration metadata; this record is not execution evidence.</p>");}
  file.Put("<h2>Tests</h2>");
  for (const auto& [id, attempts] : summary.tests) {
    const auto& last = *attempts.back();
    const auto title = XML(last.title_json);
    file.Put("<details class=\"");
    file.Put(summary.final_skipped.at(id) ? "skipped" : summary.final_expected.at(id) ? "expected" : "unexpected");
    file.Put("\" data-test-id=\""); file.NumberValue(id);
    file.Put("\"><summary><span class=\"status\">"); file.Put(summary.final_flaky.at(id) && std::any_of(report.serial_groups.begin(),report.serial_groups.end(),[&](const auto& group){return std::find(group.tests.begin(),group.tests.end(),id)!=group.tests.end();}) ? "flaky" : last.status);
    file.Put(summary.final_expected.at(id) ? " · expected" : " · unexpected");
    file.Put("</span> — "); file.Put(title.text); file.Put("</summary>");
    for (const auto* attempt : attempts) {
      file.Put("<section class=\"attempt\" data-generation=\""); file.NumberValue(attempt->generation);
      file.Put("\"><h3>Attempt "); file.NumberValue(attempt->retry + 1);
      file.Put("</h3><dl>");
      html_detail::Number(file, "Retry", attempt->retry);
      html_detail::Number(file, "Worker", attempt->worker_index);
      html_detail::Number(file, "PID", attempt->pid);
      html_detail::Number(file, "Duration (ms)", attempt->duration_ms);
      if(attempt->wall_duration_ms)html_detail::Number(file, "Actual wall duration (ms)", *attempt->wall_duration_ms);
      file.Put("<dt>Status</dt><dd>"); file.Put(attempt->status);
      file.Put(attempt->expected ? " (expected)" : " (unexpected)"); file.Put("</dd></dl>");
      if(!attempt->expected_status.empty()){file.Put("<div><strong>Expected status</strong>: ");file.Put(attempt->expected_status);file.Put("</div>");}
      if(attempt->expected_failure_reason_json)html_detail::Field(file, "Expected failure reason", *attempt->expected_failure_reason_json);
      html_detail::Field(file, "Title", attempt->title_json);
      if(attempt->originating_step){file.Put("<dl>");html_detail::Number(file,"Actual originating failure step",attempt->originating_step);file.Put("</dl>");}
      if(const auto* source=AttemptBoxedSource(*attempt,step_index)){const auto* location=StepErrorAttribution(*source);html_detail::Field(file,"Primary attempt boxed error location",location->file_json);file.Put("<dl>");html_detail::Number(file,"Boxed error line",location->line);html_detail::Number(file,"Boxed error column",location->column);html_detail::Number(file,"Attribution step",source->id);file.Put("</dl><p>Native originating-step receipt; original authored error and stack preserved below.</p>");}
      html_detail::Field(file, "Error", attempt->error_json);
      for(const auto& secondary:attempt->secondary_errors){file.Put("<section class=\"native-secondary-error\"><h4>Associated phase diagnostic</h4><dl>");html_detail::Number(file,"Scope",secondary.source.scope);html_detail::Number(file,"Scope generation",secondary.source.generation);if(secondary.originating_step)html_detail::Number(file,"Originating step",secondary.originating_step);file.Put("</dl>");html_detail::Field(file,"Secondary error",secondary.error_json);file.Put("</section>");}
      if(attempt->skip_reason_json)html_detail::Field(file, "Runtime skip reason", *attempt->skip_reason_json);
      for(const auto& annotation:attempt->annotations){file.Put("<section class=\"native-annotation\"><strong>Actual runtime modifier: ");file.Put(annotation.type);file.Put("</strong>");if(annotation.description_present)html_detail::Field(file,"Description",annotation.reason_json);if(annotation.location){html_detail::Field(file,"Source file",annotation.location->file_json);file.Put("<dl>");html_detail::Number(file,"Line",annotation.location->line);html_detail::Number(file,"Column",annotation.location->column);file.Put("</dl>");}file.Put("</section>");}
      file.Put("</section>");
    }
    file.Put("</details>");
  }
  file.Put("<h2>Skipped by declaration</h2><p>These selected declarations were skipped before callbacks or fixtures started.</p>");
  for(const auto& declaration:report.skipped){
    file.Put("<details class=\"declared-skip skipped\" data-test-id=\"");file.NumberValue(declaration.test_id);file.Put("\"><summary>Skipped — ");file.Put(XML(declaration.title_json).text);file.Put("</summary>");
    html_detail::Field(file,"Title",declaration.title_json);html_detail::Field(file,"Reason",declaration.reason_json);file.Put("</details>");
  }
  file.Put("<h2>Runtime beforeAll skip causes</h2>");
  for(const auto& cause:report.skip_causes){file.Put("<details class=\"runtime-skip-cause\" data-cause-id=\"");file.NumberValue(cause.scope_id);file.Put("\"><summary>Actual beforeAll skip request</summary>");html_detail::Field(file,"Reason",cause.reason_json);file.Put("<dl>");html_detail::Number(file,"Associated test",cause.test_id);html_detail::Number(file,"Associated generation",cause.generation);html_detail::Number(file,"Scope generation",cause.scope_generation);html_detail::Number(file,"Suite",cause.suite_id);html_detail::Number(file,"Callback",cause.callback_id);html_detail::Number(file,"Callback token",cause.callback_token);html_detail::Number(file,"Worker",cause.worker_index);html_detail::Number(file,"PID",cause.pid);file.Put("</dl><div>Actual scope status: ");file.Put(cause.status);file.Put("</div></details>");}
  file.Put("<h2>Skipped by runtime beforeAll</h2><p>These declarations have no actual attempt, duration or worker attribution.</p>");
  for(const auto& declaration:report.runtime_skipped){file.Put("<details class=\"runtime-skip skipped\" data-test-id=\"");file.NumberValue(declaration.test_id);file.Put("\"><summary>Skipped — ");file.Put(XML(declaration.title_json).text);file.Put("</summary><dl>");html_detail::Number(file,"Cause",declaration.cause_id);file.Put("</dl></details>");}
  file.Put("<h2>Did not run</h2><p>These declared tests have no actual attempt, duration or worker attribution.</p>");
  for (const auto& declaration : report.unrun) {
    file.Put("<details class=\"did-not-run\" data-test-id=\"");file.NumberValue(declaration.test_id);file.Put("\"><summary>Did not run — ");file.Put(XML(declaration.title_json).text);file.Put("</summary>");
    html_detail::Field(file,"Title",declaration.title_json);html_detail::Field(file,"Reason",declaration.reason_json);file.Put("</details>");
  }
  if(!report.callback_assertions.empty()){
    file.Put("<h2>Retried callback assertions</h2>");
    for(const auto& record:report.callback_assertions){
      file.Put("<details class=\"native-callback-assertion\" data-assertion-id=\"");file.NumberValue(record.id);
      file.Put("\"><summary>");file.Put(CallbackAssertionStatusName(record.status));file.Put(" · ");
      file.NumberValue(record.attempts);file.Put(" invocation(s)</summary><dl>");
      html_detail::Number(file,"Associated test",record.owner.test_id);
      html_detail::Number(file,"Actual attempt generation",record.owner.attempt_generation);
      html_detail::Number(file,"Parent step",record.parent);
      html_detail::Number(file,"Started (monotonic ms)",record.started);
      html_detail::Number(file,"Deadline (monotonic ms)",record.deadline);
      html_detail::Number(file,"Physical receipt (monotonic ms)",record.received);
      file.Put("</dl>");html_detail::Field(file,"Error",record.error_json);file.Put("</details>");
    }
  }
  file.Put("<h2>Actual callback steps</h2>");
  for(const auto& step:report.steps){file.Put("<details class=\"native-step\" data-step-id=\"");file.NumberValue(step.id);file.Put("\"><summary>");file.Put(StepStatusName(step.status));file.Put(" — ");file.Put(XML(step.title_json).text);file.Put("</summary><dl>");if(step.callback_iteration){html_detail::Number(file,"Callback assertion",step.callback_iteration->assertion);html_detail::Number(file,"Callback invocation",step.callback_iteration->invocation);}html_detail::Number(file,"Parent step",step.parent);html_detail::Number(file,"Associated test",step.owner.test_id);html_detail::Number(file,"Actual attempt generation",step.owner.attempt_generation);html_detail::Number(file,"Callback token",step.owner.callback_token);html_detail::Number(file,"Scope",step.owner.scope);html_detail::Number(file,"Scope generation",step.owner.scope_generation);html_detail::Number(file,"Worker",step.owner.worker_index);html_detail::Number(file,"PID",step.owner.pid);html_detail::Number(file,"Step timeout (ms; 0 means no extra cap)",step.timeout_ms);html_detail::Number(file,"Duration (ms)",*step.ended-step.started);if(step.receipt!=StepReceipt::NotInvoked)html_detail::Number(file,"Physical duration (ms)",*step.received-step.started);file.Put("</dl><div>Physical receipt: ");file.Put(step.receipt==StepReceipt::Settled?"actual callback settled":step.receipt==StepReceipt::NotInvoked?"author callback deliberately not invoked":"actual worker exited");file.Put("</div>");html_detail::Field(file,"Actual step title",step.title_json);if(const auto* location=StepErrorAttribution(step)){html_detail::Field(file,"Primary boxed error location",location->file_json);file.Put("<dl>");html_detail::Number(file,"Boxed error line",location->line);html_detail::Number(file,"Boxed error column",location->column);html_detail::Number(file,"Attribution step",step.id);file.Put("</dl><p>Genuine captured runtime caller; original authored error and stack preserved below.</p>");}if(step.location){html_detail::Field(file,"Step source file",step.location->file_json);file.Put("<dl>");html_detail::Number(file,"Step source line",step.location->line);html_detail::Number(file,"Step source column",step.location->column);file.Put("</dl><div>Location origin: ");file.Put(step.location->explicit_location?"author-declared":"runtime callsite");file.Put("</div>");}html_detail::Field(file,"Error",step.error_json);html_detail::Field(file,"Raw receipt error",step.receipt_error_json);if(step.callback_received){file.Put("<dl>");html_detail::Number(file,"Actual raw callback receipt (monotonic ms)",*step.callback_received);file.Put("</dl>");}if(step.attachment_error_json)html_detail::Field(file,"Actual attachment error",*step.attachment_error_json);if(step.declared_skip_request){file.Put("<dl>");html_detail::Number(file,"Actual skipped declaration request",step.declared_skip_request);file.Put("</dl>");}if(step.test_skip){file.Put("<dl>");html_detail::Number(file,"Accepted native test-skip request",step.test_skip->request);html_detail::Number(file,"Raw receipt test-skip request",step.receipt_test_skip);file.Put("</dl>");html_detail::Field(file,"Accepted test-skip reason",step.test_skip->reason_json);}if(step.step_skip){file.Put("<dl>");html_detail::Number(file,"Accepted native local-step-skip request",step.step_skip->request);html_detail::Number(file,"Actual skip source step",step.step_skip->source_step);html_detail::Number(file,"Raw receipt local-step-skip request",step.receipt_step_skip);file.Put("</dl>");if(step.step_skip->description_present)html_detail::Field(file,"Actual local-step skip reason",step.step_skip->reason_json);}file.Put("</details>");}
  if(!report.soft_assertions.empty())file.Put("<h2>Actual soft assertion journal</h2>");
  for(const auto& r:report.soft_assertions){file.Put("<details class=\"native-soft-assertion\"><summary>");file.Put(SoftReceiptName(r.receipt));file.Put("</summary><dl>");html_detail::Number(file,"Native soft assertion ID",r.id);html_detail::Number(file,"Actual request",r.request);html_detail::Number(file,"Test",r.owner.test_id);html_detail::Number(file,"Attempt generation",r.owner.attempt_generation);html_detail::Number(file,"Worker",r.owner.worker_index);html_detail::Number(file,"PID",r.owner.pid);html_detail::Number(file,"Step",r.step);html_detail::Number(file,"Native admission (monotonic ms)",r.admitted);html_detail::Number(file,"Actual matcher receipt (monotonic ms)",r.received);file.Put("</dl><p>Raw outcome: ");file.Put(SoftRawName(r.raw));file.Put("; callback: ");file.Put(r.owner.callback_kind);file.Put(r.cleanup?"; cleanup failure</p>":"</p>");html_detail::Field(file,"Original matcher error and stack",r.error_json);if(r.cancel_reason_json!="\"\"")html_detail::Field(file,"Native cancellation cause",r.cancel_reason_json);if(r.location){html_detail::Field(file,"Actual assertion caller",r.location->file_json);file.Put("<dl>");html_detail::Number(file,"Line",r.location->line);html_detail::Number(file,"Column",r.location->column);file.Put("</dl>");}file.Put("</details>");}
  if(!report.attachment_declarations.empty())file.Put("<h2>Attachment declarations</h2>");
  for(const auto& declaration:report.attachment_declarations){file.Put("<details class=\"native-attachment-declaration\"><summary>");file.Put(XML(declaration.name_json).text);file.Put(" · ");file.Put(AttachmentDeclarationReceipt(declaration.receipt));file.Put("</summary><dl>");html_detail::Number(file,"Declaration ID",declaration.id);html_detail::Number(file,"Actual request",declaration.request);html_detail::Number(file,"Associated test",declaration.owner.test_id);html_detail::Number(file,"Attempt generation",declaration.owner.attempt_generation);html_detail::Number(file,"Worker",declaration.owner.worker_index);html_detail::Number(file,"PID",declaration.owner.pid);if(declaration.expected_bytes)html_detail::Number(file,"Declared byte length",*declaration.expected_bytes);if(declaration.receipt==AttachmentDeclarations::Receipt::Committed)html_detail::Number(file,"Actual committed attachment ID",declaration.artifact_id);file.Put("</dl>");html_detail::Field(file,"Content type",declaration.type_json);if(declaration.author_path_json){html_detail::Field(file,"Author-declared path reference",*declaration.author_path_json);file.Put(declaration.source==AttachmentDeclarations::Source::Body?"<p>Author path is metadata only; actual body-file receipts are separate. No file existence, bytes or native ownership receipt is claimed for this path.</p>":declaration.receipt==AttachmentDeclarations::Receipt::ReferenceAccepted?"<p>Declaration accepted; no file existence, bytes or native ownership receipt is claimed.</p>":"<p>Author path reference was rejected; no file existence, bytes or native ownership receipt is claimed.</p>");}if(declaration.source==AttachmentDeclarations::Source::Absent)file.Put("<p>Author metadata declaration has no body or path.</p>");if(declaration.receipt==AttachmentDeclarations::Receipt::Failed||declaration.receipt==AttachmentDeclarations::Receipt::Rejected)html_detail::Field(file,"Native append error",declaration.error_json);file.Put("</details>");}
  if(!report.attachment_views.empty())file.Put("<h2>Accepted native attachment views</h2><p>Final accepted declaration order and sparse slots. Withdrawing a slot does not delete immutable files or error history below.</p>");
  for(const auto& view:report.attachment_views){
    file.Put("<details class=\"native-attachment-view\"><summary>Accepted attachment view — attempt ");file.NumberValue(view.owner.attempt_generation);file.Put("</summary><dl>");html_detail::Number(file,"Test",view.owner.test_id);html_detail::Number(file,"Worker",view.owner.worker_index);html_detail::Number(file,"PID",view.owner.pid);html_detail::Number(file,"Native revision",view.changes.size());file.Put("</dl><ol>");
    if(!view.changes.empty())for(const auto& slot:view.changes.back().slots){file.Put("<li>");if(!slot)file.Put("Hole (no attachment)");
      else if(slot->kind==AttachmentMutations::Kind::Artifact){const auto found=std::find_if(report.attachments.begin(),report.attachments.end(),[&](const auto& artifact){return artifact.id==slot->id;});html_detail::Field(file,"Committed attachment",found->name_json);html_detail::Field(file,"Actual native file",AttachmentPathJSON(found->path));}
      else{const auto found=std::find_if(report.attachment_declarations.begin(),report.attachment_declarations.end(),[&](const auto& declaration){return declaration.id==slot->id;});html_detail::Field(file,"Admitted declaration",found->name_json);if(found->author_path_json)html_detail::Field(file,"Authored path reference",*found->author_path_json);if(found->receipt==AttachmentDeclarations::Receipt::Committed){const auto artifact=std::find_if(report.attachments.begin(),report.attachments.end(),[&](const auto& value){return value.id==found->artifact_id;});html_detail::Field(file,"Actual native file",AttachmentPathJSON(artifact->path));}file.Put(AttachmentDeclarationReceipt(found->receipt));}
      file.Put("</li>");
    }file.Put("</ol></details>");
  }
  file.Put("<h2>Immutable native attachment history</h2><p>Every genuinely committed artifact remains here independently of the final accepted view.</p>");
  for(const auto& a:report.attachments){
    file.Put("<details class=\"native-attachment\" data-attachment-id=\"");file.NumberValue(a.id);if(a.step_id){file.Put("\" data-step-id=\"");file.NumberValue(a.step_id);}file.Put("\"><summary>");file.Put(XML(a.name_json).text);file.Put("</summary>");html_detail::Field(file,"Name",a.name_json);html_detail::Field(file,"Content type",a.content_type_json);html_detail::Field(file,"Actual native file",AttachmentPathJSON(a.path));file.Put("<dl>");html_detail::Number(file,"Associated test",a.owner.test_id);if(a.step_id)html_detail::Number(file,"Actual owning step",a.step_id);html_detail::Number(file,"Attempt generation",a.owner.attempt_generation);html_detail::Number(file,"Worker",a.owner.worker_index);html_detail::Number(file,"PID",a.owner.pid);html_detail::Number(file,"Native writer PID",a.writer_pid);html_detail::Number(file,"Actual bytes",a.bytes);html_detail::Number(file,"Duration (ms)",a.received-a.started);file.Put("</dl><p>Native writer exit confirmed after actual file commit.</p><a download href=\"");
    static constexpr char digits[]="0123456789ABCDEF";std::string url;for(const unsigned char c:a.path){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='.'||c=='_'||c=='-'||c=='~')url+=static_cast<char>(c);else{url+='%';url+=digits[c>>4];url+=digits[c&15];}}html_detail::Text(file,url);file.Put("\">Open actual attachment</a></details>");
  }
  file.Put("<h2>Global errors</h2>");
  for (const auto& error : report.global_errors_json) html_detail::Field(file, "Runner error", error);
  file.Put("<h2>Workers</h2><p>Streams belong to the actual worker process. Expand to inspect exact bytes in hexadecimal.</p>");
  for (const auto& worker : report.workers) {
    file.Put("<details class=\"worker\" data-worker-index=\""); file.NumberValue(worker.worker_index);
    file.Put("\"><summary>Worker "); file.NumberValue(worker.worker_index);
    file.Put(" · PID "); file.NumberValue(worker.pid); file.Put("</summary><dl><dt>Exit status</dt><dd>");
    if (worker.exit_code < 0) file.Put("Unknown");
    else { file.Put(worker.signaled ? "Signal " : "Code "); file.NumberValue(worker.exit_code); }
    file.Put("</dd></dl>");
    for (const auto& [name, bytes] : {std::pair<std::string_view, const std::string&>{"stdout", worker.stdout_bytes}, {"stderr", worker.stderr_bytes}}) {
      file.Put("<details class=\"stream\" data-stream=\""); file.Put(name);
      file.Put("\"><summary>"); file.Put(name); file.Put(" · "); file.NumberValue(bytes.size());
      file.Put(" bytes · hex</summary><pre>"); file.Hex(bytes); file.Put("</pre></details>");
    }
    file.Put("</details>");
  }
  file.Put("<h2>Native attempt output directories</h2><p>Each outputDir was assigned after real directory creation, writer exit and host readiness ACK. Child paths do not imply child files exist.</p>");
  if(report.output_preservation){file.Put("<h2>Native output retention</h2><p>Original output creation and assignment remain immutable history. These separate native decisions concern only assigned output trees, not committed attachment directories. Kept means preserved by policy without a new availability observation.</p>");for(const auto& row:report.output_retentions){file.Put("<details class=\"native-output-retention\"><summary>Output ");file.NumberValue(row.output_id);file.Put(" · ");file.Put(OutputRetentionStatusName(row.status));file.Put("</summary><p>");file.Put(OutputRetentionAvailability(row.status));file.Put("</p>");html_detail::Field(file,"Original assigned path",AttachmentPathJSON(row.path));html_detail::Field(file,"Native cleanup error",AttachmentPathJSON(row.error));file.Put("</details>");}}
  for(const auto& output:report.outputs){file.Put("<details class=\"native-output\"><summary>Output ");file.NumberValue(output.id);file.Put(" · ");file.Put(OutputStatusName(output.status));file.Put("</summary><dl>");html_detail::Number(file,"Test ID",output.owner.test_id);html_detail::Number(file,"Generation",output.owner.generation);html_detail::Number(file,"Worker",output.owner.worker_index);html_detail::Number(file,"PID",output.owner.pid);html_detail::Number(file,"Writer PID",output.writer_pid);html_detail::Number(file,"Writer exit code",output.writer_exit);if(output.inode)html_detail::Number(file,"Actual inode",output.inode);if(output.assigned)html_detail::Number(file,"Assignment ACK receipt (monotonic ms)",*output.assigned);html_detail::Number(file,"Physical receipt (monotonic ms)",*output.received);file.Put("</dl>");html_detail::Field(file,output.assigned?"Assigned outputDir (historical receipt)":"Proposed path (unassigned)",AttachmentPathJSON(output.path));html_detail::Field(file,"Error",AttachmentPathJSON(output.error));html_detail::Field(file,"Cleanup error",AttachmentPathJSON(output.cleanup_error));file.Put("</details>");}
  file.Put("</body></html>\n"); file.Commit();
}
} // namespace suitecut::runner
