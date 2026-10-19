#ifndef SUITECUT_HTTP_RETRY_POLICY_H_
#define SUITECUT_HTTP_RETRY_POLICY_H_
#include <curl/curl.h>
#include <cerrno>
namespace suitecut {
enum class NativeHTTPRetryReason { None, ConnectionReset, EmptyReply, UnexpectedEOF };
inline NativeHTTPRetryReason NativeHTTPRetry(CURLcode result,long os_errno,int status) {
  if(result==CURLE_RECV_ERROR&&os_errno==ECONNRESET)return NativeHTTPRetryReason::ConnectionReset;
  if(result==CURLE_GOT_NOTHING)return NativeHTTPRetryReason::EmptyReply;
  // A received final HTTP response ended before its advertised transfer framing.
  // This is a replay policy, not a reinterpretation of CURLcode or OS errno.
  if(result==CURLE_PARTIAL_FILE&&status>=200&&status<=599)return NativeHTTPRetryReason::UnexpectedEOF;
  return NativeHTTPRetryReason::None;
}
}
#endif
