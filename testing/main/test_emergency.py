"""Tests actual main emergency coordinator using host NVS/GPS/job adapters."""
import os,shutil,subprocess,tempfile
from pathlib import Path
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
headers=Path(os.environ['SJKS_ARDUINOJSON'])
with tempfile.TemporaryDirectory() as t:
 p=Path(t);s=p/'sketch';h=p/'host'
 shutil.copytree(r/'SmartVest_SJKS01_Main',s);shutil.copytree(r/'../testing/main/host',h)
 prefs=(h/'Preferences.h').read_text();prefs=prefs[:prefs.index('class Preferences')]+"""
 inline std::vector<uint8_t> blob;inline bool failWrite=false;
 class Preferences {public:
 bool begin(const char*,bool){return true;}
 size_t getBytesLength(const char*){return blob.size();}
 size_t getBytes(const char*,void*p,size_t n){if(n!=blob.size())return 0;std::memcpy(p,blob.data(),n);return n;}
 size_t putBytes(const char*,const void*p,size_t n){if(failWrite)return 0;blob.assign((const uint8_t*)p,(const uint8_t*)p+n);return n;}
 };
"""
 (h/'Preferences.h').write_text(prefs)
 (h/'esp_system.h').write_text('#pragma once\n')
 (s/'src/gps/NEO6MService.h').write_text('#pragma once\n#include "../core/Models.h"\nclass NEO6MService{public:bool latestLocation(double&,double&,uint32_t&,bool&){return false;}};\n')
 (s/'src/storage/StateStore.h').write_text('#pragma once\n#include <Preferences.h>\n#include "../core/Models.h"\nclass StateStore{public:DeviceState data;bool healthy=true,writable=true,failCommit=false;bool commit(const DeviceState& next){if(failCommit)return false;data=next;return true;}static bool enqueueSms(DeviceState&,const char*,const char*,const char*){return false;}};\n')
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(h),'-I'+str(headers),'-I'+str(s),str(s/'src/emergency/EmergencyCoordinator.cpp'),str(r/'../testing/main/camera/emergency_test.cpp'),'-I'+str(s/'src/emergency'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: actual main coordinator: no-SMS tests, retries/deduplication, per-camera ACKs, NVS failure, outage persistence, three-test reserve, reboot timing')
