"""Compile and exercise the production bounded parser without network hardware."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
 out=Path(tmp)/'http_parser'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(root/'testing/system/test_http_parser.cpp'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
