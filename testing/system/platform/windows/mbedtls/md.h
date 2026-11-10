#pragma once
#include "sha256.h"
#define MBEDTLS_MD_SHA256 6
struct mbedtls_md_info_t {};
struct mbedtls_md_context_t {BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;};
inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int type){static mbedtls_md_info_t value;return type==MBEDTLS_MD_SHA256?&value:nullptr;}
inline void mbedtls_md_init(mbedtls_md_context_t* c){c->algorithm=nullptr;c->hash=nullptr;}
inline void mbedtls_md_free(mbedtls_md_context_t* c){if(c->hash)BCryptDestroyHash(c->hash);if(c->algorithm)BCryptCloseAlgorithmProvider(c->algorithm,0);mbedtls_md_init(c);}
inline int mbedtls_md_setup(mbedtls_md_context_t* c,const mbedtls_md_info_t* info,int hmac){return !info||!hmac||BCryptOpenAlgorithmProvider(&c->algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0?-1:0;}
inline int mbedtls_md_hmac_starts(mbedtls_md_context_t* c,const unsigned char* key,size_t n){return n>ULONG_MAX||BCryptCreateHash(c->algorithm,&c->hash,nullptr,0,const_cast<PUCHAR>(key),ULONG(n),0)<0?-1:0;}
inline int mbedtls_md_hmac_update(mbedtls_md_context_t* c,const unsigned char* bytes,size_t n){return !c->hash||n>ULONG_MAX||BCryptHashData(c->hash,const_cast<PUCHAR>(bytes),ULONG(n),0)<0?-1:0;}
inline int mbedtls_md_hmac_finish(mbedtls_md_context_t* c,unsigned char* bytes){return !c->hash||BCryptFinishHash(c->hash,bytes,32,0)<0?-1:0;}
