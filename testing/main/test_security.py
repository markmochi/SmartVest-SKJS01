"""Execute the shipped Security.cpp with synthetic credentials and OpenSSL PBKDF2.
Requires Python 3, g++ and OpenSSL development headers; no device is simulated.
"""
from pathlib import Path
import shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[2]/'Main_and_Cameras'
source = root / 'SmartVest_SJKS01_Main'
checks = 0
for home in (False, True):
 for factory_allowed in (False, True):
  with tempfile.TemporaryDirectory() as temporary:
   folder = Path(temporary)
   shutil.copytree(root / '../testing/main/host', folder / 'host')
   for directory in ('src/security', 'src/storage', 'src/core', 'host/mbedtls'):
    (folder / directory).mkdir(parents=True, exist_ok=True)
   for name in ('Security.h', 'Security.cpp', 'Access.h'):
    shutil.copy(source / 'src/security' / name, folder / 'src/security' / name)
   shutil.copy(source / 'src/core/Models.h', folder / 'src/core/Models.h')
   (folder / 'src/storage/StateStore.h').write_text('#pragma once\n#include "../core/Models.h"\n')
   # Isolate authentication from unrelated SMS-formatting helpers.
   (folder / 'src/core/Domain.h').write_text('#pragma once\n#include <cstdint>\nnamespace Domain { inline bool elapsed(uint32_t now,uint32_t then,uint32_t interval){return uint32_t(now-then)>=interval;} }\n')
   (folder / 'Config.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
namespace Config {
constexpr size_t MAX_CONTACTS=5, MAX_SMS_JOBS=8, MAX_SESSIONS=4;
constexpr uint32_t SESSION_TIMEOUT_MS=120000, LOGIN_THROTTLE_MS=30000;
constexpr size_t RIDER_MIN_PASSWORD_LENGTH=12;
constexpr char DEFAULT_RIDER_PASSWORD[]="admin123";
constexpr char LEGACY_DEFAULT_PASSWORD[]="legacy-test-2026";
constexpr char DEVELOPER_USERNAME[]="developer";
constexpr char DEVELOPER_PASSWORD[]="developer-test-password";
constexpr bool USE_HOME_WIFI=''' + str(home).lower() + ''';
constexpr bool ALLOW_DEFAULT_RIDER_LOGIN=''' + str(factory_allowed).lower() + ''';
}
''')
   (folder / 'host/WebServer.h').write_text('#pragma once\n#include "Arduino.h"\nclass WebServer{public:String cookie;String header(const char*){return cookie;}};\n')
   (folder / 'host/esp_system.h').write_text('#pragma once\n#include <cstdint>\ninline uint32_t esp_random(){static uint32_t seed=42;seed=seed*1664525u+1013904223u;return seed>>24;}\n')
   (folder / 'host/mbedtls/md.h').write_text('#pragma once\n#define MBEDTLS_MD_SHA256 6\n')
   (folder / 'host/mbedtls/pkcs5.h').write_text('''#pragma once
#include <openssl/evp.h>
inline unsigned passwordDerivations=0;
inline int mbedtls_pkcs5_pbkdf2_hmac_ext(int,const unsigned char* password,size_t plen,const unsigned char* salt,size_t slen,unsigned long rounds,size_t length,unsigned char* output){++passwordDerivations;return PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(password),plen,salt,slen,rounds,EVP_sha256(),length,output)==1?0:-1;}
''')
   shutil.copy(root / '../testing/main/host/security_scenarios.cpp', folder / 'scenarios.cpp')
   command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(folder/'host'), '-I'+str(folder), str(folder/'src/security/Security.cpp'), str(folder/'scenarios.cpp'), '-lcrypto', '-o', str(folder/'test')]
   subprocess.run(command, check=True)
   subprocess.run([str(folder/'test')], check=True)
   checks += 1
print(f'PASS: actual Security.cpp in {checks} home/hotspot and factory-policy combinations')
