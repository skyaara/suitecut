// SPDX-License-Identifier: MIT
// Included inside Runner. All execution/retirement/report validation remains
// before this final output step. A reporter never calls authored callbacks.
sr::NativeReportPublication* TerminalPublication(sr::NativeReportPublication& publication)const{
  return options_.terminal_reporter==sr::NativeTerminalReporter::JSON&&!options_.directory_admission?&publication:nullptr;
}
void EmitTerminalReport(const sr::NativeReportPublication& publication,int actual_exit){
  if(options_.terminal_reporter!=sr::NativeTerminalReporter::JSON||options_.directory_admission)return;
  try{sr::EmitNativeJSONReporter(publication,[this]{return StopRequested();});}
  catch(const std::exception& error){throw std::runtime_error("Native JSON reporter failed after actual execution exit "+std::to_string(actual_exit)+": "+error.what());}
}
