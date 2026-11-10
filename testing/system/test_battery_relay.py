"""Actual Nano/Sensing/Main code against deterministic UART, I2C and NVS adapters.
Run through the handoff Windows Zig runner, or use g++ with ArduinoJson headers.
No physical peripherals or radio/SMS actions are used.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

r=Path(__file__).resolve().parents[2]
sense=r/'Sensing/SmartVest_SJKS01_Sensing'
main=r/'Main_and_Cameras/SmartVest_SJKS01_Main'
json=Path(os.environ['SJKS_ARDUINOJSON'])
copies=[r/'common/BatteryProtocol.h',sense/'src/power/BatteryProtocol.h',main/'src/power/BatteryProtocol.h',r/'Nano/SmartVest_INA226_Nano/src/BatteryProtocol.h']
assert len({p.read_bytes() for p in copies})==1
nano=r/'Nano/SmartVest_INA226_Nano'
assert not (nano/'src/ChargeConfig.h').exists() and not (nano/'src/ChargeCurrent.h').exists()
source=(nano/'SmartVest_INA226_Nano.ino').read_text()
assert all(token not in source for token in ['measureCharge','ChargeCurrent','ChargeConfig','charging=','net_mA='])
with tempfile.TemporaryDirectory() as tmp:
    t=Path(tmp);m=t/'main';shutil.copytree(main,m)
    # Combined executable shares the V1/V2 type identity across board copies.
    for name in ['CrashProtocol.h','ImuLabProtocol.h','PiezoProtocol.h']:
        (m/'src/link'/name).write_text('#include "'+(sense/'src/link'/name).as_posix()+'"\n',encoding='utf8')
    (m/'src/emergency/EmergencyCoordinator.h').write_text('''#pragma once
#include "../link/CrashProtocol.h"
class EmergencyCoordinator{public:bool readyForCrash()const{return true;}void observeImpact(uint32_t,uint32_t,uint8_t){}bool accept(const CrashProtocol::Frame&,bool,uint32_t){return true;}};
''',encoding='utf8')
    out=t/'roundtrip'
    subprocess.run(['g++','-std=c++17','-DARDUINO_ARCH_ESP32=1','-DCONFIG_IDF_TARGET_ESP32=1',
        '-I'+str(r/'testing/sensing/host/battery'),'-I'+str(r/'testing/sensing/host'),
        '-I'+str(sense),'-I'+str(m/'src/link'),'-I'+str(json),
        str(sense/'src/link/EventLink.cpp'),str(m/'src/link/SensorLink.cpp'),str(m/'src/link/ImuLabReceiver.cpp'),str(m/'src/link/PiezoReceiver.cpp'),
        str(r/'testing/sensing/host/battery_roundtrip.cpp'),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
    host=t/'power';host.mkdir()
    out=t/'power_test'
    subprocess.run(['g++','-std=c++17','-DARDUINO=10800','-DARDUINOJSON_ENABLE_ARDUINO_STRING=0',
        '-DARDUINOJSON_ENABLE_PROGMEM=0','-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
        '-I'+str(host),'-I'+str(r/'testing/main/host'),'-I'+str(json),'-I'+str(main),
        str(main/'src/power/BatteryService.cpp'),str(r/'testing/main/host/battery_power_scenarios.cpp'),
        '-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('PASS all four battery protocol copies agree; both actual-code suites passed')
