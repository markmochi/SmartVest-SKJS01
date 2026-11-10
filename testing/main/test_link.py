"""Production reconnect policy test; no radio hardware is emulated."""
import subprocess,tempfile
from pathlib import Path
r=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
with tempfile.TemporaryDirectory() as t:
 p=Path(t)/'test'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(r/'../testing/main/camera/link_policy_test.cpp'),'-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)
print('PASS: production reconnect policy: join timeout, no premature cancellation, duplicate callback, exponential/capped backoff, role phase, jitter, healthy link, timer wrap')
