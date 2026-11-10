from pathlib import Path
import subprocess,tempfile

ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    binary=Path(tmp)/'detector'
    subprocess.run(['g++','-std=c++17','-UNDEBUG','-I'+str(ROOT/'Sensing/SmartVest_SJKS01_Sensing'),str(ROOT/'testing/sensing/host/detector_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
