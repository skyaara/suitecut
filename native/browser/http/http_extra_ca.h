// SPDX-License-Identifier: MIT
#pragma once
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509err.h>
#include <climits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace suitecut {
// Immutable additional trust anchors for an API context. suitecut sets no
// certificate count or byte limit; OpenSSL enforces its own.
class NativeHTTPExtraCA {
 public:
  static std::shared_ptr<const NativeHTTPExtraCA> Parse(const std::vector<std::string>& input,
                                                        std::string& error) {
    auto result = std::shared_ptr<NativeHTTPExtraCA>(new NativeHTTPExtraCA);
    for (const auto& pem : input) {
      if (pem.find('\0') != std::string::npos) {
        error = "Native API extra CA input contains NUL";
        return {};
      }
      std::string_view remaining = pem;
      while (!remaining.empty()) {
        while (!remaining.empty() && Space(remaining.front())) remaining.remove_prefix(1);
        if (remaining.empty()) break;
        constexpr std::string_view begin = "-----BEGIN CERTIFICATE-----",
                                   end = "-----END CERTIFICATE-----";
        const auto finish = remaining.find(end);
        if (!remaining.starts_with(begin) || finish == std::string_view::npos) {
          error = "Native API extra CA requires PEM certificates without other data";
          return {};
        }
        const auto block = remaining.substr(0, finish + end.size());
        // OpenSSL's memory BIO takes an int length; that is its own limit.
        if (block.size() > static_cast<size_t>(INT_MAX)) {
          error = "Native API extra CA PEM block is larger than OpenSSL accepts";
          return {};
        }
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(
            BIO_new_mem_buf(block.data(), static_cast<int>(block.size())), BIO_free);
        std::shared_ptr<X509> certificate(
            bio ? PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr) : nullptr, X509_free);
        if (!certificate) {
          ERR_clear_error();
          error = "Native API extra CA contains an invalid PEM certificate";
          return {};
        }
        result->certificates_.push_back(std::move(certificate));
        remaining.remove_prefix(block.size());
      }
      if (pem.empty() || std::string_view(pem).find(begin_marker) == std::string_view::npos) {
        error = "Native API extra CA contains an empty certificate";
        return {};
      }
    }
    return result;
  }
  bool Install(SSL_CTX* context) const {
    if (!context) return false;
    auto* store = SSL_CTX_get_cert_store(context);
    if (!store) return false;
    for (const auto& certificate : certificates_) {
      if (X509_STORE_add_cert(store, certificate.get()) == 1) continue;
      const auto error = ERR_peek_last_error();
      if (ERR_GET_LIB(error) == ERR_LIB_X509 &&
          ERR_GET_REASON(error) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
        ERR_clear_error();
        continue;
      }
      ERR_clear_error();
      return false;
    }
    return true;
  }
  size_t size() const { return certificates_.size(); }

 private:
  static constexpr std::string_view begin_marker = "-----BEGIN CERTIFICATE-----";
  static bool Space(char value) {
    return value == ' ' || value == '\r' || value == '\n' || value == '\t';
  }
  std::vector<std::shared_ptr<X509>> certificates_;
};
}  // namespace suitecut
