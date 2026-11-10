from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,tempfile,sys

ROOT=Path(__file__).resolve().parents[1]
TARGETS={
 'main':('Main_and_Cameras/SmartVest_SJKS01_Main','esp32:esp32:esp32:PSRAM=disabled,PartitionScheme=default,DebugLevel=none'),
 'sensing':('Sensing/SmartVest_SJKS01_Sensing','esp32:esp32:esp32:PSRAM=disabled,DebugLevel=none'),
 'nano':('Nano/SmartVest_INA226_Nano','arduino:avr:nano:cpu=atmega328old'),
 'front':('Main_and_Cameras/SmartVest_SJKS01_Camera','esp32:esp32:esp32cam:DebugLevel=none'),
 'rear':('Main_and_Cameras/SmartVest_SJKS01_Camera','esp32:esp32:esp32cam:DebugLevel=none')}
p=argparse.ArgumentParser(description='Compile every release role without uploading or modifying source configuration.')
p.add_argument('--cli',default=shutil.which('arduino-cli'))
p.add_argument('--config',type=Path)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--results-file',type=Path,help='Optional independent report path when building disjoint targets with the same cache')
p.add_argument('--targets',nargs='+',choices=TARGETS,default=list(TARGETS))
p.add_argument('--jobs',type=int,default=1,help='Concurrent compiler jobs; default 1 limits memory use on finalization workstations')
args=p.parse_args()
if args.jobs<1:p.error('--jobs must be positive')
if not args.cli:p.error('Pass --cli with the Arduino CLI executable')
args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
results_file=args.results_file.resolve() if args.results_file else args.output/'build-results.json'
results_file.parent.mkdir(parents=True,exist_ok=True)
records=[]
for key in args.targets:
 relative,fqbn=TARGETS[key]
 source=ROOT/relative
 with tempfile.TemporaryDirectory() as tmp:
  if key in {'front','rear'}:
   copy=Path(tmp)/source.name;shutil.copytree(source,copy)
   config=copy/'CameraConfig.h';text=config.read_text(encoding='utf8')
   text,n=re.subn(r'ROLE\[\] = "(?:front|rear)"',f'ROLE[] = "{key}"',text)
   assert n==1;config.write_text(text,encoding='utf8',newline='\n');source=copy
  inputs={p.relative_to(source).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob("*") if p.is_file() and p.suffix in {".h",".cpp",".ino",".csv"}}
  build=args.output/key
  command=[str(args.cli),'compile','--verbose','--warnings','all','--fqbn',fqbn,'--jobs',str(args.jobs),'--build-path',str(build)]
  if args.config:command+=['--config-file',str(args.config.resolve())]
  command+=[str(source)]
  log=args.output/(key+'.log')
  with log.open('w',encoding='utf8') as stream:
   result=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT)
  text=log.read_text(encoding='utf8');flash=re.search(r'Sketch uses (\d+) bytes .*?Maximum is (\d+) bytes',text);ram=re.search(r'Global variables use (\d+) bytes',text)
  record={'target':key,'exitCode':result.returncode,'fqbn':fqbn,'compiledInputs':inputs}
  assert inputs=={p.relative_to(source).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file() and p.suffix in {'.h','.cpp','.ino','.csv'}}, 'Source changed during build'
  if flash:record.update(programBytes=int(flash[1]),buildLimit=int(flash[2]))
  if ram:record['staticRamBytes']=int(ram[1])
  binary=build/(Path(relative).name+('.ino.hex' if key=='nano' else '.ino.bin'))
  if binary.is_file():record.update(imageBytes=binary.stat().st_size,imageSha256=hashlib.sha256(binary.read_bytes()).hexdigest())
  if key=='main' and result.returncode==0:
   assert (build/'partitions.csv').read_bytes()==(ROOT/relative/'partitions.csv').read_bytes()
   budget=subprocess.run([sys.executable,str(ROOT/'Main_and_Cameras/tools/check_flash_budget.py'),str(build)],capture_output=True,text=True)
   record['flashReserveCheckExitCode']=budget.returncode
   (args.output/'main-budget.log').write_text(budget.stdout+budget.stderr,encoding='utf8')
  records.append(record)
  results_file.write_text(json.dumps(records,indent=2),encoding='utf8')
  print(json.dumps({k:v for k,v in record.items() if k!="compiledInputs"}),flush=True)
  if result.returncode:print(text[-4000:],flush=True)
raise SystemExit(1 if any(r['exitCode'] or r.get('flashReserveCheckExitCode',0) for r in records) else 0)
