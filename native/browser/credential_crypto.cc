// SPDX-License-Identifier: MIT
#include "credential_crypto.h"
#include <memory>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
namespace suitecut {
namespace {
using Key = std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>;
using KeyContext = std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)>;
using PKCS8 = std::unique_ptr<PKCS8_PRIV_KEY_INFO,decltype(&PKCS8_PRIV_KEY_INFO_free)>;
struct Secret {
  std::vector<unsigned char> bytes;
  ~Secret() { if (!bytes.empty()) OPENSSL_cleanse(bytes.data(),bytes.size()); }
};
bool RelyingParty(const std::string& input) {
  if (input.empty() || input.size() > 1024) return false;
  for (const unsigned char byte : input) if (byte <= 32 || byte >= 127 || byte == '/' || byte == ':' || byte == '\\' || byte == '@') return false;
  return true;
}
bool Supported(EVP_PKEY* key) {
  if (!key || !EVP_PKEY_is_a(key,"EC")) return false;
  char group[80] = {}; size_t length = 0;
  return EVP_PKEY_get_utf8_string_param(key,OSSL_PKEY_PARAM_GROUP_NAME,group,sizeof(group),&length) && (std::string(group) == "prime256v1" || std::string(group) == "P-256");
}
Key Private(const std::string& encoded) {
  Secret data;
  if (!CredentialDecodeBase64URL(encoded,16384,data.bytes)) return Key(nullptr,EVP_PKEY_free);
  const unsigned char* cursor = data.bytes.data();
  PKCS8 info(d2i_PKCS8_PRIV_KEY_INFO(nullptr,&cursor,static_cast<long>(data.bytes.size())),PKCS8_PRIV_KEY_INFO_free);
  if (!info || cursor != data.bytes.data()+data.bytes.size()) return Key(nullptr,EVP_PKEY_free);
  Key key(EVP_PKCS82PKEY(info.get()),EVP_PKEY_free);
  if (!Supported(key.get())) return Key(nullptr,EVP_PKEY_free);
  return key;
}
bool Public(EVP_PKEY* key,std::string& output) {
  const int length = i2d_PUBKEY(key,nullptr); if (length <= 0 || length > 16384) return false;
  std::vector<unsigned char> data(static_cast<size_t>(length)); unsigned char* cursor = data.data();
  if (i2d_PUBKEY(key,&cursor) != length) return false;
  output = CredentialBase64URL(data.data(),data.size()); return true;
}
}
std::string CredentialBase64URL(const unsigned char* data,size_t length) {
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string output; output.reserve((length*4+2)/3); uint32_t bits = 0; unsigned count = 0;
  for (size_t index = 0; index < length; ++index) { bits = (bits<<8)|data[index]; count += 8; while (count >= 6) { count -= 6; output += alphabet[(bits>>count)&63]; } }
  if (count) output += alphabet[(bits<<(6-count))&63];
  return output;
}
bool CredentialDecodeBase64URL(const std::string& text,size_t maximum,std::vector<unsigned char>& output) {
  output.clear(); if (text.empty() || text.size() > (maximum*4+2)/3 || text.size()%4 == 1) return false;
  output.reserve(text.size()*3/4); uint32_t bits = 0; unsigned count = 0;
  for (const unsigned char byte : text) {
    int value = byte >= 'A' && byte <= 'Z' ? byte-'A' : byte >= 'a' && byte <= 'z' ? byte-'a'+26 : byte >= '0' && byte <= '9' ? byte-'0'+52 : byte == '-' ? 62 : byte == '_' ? 63 : -1;
    if (value < 0) { output.clear(); return false; }
    bits = (bits<<6)|static_cast<unsigned>(value); count += 6;
    if (count >= 8) { count -= 8; output.push_back(static_cast<unsigned char>((bits>>count)&255)); if (output.size() > maximum) { output.clear(); return false; } }
  }
  if ((count && (bits&((1u<<count)-1))) || output.empty()) { output.clear(); return false; }
  return true;
}
bool GenerateVirtualCredential(const std::string& rp_id,uint32_t sign_count,VirtualCredential& output,std::string& error) {
  if (!RelyingParty(rp_id)) { error = "Invalid WebAuthn relying party id"; return false; }
  KeyContext context(EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr),EVP_PKEY_CTX_free); EVP_PKEY* generated = nullptr;
  if (!context || EVP_PKEY_keygen_init(context.get()) <= 0 || EVP_PKEY_CTX_set_group_name(context.get(),"prime256v1") <= 0 || EVP_PKEY_generate(context.get(),&generated) <= 0) { error = "Native WebAuthn ECDSA key generation failed"; return false; }
  Key key(generated,EVP_PKEY_free); PKCS8 info(EVP_PKEY2PKCS8(key.get()),PKCS8_PRIV_KEY_INFO_free);
  const int length = info ? i2d_PKCS8_PRIV_KEY_INFO(info.get(),nullptr) : 0;
  if (length <= 0 || length > 16384) { error = "Native WebAuthn private key encoding failed"; return false; }
  Secret secret; secret.bytes.resize(static_cast<size_t>(length)); unsigned char* cursor = secret.bytes.data();
  if (i2d_PKCS8_PRIV_KEY_INFO(info.get(),&cursor) != length) { error = "Native WebAuthn private key encoding failed"; return false; }
  unsigned char random[32] = {};
  if (RAND_bytes(random,sizeof(random)) != 1) { error = "Native WebAuthn random identity generation failed"; return false; }
  VirtualCredential credential; credential.rp_id = rp_id; credential.sign_count = sign_count;
  credential.id = CredentialBase64URL(random,16); credential.user_handle = CredentialBase64URL(random+16,16); OPENSSL_cleanse(random,sizeof(random));
  credential.private_key = CredentialBase64URL(secret.bytes.data(),secret.bytes.size());
  if (!Public(key.get(),credential.public_key)) { error = "Native WebAuthn public key encoding failed"; return false; }
  output = std::move(credential); return true;
}
bool ValidateVirtualCredential(const VirtualCredential& input,std::string& error) {
  if (!RelyingParty(input.rp_id)) { error = "Invalid WebAuthn relying party id"; return false; }
  std::vector<unsigned char> decoded;
  if (!CredentialDecodeBase64URL(input.id,1024,decoded) || !CredentialDecodeBase64URL(input.user_handle,64,decoded)) { error = "Invalid WebAuthn credential id or user handle"; return false; }
  const auto key = Private(input.private_key);
  if (!key || !CredentialDecodeBase64URL(input.public_key,16384,decoded)) { error = "Invalid WebAuthn ECDSA P-256 PKCS8/SPKI key encoding"; return false; }
  const unsigned char* cursor = decoded.data(); Key public_key(d2i_PUBKEY(nullptr,&cursor,static_cast<long>(decoded.size())),EVP_PKEY_free);
  if (!Supported(public_key.get()) || cursor != decoded.data()+decoded.size() || EVP_PKEY_eq(key.get(),public_key.get()) != 1) { error = "WebAuthn private and public keys do not match"; return false; }
  return true;
}
bool VirtualCredentialPublicKey(const std::string& private_key,std::string& public_key,std::string& error) {
  const auto key = Private(private_key); if (!key || !Public(key.get(),public_key)) { error = "Invalid native WebAuthn private key"; return false; } return true;
}
}
