from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
checker=root/'Main_and_Cameras/tools/check_flash_budget.py'
partition=root/'Main_and_Cameras/SmartVest_SJKS01_Main/partitions.csv'
with tempfile.TemporaryDirectory() as temporary:
    build=Path(temporary)
    (build/'partitions.csv').write_bytes(partition.read_bytes())
    image=build/'SmartVest_SJKS01_Main.ino.bin'
    def check(size,passed):
        with image.open('wb') as stream: stream.truncate(size)
        result=subprocess.run([sys.executable,str(checker),str(build)],capture_output=True,text=True)
        assert (result.returncode==0)==passed,result.stdout+result.stderr
    check(1310720-8192,True)
    check(1310720-8191,False)
    check(1310721,False)
    (build/'partitions.csv').write_text('changed partition layout')
    check(1200000,False)
print('PASS exact IDE reserve boundary, oversized image and changed partition rejection')
