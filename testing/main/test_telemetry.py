"""Compile actual modem/GPS/power/sensing/serial code with deterministic adapters.
No ESP32, SIM radio, I2C device or real SMS is used by these tests.
"""
from pathlib import Path
import os,re,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
json_headers=Path(os.environ.get('SJKS_ARDUINOJSON','/tmp/sjks-arduino/user/libraries/ArduinoJson/src'))
gps_headers=Path(os.environ.get('SJKS_TINYGPS','/tmp/sjks-arduino/user/libraries/TinyGPSPlus/src'))
output=[]
for home in [True,False]:
 with tempfile.TemporaryDirectory() as temporary:
  folder=Path(temporary);sketch=folder/'sketch';host=folder/'host'
  shutil.copytree(root/'SmartVest_SJKS01_Main',sketch);shutil.copytree(root/'../testing/main/host',host)
  config=sketch/'Config.h';text=config.read_text()
  for name,value in {'HOME_WIFI_SSID':'Test-Network','HOME_WIFI_PASSWORD':'Test-Home-Password','WIFI_NAME':'Test-Vest','WIFI_PASSWORD':'Test-Vest-Password','DEVELOPER_USERNAME':'test-maintainer','DEVELOPER_PASSWORD':'Test-Maintainer-Password','LEGACY_DEFAULT_PASSWORD':'Test-Legacy-Password'}.items():
   text=re.sub(r'(\b'+name+r'\[\]\s*=\s*)"[^"\n]*"',lambda m:m[1]+'"'+value+'"',text)
  for name,value in {'USE_HOME_WIFI':home,'ENABLE_SIM800L':True,'ENABLE_NEO6M':True,'ENABLE_SENSOR_LINK':True,'ENABLE_USB_DEBUG':True}.items():
   text=re.sub(r'(\b'+name+r'\s*=\s*)(true|false)',lambda m:m[1]+str(value).lower(),text)
  config.write_text(text)
  (sketch/'src/storage/StateStore.h').write_text('#pragma once\n#include "../core/Models.h"\nclass StateStore {public:DeviceState data;bool healthy=true,writable=true,failWrites=false;bool commit(const DeviceState& d){if(failWrites)return false;data=d;return true;}static int findFreeJobSlot(DeviceState&);static bool enqueueSms(DeviceState&,const String&,const String&,const String&);};\n')
  # Exercise the production allocator while substituting only NVS persistence.
  storage_source=(root/'SmartVest_SJKS01_Main/src/storage/StateStore.cpp').read_text()
  allocator_source=storage_source[storage_source.index('int StateStore::findFreeJobSlot('):]
  (host/'sms_slots.cpp').write_text('#include "src/storage/StateStore.h"\n#include "src/core/Domain.h"\n'+allocator_source)
  (sketch/'src/emergency/EmergencyCoordinator.h').write_text('#pragma once\n#include "../link/CrashProtocol.h"\nclass EmergencyCoordinator{public:bool readyForCrash()const{return true;}void observeImpact(uint32_t,uint32_t,uint8_t){}bool writable=true;bool accept(const CrashProtocol::Frame&,bool,uint32_t){return true;}};\n')
  sources=[sketch/'src/gsm/SIM800LService.cpp',sketch/'src/gps/NEO6MService.cpp',sketch/'src/power/BatteryService.cpp',sketch/'src/link/SensorLink.cpp',sketch/'src/link/ImuLabReceiver.cpp',sketch/'src/link/PiezoReceiver.cpp',sketch/'src/diagnostics/SerialReporter.cpp',gps_headers/'TinyGPS++.cpp',host/'sms_slots.cpp',host/'telemetry_scenarios.cpp']
  compiled=subprocess.run(['g++','-std=c++17','-DARDUINO=10800','-DARDUINOJSON_ENABLE_ARDUINO_STRING=0','-DARDUINOJSON_ENABLE_PROGMEM=0','-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0','-Wall','-Wextra','-Werror','-Wno-implicit-fallthrough','-I'+str(host),'-I'+str(json_headers),'-I'+str(gps_headers),'-I'+str(sketch),*map(str,sources),'-o',str(folder/'test')],capture_output=True,text=True)
  if compiled.returncode:raise SystemExit(compiled.stderr[-6000:])
  result=subprocess.run([str(folder/'test')],capture_output=True,text=True)
  if result.returncode:raise SystemExit(result.stdout+result.stderr)
  output.append(result.stdout)
results=Path(os.environ.get('SJKS_TEST_OUTPUT',root.parent/'testing/results/telemetry'))
results.mkdir(parents=True,exist_ok=True)
(results/'telemetry-results.txt').write_text(''.join(output))
print(''.join(output),end='')
