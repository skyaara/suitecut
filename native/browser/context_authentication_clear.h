// SPDX-License-Identifier: MIT
#pragma once
#include "locator_handler_async_work.h"
#include "context_authentication_controls.h"
#include "include/cef_callback.h"
#include "include/cef_request_context.h"
namespace suitecut {
// This callback retains its actual request context until CEF reports completion.
class NativeContextAuthenticationClear final:public CefCompletionCallback {
 public:
  NativeContextAuthenticationClear(CefRefPtr<CefRequestContext>context,NativeContextAuthenticationControls::Result done):context_(std::move(context)),done_(BindNativeLocatorHandlerAsync(std::move(done))){}
  void OnComplete()override{context_=nullptr;auto done=std::move(done_);if(done)done(true,"");}
 private:
  NativeLocatorHandlerAsyncWork handler_receipt_;
  CefRefPtr<CefRequestContext>context_;NativeContextAuthenticationControls::Result done_;
  IMPLEMENT_REFCOUNTING(NativeContextAuthenticationClear);
};
}
