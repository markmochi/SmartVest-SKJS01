"""Compile actual candidate code with host hardware adapters.

Optional --main points at the paired SmartVest_SJKS01_Main folder.
This tests the actual receiver, replacing only EmergencyCoordinator with a
commit/dedup adapter. It does not test physical UART, NVS, IMU, SMS or cameras.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys
sys.dont_write_bytecode=True
os.environ.setdefault('PYTHONDONTWRITEBYTECODE','1')

p = argparse.ArgumentParser()
p.add_argument('--zig', type=Path)
p.add_argument('--core-only', action='store_true')
p.add_argument('--main', type=Path)
p.add_argument('--lab-only', action='store_true')
p.add_argument('--arduinojson', type=Path, default=Path(os.environ.get('SJKS_ARDUINOJSON', '/nonexistent')))
args = p.parse_args()
root = Path(__file__).resolve().parents[2]/'Sensing'
sketch = root / 'SmartVest_SJKS01_Sensing'
host = root / '../testing/sensing/host'
flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
if args.zig: flags=['-std=c++17','-UNDEBUG','-g']
compiler=[str(args.zig.resolve()),'c++'] if args.zig else ['g++']
sources = [str(sketch/'src/link/EventLink.cpp'), str(sketch/'src/sensing/Imu.cpp'), str(sketch/'src/sensing/ImuLab.cpp'), str(sketch/'src/sensing/PiezoInputs.cpp')]
with tempfile.TemporaryDirectory() as work:
    work = Path(work)
    def build(name, tests, defines=(), extra=(), includes=()):
        out = work/(name+'.exe' if os.name=='nt' else name)
        command = [*compiler, *flags, '-I'+str(host), '-I'+str(sketch), *includes,
                   *['-D'+d for d in [*defines,'ARDUINO_ARCH_ESP32=1','CONFIG_IDF_TARGET_ESP32=1']], *sources, str(host/tests), *extra, '-o', str(out)]
        subprocess.run(command, check=True)
        subprocess.run([str(out)], check=True, env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0'})
    if not args.lab_only:
        build('unit', 'unit_tests.cpp', ['SV_ENABLE_SYSTEM_TEST=1'])
        build('driver', 'driver_tests.cpp')
        build('protocol', 'protocol_tests.cpp')
        build('protocol_disabled', 'protocol_tests.cpp', ['SV_ARM_CRASH_DETECTION=0'])
        build('adc', 'piezo_tests.cpp', ['SV_PIEZO_MAPPING_CONFIRMED=1', 'SV_PIEZO_CIRCUIT_VERIFIED=1'])
        build('piezo_impact', 'piezo_impact_tests.cpp')
        build('piezo_support', 'piezo_support_tests.cpp')
        build('piezo_controls', 'piezo_controls_tests.cpp')
    if not args.lab_only and not args.core_only:
        build('interfaces_disabled', 'sketch_tests.cpp', ['SV_UART_MAPPING_CONFIRMED=0', 'SV_IMU_MAPPING_CONFIRMED=0', 'SV_PIEZO_MAPPING_CONFIRMED=0', 'SV_PIEZO_CIRCUIT_VERIFIED=0'])
        build('shipped_config', 'shipped_config_tests.cpp')
        build('enabled', 'sketch_tests.cpp', ['SV_UART_MAPPING_CONFIRMED=1', 'SV_IMU_MAPPING_CONFIRMED=1', 'SV_PIEZO_MAPPING_CONFIRMED=1', 'SV_PIEZO_CIRCUIT_VERIFIED=1'])
    if args.main:
        main = args.main.resolve()
        if not (args.arduinojson/'ArduinoJson.h').is_file():
            raise SystemExit('Set --arduinojson to ArduinoJson 7 src folder')
        assert (main/'src/link/CrashProtocol.h').read_bytes() == (sketch/'src/link/CrashProtocol.h').read_bytes(), 'V1 protocol mismatch'
        adapted = work/'main'
        for name in ['Config.h', 'src/network/ConfigValidation.h', 'src/link/SensorLink.h',
                     'src/link/SensorLink.cpp', 'src/link/CrashProtocol.h', 'src/link/ImuLabProtocol.h', 'src/link/PiezoProtocol.h', 'src/link/PiezoReceiver.cpp', 'src/link/ImuLabReceiver.cpp', 'src/diagnostics/UartHealth.h','src/power/BatteryProtocol.h']:
            source = main/name
            target = adapted/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            print('Main reference SHA256', name, hashlib.sha256(source.read_bytes()).hexdigest())
        (adapted/'src/link/PiezoProtocol.h').write_text('#include "'+(sketch/'src/link/PiezoProtocol.h').as_posix()+'"\n')
        # Identical V1 headers share a file identity in this combined host executable.
        protocol = adapted/'src/link/CrashProtocol.h'
        protocol.unlink()
        protocol.write_text('#include "'+(sketch/'src/link/CrashProtocol.h').as_posix()+'"\n')
        lab_protocol = adapted/'src/link/ImuLabProtocol.h'
        lab_protocol.unlink()
        lab_protocol.write_text('#include "'+(sketch/'src/link/ImuLabProtocol.h').as_posix()+'"\n')
        emergency = adapted/'src/emergency/EmergencyCoordinator.h'
        emergency.parent.mkdir(parents=True, exist_ok=True)
        emergency.write_text('''#pragma once
#include <set>
#include <utility>
#include "../link/CrashProtocol.h"
class EmergencyCoordinator {public:bool readyForCrash()const{return true;}void observeImpact(uint32_t,uint32_t,uint8_t){}
 CrashProtocol::Frame lastFrame;
 bool commitOk=true,lastAgeKnown=false; uint32_t lastAge=0,unique=0;
 std::set<std::pair<uint32_t,uint32_t>> ids;
 bool accept(const CrashProtocol::Frame& f,bool known,uint32_t age){
  lastFrame=f;lastAgeKnown=known;lastAge=age;if(!commitOk)return false;
  if(ids.insert({f.boot,f.sequence}).second)++unique;
  return true;
 }
};
''')
        if not args.lab_only:
            build('main_roundtrip', 'main_roundtrip.cpp', ['SV_ENABLE_SYSTEM_TEST=1', 'SV_PIEZO_MAPPING_CONFIRMED=1', 'SV_PIEZO_CIRCUIT_VERIFIED=1'],
                  [str(adapted/'src/link/SensorLink.cpp'),str(adapted/'src/link/ImuLabReceiver.cpp'),str(adapted/'src/link/PiezoReceiver.cpp')],
                  ['-I'+str(adapted/'src/link'), '-I'+str(args.arduinojson.resolve())])
        build('lab_roundtrip', 'lab_roundtrip.cpp', [],
              [str(adapted/'src/link/SensorLink.cpp'),str(adapted/'src/link/ImuLabReceiver.cpp'),str(adapted/'src/link/PiezoReceiver.cpp')],
              ['-I'+str(adapted/'src/link'), '-I'+str(args.arduinojson.resolve())])
        build('automatic_roundtrip', 'automatic_roundtrip.cpp', [],
              [str(adapted/'src/link/SensorLink.cpp'),str(adapted/'src/link/ImuLabReceiver.cpp'),str(adapted/'src/link/PiezoReceiver.cpp')],
              ['-I'+str(adapted/'src/link'), '-I'+str(args.arduinojson.resolve())])
        print('PASS: candidate and main V1 protocol headers are byte-identical')
    else:
        print('SKIPPED main roundtrip: pass --main and --arduinojson to test your paired Main receiver')
