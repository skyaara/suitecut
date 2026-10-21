// SPDX-License-Identifier: MIT
#include "http_connection_metadata.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
  assert(suitecut::HTTPOpenSSLBackendCompatible("OpenSSL/"+std::to_string(OPENSSL_VERSION_MAJOR)+".0.0"));
  assert(!suitecut::HTTPOpenSSLBackendCompatible("OpenSSL/"+std::to_string(OPENSSL_VERSION_MAJOR+1)+".0.0"));
  assert(!suitecut::HTTPOpenSSLBackendCompatible("SecureTransport"));
  assert(!suitecut::HTTPOpenSSLBackendCompatible("LibreSSL/3.3.6"));
  assert(suitecut::HTTPCommonName(nullptr).empty());
  auto name=X509_NAME_new();assert(name);assert(suitecut::HTTPCommonName(name).empty());
  assert(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("localhost"),-1,-1,0)==1);
  assert(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("secondary-name"),-1,-1,-1)==1);
  assert(suitecut::HTTPCommonName(name)=="localhost");
  unsigned char*encoded=nullptr;const int length=i2d_X509_NAME(name,&encoded);assert(length>0);
  const unsigned char*cursor=encoded;auto decoded=d2i_X509_NAME(nullptr,&cursor,length);assert(decoded);assert(suitecut::HTTPCommonName(decoded)=="localhost");
  X509_NAME_free(decoded);OPENSSL_free(encoded);X509_NAME_free(name);
  name=X509_NAME_new();assert(name);assert(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_UTF8,reinterpret_cast<const unsigned char*>("native-é.test"),-1,-1,0)==1);
  assert(suitecut::HTTPCommonName(name)=="native-é.test");X509_NAME_free(name);
  std::cout<<"Actual OpenSSL certificate common names: first repeated RDN value survives DER roundtrip, UTF-8 and absent name handling passed\n";
}
