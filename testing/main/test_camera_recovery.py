"""Host-only late-start/recovery tests. Requires g++, Python 3 and Node.js."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
with tempfile.TemporaryDirectory() as t:
    binary=Path(t)/'recovery'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(root/'../testing/main/camera/recovery_test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
subprocess.run(['node',str(root/'../testing/main/test_camera_recovery.cjs')],check=True)
