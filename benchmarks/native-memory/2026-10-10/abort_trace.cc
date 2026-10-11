#include <csignal>
#include <execinfo.h>
#include <unistd.h>
static void Trace(int) {void* frames[64];int count=backtrace(frames,64);backtrace_symbols_fd(frames,count,2);_exit(134);}
__attribute__((constructor)) static void Install() {signal(SIGABRT,Trace);}
