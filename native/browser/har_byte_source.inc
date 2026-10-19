// SPDX-License-Identifier: MIT
// Source-local observation coverage, not a new protocol command or waiter.
int native_har_primary_enable_ = 0;
void NativeHARPrimaryObservation(bool ready) {
  engine_.network_archive().PrimaryObservation(network_context_id_,"page:"+std::to_string(page_id_),ready);
}
void NativeHARPrimaryReset() {
  native_har_primary_enable_=0;NativeHARPrimaryObservation(false);
}
int NativeHARPrimaryEnable(Dict params) {
  NativeHARPrimaryReset();
  const int id=AllocateDevToolsId();native_har_primary_enable_=id;
  if(!SubmitDevTools(id,"Network.enable",params,"")){native_har_primary_enable_=0;return 0;}
  return id;
}
void NativeHARPrimaryReceipt(int id,bool success,const std::string&session) {
  if(!session.empty()||!native_har_primary_enable_||id!=native_har_primary_enable_)return;
  native_har_primary_enable_=0;NativeHARPrimaryObservation(success&&!closing_page_);
}
void NativeHARResponseBytes(const std::string&method,Dict data,const std::string&session) {
  if(!session.empty()||closing_page_)return;
  if(method!="Network.responseReceived"&&method!="Network.dataReceived"&&method!="Network.loadingFinished"&&method!="Network.loadingFailed"&&method!="Network.requestServedFromCache")return;
  engine_.network_archive().ResponseBytes(network_context_id_,"page:"+std::to_string(page_id_),method,data,session);
}
