"""Execute the full admin status handler and actual service JSON writers on host.

Compile for 32-bit pointers and the same ArduinoJson pool profile as ESP32.
Only networking, serial I/O, ESP heap readings and NVS are substituted. This is
not an ESP32 heap measurement or a physical-radio endurance test.
"""
from pathlib import Path
import os,re,shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'testing/system'))
from http_core import method
main=root/'Main_and_Cameras/SmartVest_SJKS01_Main'
with tempfile.TemporaryDirectory() as temporary:
    folder=Path(temporary);sketch=folder/'sketch';host=folder/'host'
    shutil.copytree(main,sketch);shutil.copytree(root/'testing/main/host',host)
    # White-box fixture access changes visibility in disposable copies only.
    for name in ['gsm/SIM800LService.h','emergency/EmergencyCoordinator.h','link/SensorLink.h']:
        file=sketch/'src'/name;file.write_text(file.read_text().replace(' private:',' public:'),encoding='utf8')
    p=host/'Arduino.h';text=p.read_text().replace('class String {','bool allowReserve(size_t);\nclass String {')
    p.write_text(text.replace('bool reserve(size_t n){v.reserve(n);return true;}','bool reserve(size_t n){if(!allowReserve(n))return false;v.reserve(n);return true;}'),encoding='utf8')
    p=host/'Preferences.h';text=p.read_text();at=text.index(' bool begin(')
    p.write_text(text[:at]+''' size_t getBytesLength(const char*){return 0;}
 size_t getBytes(const char*,void*,size_t){return 0;}
 size_t putBytes(const char*,const void*,size_t n){return n;}
'''+text[at:],encoding='utf8')
    (host/'esp_system.h').write_text('#pragma once\ninline unsigned esp_reset_reason(){return 1;}\ninline unsigned esp_random(){return 1234;}\n',encoding='utf8')
    (sketch/'src/storage/StateStore.h').write_text('''#pragma once
#include <Preferences.h>
#include "../core/Models.h"
class StateStore {public:DeviceState data;bool healthy=true,writable=true,failWrites=false;String lastError;
bool commit(const DeviceState& d){if(failWrites)return false;data=d;return true;}
static int findFreeJobSlot(DeviceState&);
static bool enqueueSms(DeviceState&,const String&,const String&,const String&);};
''',encoding='utf8')
    storage=(main/'src/storage/StateStore.cpp').read_text()
    (folder/'sms_slots.cpp').write_text('#include "src/storage/StateStore.h"\n#include "src/core/Domain.h"\n'+storage[storage.index('int StateStore::findFreeJobSlot('):],encoding='utf8')
    source=(main/'src/web/DashboardServer.cpp').read_text()
    rider=(main/'src/web/rider/RiderDashboard.cpp').read_text()
    (folder/'handlers.inc').write_text(method(source,'template <typename Camera>')+'\n'+'\n'.join(method(source,'void DashboardServer::'+name+'(') for name in ['sendJson','sendStatus'])+'\n'+method(rider,'bool riderCameraFrameFresh(')+'\n'+method(rider,'void DashboardServer::sendRiderStatus(')+'\n'+'\n'.join(method((main/'src/web/MessageQueue.cpp').read_text(encoding='utf8'),'void DashboardServer::'+name+'(') for name in ['writeMessageQueue','cancelMessageSelection']),encoding='utf8')
    sources=[sketch/'src'/n for n in ['gsm/SIM800LService.cpp','gps/NEO6MService.cpp','power/BatteryService.cpp','link/SensorLink.cpp','link/ImuLabReceiver.cpp','link/PiezoReceiver.cpp','emergency/EmergencyCoordinator.cpp']]
    sources += [Path(os.environ['SJKS_TINYGPS'])/'TinyGPS++.cpp',folder/'sms_slots.cpp',root/'testing/main/host/status_response.cpp']
    command=['g++','-m32','-std=c++17','-DARDUINO=10800','-DARDUINOJSON_ENABLE_ARDUINO_STRING=0','-DARDUINOJSON_ENABLE_PROGMEM=0','-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0','-Wall','-Wextra','-Werror','-Wno-implicit-fallthrough','-UNDEBUG','-I'+str(host),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+os.environ['SJKS_TINYGPS'],'-I'+str(sketch),'-I'+str(sketch/'src/web'),'-I'+str(folder),'-I'+str(root/'common'),'-I'+str(root/'testing/system/platform'),*map(str,sources),'-lcrypto','-o',str(folder/'test')]
    subprocess.run(command,check=True);subprocess.run([str(folder/'test')],check=True)
