#pragma once
#include <windows.h>
#include <bcrypt.h>

// Host tests only: the firmware uses mbedTLS. This adapter uses Windows CNG for PBKDF2.
struct EVP_MD {};
inline const EVP_MD* EVP_sha256() { static EVP_MD algorithm; return &algorithm; }
inline int PKCS5_PBKDF2_HMAC(const char* password, int passwordBytes,
                           const unsigned char* salt, int saltBytes,
                           int iterations, const EVP_MD*, int outputBytes,
                           unsigned char* output) {
  if (!password || !salt || !output || passwordBytes < 0 || saltBytes < 0 ||
      iterations <= 0 || outputBytes <= 0) return 0;
  BCRYPT_ALG_HANDLE provider = nullptr;
  if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr,
                                 BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return 0;
  const auto status = BCryptDeriveKeyPBKDF2(provider,
      reinterpret_cast<PUCHAR>(const_cast<char*>(password)), ULONG(passwordBytes),
      const_cast<PUCHAR>(salt), ULONG(saltBytes), ULONGLONG(iterations),
      output, ULONG(outputBytes), 0);
  BCryptCloseAlgorithmProvider(provider, 0);
  return status >= 0;
}
