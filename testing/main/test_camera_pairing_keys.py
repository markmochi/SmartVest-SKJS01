"""Pairing identity must be private, distinct by role and synchronized.
Actual erase/reboot persistence is exercised by test_storage.py.
"""
from pathlib import Path
import re
r=Path(__file__).resolve().parents[2]
copies=[r/'common/CameraPairingKeys.h',r/'Main_and_Cameras/SmartVest_SJKS01_Main/src/security/CameraPairingKeys.h',r/'Main_and_Cameras/SmartVest_SJKS01_Camera/src/CameraPairingKeys.h']
assert len({p.read_bytes() for p in copies})==1
keys=re.findall(r'constexpr char (?:FRONT|REAR)_KEY\[\] = "([0-9a-f]{64})";',copies[0].read_text(encoding='utf8'))
assert len(keys)==2 and keys[0]!=keys[1]
camera=(r/'Main_and_Cameras/SmartVest_SJKS01_Camera/CameraConfig.h').read_text(encoding='utf8')
assert "ROLE[0]=='f' ? CameraPairing::FRONT_KEY : CameraPairing::REAR_KEY" in camera
assert not re.search(r'constexpr char TOKEN\[\] = "[0-9a-f]{64}"',camera)
store=(r/'Main_and_Cameras/SmartVest_SJKS01_Main/src/storage/StateStore.cpp').read_text(encoding='utf8')
assert 'data.cameraKeys[0] = CameraPairing::FRONT_KEY;' in store
assert 'data.cameraKeys[1] = CameraPairing::REAR_KEY;' in store
assert not re.search(r'data\.cameraKeys\[\d\] = Security::randomHex',store)
assert 'Firmware::VERSION' not in store
print('PASS three synchronized private camera-key copies; distinct 64-hex keys; role-selected camera token; Main identity independent of firmware version/random NVS initialization')
