#pragma once
#ifdef _WIN32
#include "../windows/mbedtls/md.h"
#else
#include "sha256.h"
#define MBEDTLS_MD_SHA256 6
struct mbedtls_md_info_t {};
struct mbedtls_md_context_t {EVP_MD_CTX* value=nullptr;EVP_PKEY* key=nullptr;};
inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int type){static mbedtls_md_info_t info;return type==MBEDTLS_MD_SHA256?&info:nullptr;}
inline void mbedtls_md_init(mbedtls_md_context_t* c){c->value=nullptr;c->key=nullptr;}
inline void mbedtls_md_free(mbedtls_md_context_t* c){EVP_MD_CTX_free(c->value);EVP_PKEY_free(c->key);mbedtls_md_init(c);}
inline int mbedtls_md_setup(mbedtls_md_context_t* c,const mbedtls_md_info_t* info,int hmac){c->value=EVP_MD_CTX_new();return info&&hmac&&c->value?0:-1;}
inline int mbedtls_md_hmac_starts(mbedtls_md_context_t* c,const unsigned char* p,size_t n){c->key=EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC,nullptr,p,n);return c->key&&EVP_DigestSignInit(c->value,nullptr,EVP_sha256(),nullptr,c->key)==1?0:-1;}
inline int mbedtls_md_hmac_update(mbedtls_md_context_t* c,const unsigned char* p,size_t n){return EVP_DigestSignUpdate(c->value,p,n)==1?0:-1;}
inline int mbedtls_md_hmac_finish(mbedtls_md_context_t* c,unsigned char* p){size_t n=32;return EVP_DigestSignFinal(c->value,p,&n)==1&&n==32?0:-1;}
#endif
