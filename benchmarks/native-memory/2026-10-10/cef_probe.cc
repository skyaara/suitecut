#include "include/capi/cef_app_capi.h"
#include "include/cef_api_hash.h"
#include <cstdio>
int main(int argc,char** argv) {
 cef_main_args_t args{argc,argv};
 cef_api_hash(CEF_API_VERSION,0);
 const int code=cef_execute_process(&args,nullptr,nullptr);if(code>=0)return code;
 cef_settings_t settings{};settings.size=sizeof(settings);settings.windowless_rendering_enabled=1;
 const char16_t path[]=u"/workspace/suitecut/.suitecut/memory-cef-probe-profile";
 cef_string_utf16_set(path,sizeof(path)/2-1,&settings.root_cache_path,1);
 std::fprintf(stderr,"C API probe: before initialize\n");
 const int initialized=cef_initialize(&args,&settings,nullptr,nullptr);
 std::fprintf(stderr,"C API probe: initialize=%d\n",initialized);
 if(initialized)cef_shutdown();
 cef_string_utf16_clear(&settings.root_cache_path);return initialized?0:1;
}
