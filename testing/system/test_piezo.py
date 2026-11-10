"""Run actual Sensing/model/config/UART tests paired with the shipped Main receiver."""
from pathlib import Path
import os,runpy,sys
root=Path(__file__).resolve().parents[2]
old=sys.argv
try:
 sys.argv=[str(root/'testing/sensing/run_tests.py'),'--main',str(root/'Main_and_Cameras/SmartVest_SJKS01_Main'),'--arduinojson',os.environ['SJKS_ARDUINOJSON']]
 runpy.run_path(sys.argv[0],run_name='__main__')
finally:sys.argv=old
