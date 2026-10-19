// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace suitecut {
struct VirtualCredential {
  std::string id, rp_id, user_handle, private_key, public_key;
  uint32_t sign_count = 0;
};
std::string CredentialBase64URL(const unsigned char* data, size_t length);
bool CredentialDecodeBase64URL(const std::string& text, size_t maximum, std::vector<unsigned char>& output);
bool GenerateVirtualCredential(const std::string& rp_id, uint32_t sign_count, VirtualCredential& output, std::string& error);
bool ValidateVirtualCredential(const VirtualCredential& input, std::string& error);
bool VirtualCredentialPublicKey(const std::string& private_key, std::string& public_key, std::string& error);
}
