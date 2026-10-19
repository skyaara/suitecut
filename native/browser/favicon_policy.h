// SPDX-License-Identifier: MIT
#pragma once

#include "include/cef_resource_request_handler.h"

namespace suitecut {

// Headless captures have no browser chrome consuming tab icons. Chromium's
// genuinely classified browser-icon requests are canceled before resource load;
// authored XHR, fetch, and image requests retain their ordinary request types.
class HeadlessBrowserIconRequest final : public CefResourceRequestHandler {
 public:
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>,
                                  CefRefPtr<CefFrame>,
                                  CefRefPtr<CefRequest> request,
                                  CefRefPtr<CefCallback>) override {
    return request && request->GetResourceType() == RT_FAVICON ? RV_CANCEL
                                                              : RV_CONTINUE;
  }

 private:
  IMPLEMENT_REFCOUNTING(HeadlessBrowserIconRequest);
};

inline CefRefPtr<CefResourceRequestHandler> HeadlessBrowserIconHandler(
    bool headless, CefRefPtr<CefRequest> request) {
  if (!headless || !request || request->GetResourceType() != RT_FAVICON)
    return nullptr;
  // One stateless handler is shared across IO-thread requests. There is no
  // retained per-request identity, callback, deadline, or resource body.
  static CefRefPtr<HeadlessBrowserIconRequest> handler =
      new HeadlessBrowserIconRequest();
  return handler;
}

}  // namespace suitecut
