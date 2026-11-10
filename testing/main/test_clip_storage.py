"""Run the actual C++ storage helpers against a temporary SD-shaped filesystem."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]/'Main_and_Cameras'
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    binary = folder / 'clip_storage.exe'
    subprocess.run(['g++', '-std=c++17','-I'+str(root.parent/'testing/system/platform'), '-Wall', '-Wextra', '-Werror',
                    '-I'+str(root/'../testing/main/camera'),
                    str(root/'../testing/main/camera/clip_storage_test.cpp'), '-lcrypto', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(folder/'card')], check=True)
