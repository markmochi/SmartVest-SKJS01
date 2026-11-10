from pathlib import Path
import argparse,shutil

ROOT=Path(__file__).resolve().parents[1]
COPIES={
 'BatteryDisplay.h':['Nano/SmartVest_INA226_Nano/src/BatteryDisplay.h','Main_and_Cameras/SmartVest_SJKS01_Main/src/power/BatteryDisplay.h'],
 'PiezoProtocol.h':['Sensing/SmartVest_SJKS01_Sensing/src/link/PiezoProtocol.h','Main_and_Cameras/SmartVest_SJKS01_Main/src/link/PiezoProtocol.h'],
 'CameraCrypto.h':['Main_and_Cameras/SmartVest_SJKS01_Main/src/security/CameraCrypto.h','Main_and_Cameras/SmartVest_SJKS01_Camera/src/CameraCrypto.h'],
 'CameraAuth.h':['Main_and_Cameras/SmartVest_SJKS01_Main/src/security/CameraAuth.h','Main_and_Cameras/SmartVest_SJKS01_Camera/src/CameraAuth.h'],
 'CrashProtocol.h':['Sensing/SmartVest_SJKS01_Sensing/src/link/CrashProtocol.h','Main_and_Cameras/SmartVest_SJKS01_Main/src/link/CrashProtocol.h'],
 'ImuLabProtocol.h':['Sensing/SmartVest_SJKS01_Sensing/src/link/ImuLabProtocol.h','Main_and_Cameras/SmartVest_SJKS01_Main/src/link/ImuLabProtocol.h'],
 'BatteryProtocol.h':['Nano/SmartVest_INA226_Nano/src/BatteryProtocol.h','Sensing/SmartVest_SJKS01_Sensing/src/power/BatteryProtocol.h','Main_and_Cameras/SmartVest_SJKS01_Main/src/power/BatteryProtocol.h'],
 'CameraPairingKeys.h':['Main_and_Cameras/SmartVest_SJKS01_Main/src/security/CameraPairingKeys.h','Main_and_Cameras/SmartVest_SJKS01_Camera/src/CameraPairingKeys.h']}
if __name__=='__main__':
 p=argparse.ArgumentParser(description='Check or synchronize sketch-local copies of canonical common headers.')
 p.add_argument('--write',action='store_true');args=p.parse_args()
 for name,targets in COPIES.items():
  source=ROOT/'common'/name
  for target in targets:
   path=ROOT/target
   if args.write:shutil.copyfile(source,path)
   if source.read_bytes()!=path.read_bytes():raise SystemExit('Header mismatch: '+target)
 print('PASS shared protocol and pairing headers are synchronized')
