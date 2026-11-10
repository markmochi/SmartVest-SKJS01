#pragma once
#ifdef _WIN32
#include "../windows/mbedtls/sha256.h"
#else
// Host only; firmware resolves Espressif's actual mbedTLS headers.
#include <openssl/evp.h>
#include <cstddef>
struct mbedtls_sha256_context {EVP_MD_CTX* value=nullptr;};
inline void mbedtls_sha256_init(mbedtls_sha256_context* c){c->value=nullptr;}
inline void mbedtls_sha256_free(mbedtls_sha256_context* c){EVP_MD_CTX_free(c->value);c->value=nullptr;}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c,int sha224){mbedtls_sha256_free(c);c->value=EVP_MD_CTX_new();return sha224||!c->value||EVP_DigestInit_ex(c->value,EVP_sha256(),nullptr)!=1?-1:0;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const unsigned char* p,size_t n){return EVP_DigestUpdate(c->value,p,n)==1?0:-1;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,unsigned char* p){unsigned n=0;return EVP_DigestFinal_ex(c->value,p,&n)==1&&n==32?0:-1;}
inline void mbedtls_sha256_clone(mbedtls_sha256_context* to,const mbedtls_sha256_context* from){mbedtls_sha256_free(to);to->value=EVP_MD_CTX_new();if(to->value)EVP_MD_CTX_copy_ex(to->value,from->value);}
#endif
