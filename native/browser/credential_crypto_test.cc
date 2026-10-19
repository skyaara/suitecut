// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "credential_crypto.h"
#include <cassert>
#include <iostream>
#include <set>
#include <openssl/evp.h>
#include <openssl/x509.h>
int main() {
  suitecut::VirtualCredential first,second; std::string error,public_key;
  assert(suitecut::GenerateVirtualCredential("localhost",17,first,error));
  assert(first.sign_count == 17 && first.rp_id == "localhost");
  assert(suitecut::ValidateVirtualCredential(first,error));
  assert(suitecut::VirtualCredentialPublicKey(first.private_key,public_key,error) && public_key == first.public_key);
  assert(suitecut::GenerateVirtualCredential("localhost",0,second,error));
  auto mismatch = first; mismatch.public_key = second.public_key;
  assert(!suitecut::ValidateVirtualCredential(mismatch,error));
  auto malformed = first; malformed.private_key += "A";
  assert(!suitecut::ValidateVirtualCredential(malformed,error));
  malformed = first; malformed.id = "A"; assert(!suitecut::ValidateVirtualCredential(malformed,error));
  malformed = first; malformed.user_handle = suitecut::CredentialBase64URL(reinterpret_cast<const unsigned char*>(std::string(65,'x').data()),65);
  assert(!suitecut::ValidateVirtualCredential(malformed,error));
  assert(!suitecut::GenerateVirtualCredential("https://localhost",0,malformed,error));
  std::vector<unsigned char> data,decoded;
  for (size_t length = 1; length < 1025; ++length) {
    data.push_back(static_cast<unsigned char>(length&255));const auto encoded = suitecut::CredentialBase64URL(data.data(),data.size());
    assert(suitecut::CredentialDecodeBase64URL(encoded,1024,decoded) && decoded == data);
    assert(!suitecut::CredentialDecodeBase64URL(encoded, length-1,decoded));
  }
  assert(!suitecut::CredentialDecodeBase64URL("Zh",1,decoded));
  assert(!suitecut::CredentialDecodeBase64URL("Zg=",1,decoded));
  std::set<std::string> ids,handles;
  for (int index = 0; index < 1000; ++index) {
    suitecut::VirtualCredential credential; assert(suitecut::GenerateVirtualCredential("native.example",static_cast<uint32_t>(index),credential,error));
    assert(ids.insert(credential.id).second && handles.insert(credential.user_handle).second);
    assert(suitecut::ValidateVirtualCredential(credential,error));
  }
  assert(suitecut::CredentialDecodeBase64URL(first.private_key,16384,decoded));const unsigned char* cursor = decoded.data();
  PKCS8_PRIV_KEY_INFO* info = d2i_PKCS8_PRIV_KEY_INFO(nullptr,&cursor,static_cast<long>(decoded.size()));assert(info);
  EVP_PKEY* private_key = EVP_PKCS82PKEY(info); assert(private_key);PKCS8_PRIV_KEY_INFO_free(info);
  assert(suitecut::CredentialDecodeBase64URL(first.public_key,16384,data));cursor=data.data();EVP_PKEY* verifying = d2i_PUBKEY(nullptr,&cursor,static_cast<long>(data.size()));assert(verifying);
  EVP_MD_CTX* signing = EVP_MD_CTX_new();assert(signing);assert(EVP_DigestSignInit(signing,nullptr,EVP_sha256(),nullptr,private_key)==1);
  const unsigned char message[] = "native WebAuthn authentication data and hashed client data";
  size_t size=0;assert(EVP_DigestSign(signing,nullptr,&size,message,sizeof(message))==1);std::vector<unsigned char> signature(size);
  assert(EVP_DigestSign(signing,signature.data(),&size,message,sizeof(message))==1);signature.resize(size);EVP_MD_CTX_free(signing);
  EVP_MD_CTX* verifier=EVP_MD_CTX_new();assert(verifier);assert(EVP_DigestVerifyInit(verifier,nullptr,EVP_sha256(),nullptr,verifying)==1);
  assert(EVP_DigestVerify(verifier,signature.data(),signature.size(),message,sizeof(message))==1);
  signature.back()^=1;assert(EVP_DigestVerify(verifier,signature.data(),signature.size(),message,sizeof(message))!=1);
  EVP_MD_CTX_free(verifier);EVP_PKEY_free(private_key);EVP_PKEY_free(verifying);
  std::cout << "native credential crypto: 1000 real random P-256 PKCS8/SPKI pairs, strict import/mismatch/bounds, signature verification passed\n";
}
