// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http_retry_policy.h"
#include <cassert>
#include <iostream>
int main(){
 using suitecut::NativeHTTPRetry;using R=suitecut::NativeHTTPRetryReason;
 assert(NativeHTTPRetry(CURLE_RECV_ERROR,ECONNRESET,0)==R::ConnectionReset);
 assert(NativeHTTPRetry(CURLE_GOT_NOTHING,0,0)==R::EmptyReply);
 assert(NativeHTTPRetry(CURLE_PARTIAL_FILE,0,200)==R::UnexpectedEOF);
 assert(NativeHTTPRetry(CURLE_PARTIAL_FILE,0,599)==R::UnexpectedEOF);
 // No final HTTP response, native decoding rejection, parser cancellation,
 // orderly complete response and unrelated OS failures are not EOF replay.
 assert(NativeHTTPRetry(CURLE_PARTIAL_FILE,0,0)==R::None);
 assert(NativeHTTPRetry(CURLE_PARTIAL_FILE,0,100)==R::None);
 assert(NativeHTTPRetry(CURLE_PARTIAL_FILE,0,600)==R::None);
 assert(NativeHTTPRetry(CURLE_WRITE_ERROR,0,200)==R::None);
 assert(NativeHTTPRetry(CURLE_ABORTED_BY_CALLBACK,0,200)==R::None);
 assert(NativeHTTPRetry(CURLE_OK,0,200)==R::None);
 assert(NativeHTTPRetry(CURLE_RECV_ERROR,EIO,200)==R::None);
 assert(NativeHTTPRetry(CURLE_OPERATION_TIMEDOUT,0,200)==R::None);
 std::cout<<"PASS native retry reasons: actual reset, empty reply and explicit final-response EOF; decoding/parser/deadline/absent-response exclusions\n";
}
