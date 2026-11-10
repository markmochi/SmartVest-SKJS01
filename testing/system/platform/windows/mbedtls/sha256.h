#pragma once
// Host verification only. Firmware uses Espressif's bundled mbedTLS.
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstddef>
#include <climits>
struct mbedtls_sha256_context {BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;};
inline void mbedtls_sha256_init(mbedtls_sha256_context* c){c->algorithm=nullptr;c->hash=nullptr;}
inline void mbedtls_sha256_free(mbedtls_sha256_context* c){if(c->hash)BCryptDestroyHash(c->hash);if(c->algorithm)BCryptCloseAlgorithmProvider(c->algorithm,0);mbedtls_sha256_init(c);}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c,int sha224){
  mbedtls_sha256_free(c);return sha224||BCryptOpenAlgorithmProvider(&c->algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptCreateHash(c->algorithm,&c->hash,nullptr,0,nullptr,0,0)<0?-1:0;
}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const unsigned char* bytes,size_t n){return !c->hash||n>ULONG_MAX||BCryptHashData(c->hash,const_cast<PUCHAR>(bytes),ULONG(n),0)<0?-1:0;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,unsigned char* bytes){return !c->hash||BCryptFinishHash(c->hash,bytes,32,0)<0?-1:0;}
inline void mbedtls_sha256_clone(mbedtls_sha256_context* to,const mbedtls_sha256_context* from){mbedtls_sha256_free(to);BCryptDuplicateHash(from->hash,&to->hash,nullptr,0,0);}
