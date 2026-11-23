// SPDX-License-Identifier: MIT
#pragma once
// Container growth inside noexcept ledgers. With exceptions enabled a failed
// allocation is caught and poisons the ledger; translation units built with
// -fno-exceptions (CEF's default) abort on allocation failure anyway.
#if defined(__cpp_exceptions)
#define SUITECUT_GROWTH_TRY try
#define SUITECUT_GROWTH_CATCH catch(...)
#else
#define SUITECUT_GROWTH_TRY if(true)
#define SUITECUT_GROWTH_CATCH else
#endif
