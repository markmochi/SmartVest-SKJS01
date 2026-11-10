"""Production clock/naming/telemetry/buffer checks; no radio emulation."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
j=Path(os.environ.get('SJKS_ARDUINOJSON',str(Path.home()/'Arduino/libraries/ArduinoJson/src')))
with tempfile.TemporaryDirectory() as t:
 p=Path(t)/'test'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-DARDUINOJSON_ENABLE_ARDUINO_STRING=0','-DARDUINOJSON_ENABLE_PROGMEM=0','-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0','-I'+str(j),str(r/'../testing/main/camera/developer_logs_test.cpp'),'-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)
print('PASS: actual clock/naming/telemetry code: Manila timestamp, invalid/leap dates, holdover expiry, legacy and safe paths, bounded spool, retry acknowledgement, replay deduplication, stream reset, ring eviction, schema rejection and 4KB payload bound')
