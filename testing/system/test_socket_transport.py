"""Optional real-socket HTTP and camera pacing checks on a POSIX host."""
from pathlib import Path
import os,subprocess,tempfile,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from http_core import prepare

if os.name!='posix':raise SystemExit('The real-socket suite requires a POSIX host; use test_http_transport.py for the portable transport model.')
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as temporary:
 folder=Path(temporary);binary=folder/'socket_transport';prepare(root.parents[1],folder)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG','-I'+str(root/'http_host'),'-I'+str(folder),str(root/'test_http_stability.cpp'),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
