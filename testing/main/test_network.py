"""Compile actual NetworkService.cpp with deterministic Wi-Fi/DNS/mDNS adapters.
This verifies logic/API usage against mocks, not an ESP32 build or radio test.
Run: python testing/main/test_network.py (g++ required).
"""
from pathlib import Path
import tempfile, shutil, subprocess, re
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
source=root/'SmartVest_SJKS01_Main'
def configure(sketch, home, ssid="Test-Network", password="Unique-Test-Passphrase", developer="Unique-Test-Developer-Passphrase", hostname="smartvest", allow_n=False):
 config=sketch/'Config.h'
 text=config.read_text()
 text,n=re.subn(r'(USE_HOME_WIFI\s*=\s*)(true|false)',lambda m:m[1]+str(home).lower(),text)
 assert n==1
 for name,value in [('HOME_WIFI_SSID',ssid),('HOME_WIFI_PASSWORD',password),('DEVELOPER_PASSWORD',developer),('LOCAL_HOSTNAME',hostname)]:
  text,n=re.subn(r'('+name+r'\[\]\s*=\s*)"[^"\n]*"',lambda m:m[1]+'"'+value+'"',text)
  assert n==1,name
 text=re.sub(r"(WIFI_AP_ALLOW_11N\s*=\s*)(true|false)",lambda m:m[1]+str(allow_n).lower(),text)
 config.write_text(text)
 (sketch/'src/diagnostics/Console.h').write_text('#pragma once\n#include <Arduino.h>\nnamespace Console { inline void line(const char* text){Serial.println(text);} }\n')
 (sketch/'src/network/LocalDnsServer.h').write_text('#pragma once\n#include <DNSServer.h>\nclass LocalDnsServer:public DNSServer {public:bool start(int port,IPAddress ip){return DNSServer::start(port,"*",ip);}};\n')

for use_home,local,allow_n in [(False,None,False),(False,True,False),(False,False,False),(True,True,False),(True,False,False),(False,None,True)]:
 with tempfile.TemporaryDirectory() as t:
  t=Path(t); sketch=t/'sketch'; shutil.copytree(source,sketch)
  config=sketch/'Config.h'
  configure(sketch,use_home,allow_n=allow_n)
  mode=[] if local is None else ['-DSV_HOTSPOT_LOCAL_DEVICE_MODE='+str(int(local))]
  cmd=['g++','-std=c++17','-DARDUINO_ARCH_ESP32','-DCONFIG_IDF_TARGET_ESP32','-Wall','-Wextra','-Werror',*mode,'-I'+str(root/'../testing/main/host'),'-I'+str(sketch),str(sketch/'src/network/NetworkService.cpp'),str(root/'../testing/main/host/network_scenarios.cpp'),'-o',str(t/'test')]
  subprocess.run(cmd,check=True);subprocess.run([str(t/'test')],check=True)
# Actionable compile-time config validation (bad settings must fail).
for ssid,password,dev,host in [('', '12345678','unique-dev-passphrase','smartvest'),
 ('Test','short','unique-dev-passphrase','smartvest'),
 ('Test','12345678','smartvest-skjs','smartvest'),
 ('Test','12345678','unique-dev-passphrase','bad.host')]:
 with tempfile.TemporaryDirectory() as t:
  t=Path(t);shutil.copytree(source,t/'sketch');sketch=t/'sketch'
  config=sketch/'Config.h'
  configure(sketch,True,ssid,password,dev,host)
  check=t/'check.cpp';check.write_text('#include "Config.h"\nint main(){}\n')
  result=subprocess.run(['g++','-std=c++17','-I'+str(root/'../testing/main/host'),'-I'+str(sketch),'-fsyntax-only',str(check)],capture_output=True,text=True)
  assert result.returncode and 'static assertion failed' in result.stderr,result.stderr
print('PASS: invalid SSID/password/developer-password/hostname rejected at compile time')
