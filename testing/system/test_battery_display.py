from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp)/'battery_display'
    subprocess.run(['g++','-std=c++17','-UNDEBUG',str(r/'test_battery_display.cpp'),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
