from pathlib import Path
import argparse,hashlib

ROOT=Path(__file__).resolve().parents[1]
MANIFEST=ROOT/'SOURCE_MANIFEST.sha256'
PRIVATE={
 'smartvest-credentials.txt',
 'IMU Lab Test/SmartVest_IMU_Finalization_Direction_Fixed/LabWiFi.h',
 'Main_and_Cameras/SmartVest_SJKS01_Main/Config.h',
 'Main_and_Cameras/SmartVest_SJKS01_Camera/CameraConfig.h',
}

def files():
 for path in sorted(ROOT.rglob('*')):
  if not path.is_file() or path==MANIFEST:continue
  relative=path.relative_to(ROOT)
  if relative.as_posix() in PRIVATE or relative.name=='CameraPairingKeys.h':continue
  if any(part in {'__pycache__','.git','.zig-cache','zig-out','build','builds','results','node_modules'} for part in relative.parts):continue
  if path.suffix in {'.pyc','.png'} or path.name.endswith('-results.txt'):continue
  yield path
def snapshot():
 return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files()}
if __name__=='__main__':
 p=argparse.ArgumentParser(description='Create/check the single current SmartVest source manifest.')
 g=p.add_mutually_exclusive_group(required=True);g.add_argument('--write',action='store_true');g.add_argument('--check',action='store_true');args=p.parse_args()
 current=snapshot()
 if args.write:MANIFEST.write_text(''.join(f'{digest}  {name}\n' for name,digest in current.items()),encoding='utf8',newline='\n')
 else:
  saved={line[66:]:line[:64] for line in MANIFEST.read_text(encoding='utf8').splitlines()}
  if saved!=current:raise SystemExit('Manifest differs: '+', '.join(sorted(name for name in saved.keys()|current.keys() if saved.get(name)!=current.get(name))))
 print(f'PASS source manifest: {len(current)} current source files')
