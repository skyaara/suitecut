// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "network_fatal_teardown.h"
int main(){std::vector<std::function<void()>>posted;int reports=0,closes=0;bool reject=true;suitecut::NativeNetworkFatalTeardown fatal([&](std::function<void()> task){if(reject)return false;posted.push_back(std::move(task));return true;},[&](const std::string&actual){assert(actual=="actual paused Fetch failure");++reports;},[&]{++closes;});fatal.Fail("actual paused Fetch failure");assert(fatal.pending()&&!fatal.requested()&&reports==0&&closes==0);reject=false;fatal.Tick();fatal.Fail("ignored duplicate");assert(posted.size()==1);posted[0]();assert(fatal.pending()&&fatal.requested()&&reports==1&&closes==1);posted[0]();fatal.Tick();assert(reports==1&&closes==1);fatal.Closed();assert(!fatal.pending());posted[0]();assert(closes==1);
 std::function<void()>late;int unexpected=0;{suitecut::NativeNetworkFatalTeardown closed([&](std::function<void()>task){late=std::move(task);return true;},[&](const std::string&){++unexpected;},[&]{++unexpected;});closed.Fail("actual closed owner");closed.Closed();}late();assert(unexpected==0);
}
