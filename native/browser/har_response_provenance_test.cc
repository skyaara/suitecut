// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_response_details.h"
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));
 auto raw=CefDictionaryValue::Create();
 auto missing=suitecut::NativeResponseWireInfo(raw,"r","session","frame","GET");
 assert(missing->GetDictionary("headers")&&!missing->GetBool("_nativeHeadersObserved"));
 raw->SetDictionary("headers",CefDictionaryValue::Create());
 auto empty=suitecut::NativeResponseWireInfo(raw,"r","session","frame","GET");
 assert(empty->GetDictionary("headers")&&empty->GetBool("_nativeHeadersObserved"));
 raw->GetDictionary("headers")->SetString("Content-Encoding","gzip");
 auto coded=suitecut::NativeResponseWireInfo(raw,"r","session","frame","GET");
 assert(coded->GetBool("_nativeHeadersObserved"));assert(coded->GetDictionary("headers")->GetString("Content-Encoding")=="gzip");
 raw->GetDictionary("headers")->SetString("Content-Encoding","identity");
 assert(coded->GetDictionary("headers")->GetString("Content-Encoding")=="gzip");
}
