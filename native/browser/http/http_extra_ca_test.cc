// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http/http_extra_ca.h"
#include <cassert>
#include <iostream>
int main(){
 using namespace suitecut;
 std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>key(EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","P-256"),EVP_PKEY_free);
 std::unique_ptr<X509,decltype(&X509_free)>certificate(X509_new(),X509_free);assert(key&&certificate);
 assert(X509_set_version(certificate.get(),2)==1&&ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()),1)==1);
 assert(X509_gmtime_adj(X509_getm_notBefore(certificate.get()),-3600)&&X509_gmtime_adj(X509_getm_notAfter(certificate.get()),86400));
 assert(X509_set_pubkey(certificate.get(),key.get())==1);
 // OpenSSL 4 returns the certificate's own name read-only; build it separately.
 std::unique_ptr<X509_NAME,decltype(&X509_NAME_free)>name(X509_NAME_new(),X509_NAME_free);assert(name&&X509_NAME_add_entry_by_txt(name.get(),"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("SuiteCut native CA component"),-1,-1,0)==1);
 assert(X509_set_subject_name(certificate.get(),name.get())==1&&X509_set_issuer_name(certificate.get(),name.get())==1&&X509_sign(certificate.get(),key.get(),EVP_sha256())>0);
 std::unique_ptr<BIO,decltype(&BIO_free)>bio(BIO_new(BIO_s_mem()),BIO_free);assert(bio&&PEM_write_bio_X509(bio.get(),certificate.get())==1);
 const char*bytes=nullptr;const auto size=BIO_get_mem_data(bio.get(),&bytes);assert(size>0);const std::string pem(bytes,static_cast<size_t>(size));
 std::string error;auto anchors=NativeHTTPExtraCA::Parse({pem,pem},error);assert(anchors&&anchors->size()==2);
 std::unique_ptr<SSL_CTX,decltype(&SSL_CTX_free)>ctx(SSL_CTX_new(TLS_client_method()),SSL_CTX_free);assert(ctx);
 assert(SSL_CTX_set_default_verify_paths(ctx.get())==1);auto*store=SSL_CTX_get_cert_store(ctx.get());
 const auto count=[store]{
#if OPENSSL_VERSION_NUMBER>=0x30300000L
  auto*objects=X509_STORE_get1_objects(store);const int size=sk_X509_OBJECT_num(objects);sk_X509_OBJECT_pop_free(objects,X509_OBJECT_free);return size;
#else
  return sk_X509_OBJECT_num(X509_STORE_get0_objects(store));
#endif
 };
 const int prior=count();
 assert(anchors->Install(ctx.get())&&count()==prior+1&&anchors->Install(ctx.get()));
 assert(!anchors->Install(nullptr));
 for(const auto&bad:{std::string(),std::string(" \r\n\t"),pem+"junk",pem+std::string(1,'\0'),std::string(32769,'x'),std::string("-----BEGIN CERTIFICATE-----\ninvalid\n-----END CERTIFICATE-----")})assert(!NativeHTTPExtraCA::Parse({bad},error));
 // No certificate count or input byte cap: 40 certificates and >32 KiB bundles parse.
 assert(NativeHTTPExtraCA::Parse(std::vector<std::string>(40,pem),error)->size()==40);
 std::string bundle;size_t copies=0;while(bundle.size()<=32*1024){bundle+=pem;++copies;}assert(NativeHTTPExtraCA::Parse({bundle},error)->size()==copies);
 assert(NativeHTTPExtraCA::Parse({},error)->size()==0);
 std::cout<<"PASS immutable native extra CA: real PEM decoding, existing default store, exact duplicate anchors, invalid/NUL/trailing/empty input, no count/byte cap\n";
}
