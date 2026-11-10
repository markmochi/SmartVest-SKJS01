"""Compile actual storage/authentication sources against a deterministic NVS adapter.
Requires g++, OpenSSL headers and ArduinoJson 7 headers; override the latter path
with SJKS_ARDUINOJSON. Credentials are synthetic. No physical flash is exercised.
"""
from pathlib import Path
import os,re,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
headers=Path(os.environ.get('SJKS_ARDUINOJSON','/tmp/sjks-arduino/user/libraries/ArduinoJson/src'))
assert (headers/'ArduinoJson.h').is_file(), 'Set SJKS_ARDUINOJSON to ArduinoJson/src'
for revision in (0,1,2):
 with tempfile.TemporaryDirectory() as temporary:
  folder=Path(temporary)
  shutil.copytree(root/'SmartVest_SJKS01_Main',folder/'sketch')
  shutil.copytree(root/'../testing/main/host',folder/'host')
  adapter=folder/'host/Arduino.h'
  adapter.write_text(adapter.read_text(encoding='utf8').replace('bool reserve(size_t n){v.reserve(n);return true;}', 'inline static bool failReserve=false,shortWrite=false;\n bool reserve(size_t n){if(failReserve)return false;v.reserve(n);return true;}').replace('size_t write(uint8_t c){v+=char(c);return 1;}', 'size_t write(uint8_t c){if(shortWrite)return 0;v+=char(c);return 1;}').replace('size_t write(const uint8_t* data,size_t count){v.append(reinterpret_cast<const char*>(data),count);return count;}', 'size_t write(const uint8_t* data,size_t count){if(shortWrite)return 0;v.append(reinterpret_cast<const char*>(data),count);return count;}'),encoding='utf8')
  config=folder/'sketch/Config.h';text=config.read_text()
  values={'DEFAULT_RIDER_PASSWORD':'admin123','LEGACY_DEFAULT_PASSWORD':'legacy-test-2026','DEVELOPER_PASSWORD':'developer-test-password','DEVELOPER_USERNAME':'developer','HOME_WIFI_SSID':'Test-Network','HOME_WIFI_PASSWORD':'Unique-Test-Passphrase','WIFI_PASSWORD':'Unique-Test-Passphrase'}
  for name,value in values.items():
   text,count=re.subn(r'(\b'+name+r'\[\]\s*=\s*)"[^"\n]*"',lambda match:match[1]+'"'+value+'"',text)
   assert count==1,name
  text=re.sub(r'(RIDER_ACCOUNT_RESET_REVISION\s*=\s*)\d+',lambda m:m[1]+str(revision),text)
  config.write_text(text)
  # Keep the actual storage functions; substitute only their JSON allocator.
  storage=folder/'sketch/src/storage/StateStore.cpp'
  source=storage.read_text(encoding='utf8')
  storage.write_text('#include "allocation_fault.h"\n'+source.replace('JsonDocument document;', 'FaultDocument document;').replace('JsonDocument record;', 'FaultDocument record;'),encoding='utf8')
  (folder/'host/allocation_fault.h').write_text('''#pragma once
#include <ArduinoJson.h>
#include <cstdlib>
inline int failDocumentAt=0,documentNumber=0;
struct FaultAllocator:ArduinoJson::Allocator {
 bool fail=false;
 void* allocate(size_t n)override{return fail?nullptr:std::malloc(n);}
 void deallocate(void* p)override{std::free(p);}
 void* reallocate(void* p,size_t n)override{return fail?nullptr:std::realloc(p,n);}
};
inline FaultAllocator settingsAllocator,historyAllocator;
struct FaultDocument:JsonDocument {
 static ArduinoJson::Allocator* next(){auto& a=++documentNumber==1?settingsAllocator:historyAllocator;a.fail=failDocumentAt==documentNumber;return &a;}
 FaultDocument():JsonDocument(next()){}
};
''',encoding='utf8')
  (folder/'host/mbedtls').mkdir()
  (folder/'host/esp_system.h').write_text('#pragma once\n#include <cstdint>\ninline uint32_t esp_random(){static uint32_t seed=42;seed=seed*1664525u+1013904223u;return seed>>24;}\n')
  (folder/'host/WebServer.h').write_text('#pragma once\n#include "Arduino.h"\nclass WebServer{public:String cookie;String header(const char*){return cookie;}};\n')
  (folder/'host/mbedtls/md.h').write_text('#pragma once\n#define MBEDTLS_MD_SHA256 6\n')
  (folder/'host/mbedtls/pkcs5.h').write_text('#pragma once\n#include <openssl/evp.h>\ninline int mbedtls_pkcs5_pbkdf2_hmac_ext(int,const unsigned char* password,size_t plen,const unsigned char* salt,size_t slen,unsigned long rounds,size_t length,unsigned char* output){return PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(password),plen,salt,slen,rounds,EVP_sha256(),length,output)==1?0:-1;}\n')
  command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(folder/'host'),'-I'+str(headers),'-I'+str(folder/'sketch'),str(folder/'sketch/src/security/Security.cpp'),str(folder/'sketch/src/storage/StateStore.cpp'),str(folder/'host/storage_scenarios.cpp'),'-lcrypto','-o',str(folder/'test')]
  subprocess.run(command,check=True);subprocess.run([str(folder/'test')],check=True)
print('PASS: one-time account reset and preservation in three reset-policy variants')
