#pragma once
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace CameraCrypto {
inline void hex(const uint8_t* data,size_t size,char* output) {
  static const char digits[]="0123456789abcdef";
  for(size_t i=0;i<size;++i){output[2*i]=digits[data[i]>>4];output[2*i+1]=digits[data[i]&15];}
  output[2*size]=0;
}
inline bool validHex(const char* s,size_t count) {
  if(!s || std::strlen(s)!=count)return false;
  for(size_t i=0;i<count;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
  return true;
}
inline bool equal(const char* a,const char* b,size_t size) {
  if(!a||!b||std::strlen(a)!=size||std::strlen(b)!=size)return false;
  volatile uint8_t difference=0;
  for(size_t i=0;i<size;++i)difference|=uint8_t(a[i]^b[i]);
  return difference==0;
}
class Hash {
 public:
  Hash(){mbedtls_sha256_init(&context);}
  ~Hash(){mbedtls_sha256_free(&context);}
  Hash(const Hash&)=delete;Hash& operator=(const Hash&)=delete;
  bool begin(){good=mbedtls_sha256_starts(&context,0)==0;return good;}
  bool add(const void* data,size_t length){return good=good&&mbedtls_sha256_update(&context,static_cast<const uint8_t*>(data),length)==0;}
  bool finish(char* output){uint8_t digest[32];if(!good||mbedtls_sha256_finish(&context,digest))return false;hex(digest,32,output);good=false;return true;}
  bool peek(char* output)const{if(!good)return false;mbedtls_sha256_context copy;mbedtls_sha256_init(&copy);mbedtls_sha256_clone(&copy,&context);uint8_t digest[32];const bool ok=mbedtls_sha256_finish(&copy,digest)==0;mbedtls_sha256_free(&copy);if(ok)hex(digest,32,output);return ok;}
 private:
  mbedtls_sha256_context context;bool good=false;
};
class Mac {
 public:
  Mac(){mbedtls_md_init(&context);}
  ~Mac(){mbedtls_md_free(&context);}
  Mac(const Mac&)=delete;Mac& operator=(const Mac&)=delete;
  bool begin(const char* key) {
    if(!validHex(key,64))return false;
    uint8_t bytes[32];
    for(size_t i=0;i<32;++i){auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};bytes[i]=uint8_t(nibble(key[2*i])*16+nibble(key[2*i+1]));}
    mbedtls_md_free(&context);mbedtls_md_init(&context);
    good=mbedtls_md_setup(&context,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1)==0;
    good=good&&mbedtls_md_hmac_starts(&context,bytes,sizeof(bytes))==0;
    std::memset(bytes,0,sizeof(bytes));return good;
  }
  bool add(const void* data,size_t length){return good=good&&mbedtls_md_hmac_update(&context,static_cast<const uint8_t*>(data),length)==0;}
  bool field(const char* text){return text&&add(text,std::strlen(text))&&add("\n",1);}
  bool finish(char* output){uint8_t digest[32];if(!good||mbedtls_md_hmac_finish(&context,digest))return false;hex(digest,32,output);good=false;return true;}
 private:
  mbedtls_md_context_t context;bool good=false;
};
}
