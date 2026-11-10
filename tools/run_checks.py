from pathlib import Path
import argparse,json,os,runpy,shutil,signal,subprocess,sys,time

ROOT=Path(__file__).resolve().parents[1]
sys.dont_write_bytecode=True
os.environ.setdefault('PYTHONDONTWRITEBYTECODE','1')
p=argparse.ArgumentParser(description='Run software checks without contacting hardware.')
p.add_argument('--zig',type=Path)
p.add_argument('--browser',action='store_true')
p.add_argument('--host',action='store_true')
p.add_argument('--media',action='store_true',help='Require FFmpeg/ffprobe playback in the camera writer suite')
p.add_argument('--sockets',action='store_true',help='Run the optional real-socket suite on POSIX')
p.add_argument('--only',nargs='+',help='Specific project-relative test paths')
p.add_argument('--list',action='store_true',help='List selected and optional checks without running them')
p.add_argument('--timeout',type=float,help='Override each check timeout; defaults to 180 s, or 900 s for the pin matrix')
p.add_argument('--output',type=Path,default=ROOT/'testing/results')
p.add_argument('--python-test',type=Path,help=argparse.SUPPRESS)
args=p.parse_args()
if args.timeout is not None and args.timeout<=0:p.error('--timeout must be positive')

# Each Python check gets its own process, including its host compiler adapter.
# This bounds execution and prevents runpy globals from leaking between suites.
if args.python_test:
 target=args.python_test.resolve()
 if not target.is_relative_to(ROOT) or not target.is_file():p.error('Test path must be an existing project file')
 if args.zig:
  from host_adapter import install
  install(args.zig,ROOT)
 sys.argv=[str(target)]
 runpy.run_path(str(target),run_name='__main__')
 raise SystemExit(0)

checks=['tools/sync_shared.py','testing/main/test_web_assets.py','testing/main/test_camera_pairing_keys.py','testing/main/test_imu_data.cjs','testing/main/test_camera_recovery.cjs','testing/main/test_ride_backpressure.cjs','testing/system/test_power_ui.mjs']
if args.host:
 for directory in ['testing/system','testing/main']:
  checks += [file.relative_to(ROOT).as_posix() for file in sorted((ROOT/directory).glob('test_*.py')) if file.name!='test_socket_transport.py']
if args.browser:
 checks += [file.relative_to(ROOT).as_posix() for file in sorted((ROOT/'testing/main').glob('test_*.cjs'))]
if args.media:checks.append('testing/main/test_camera.py')
if args.sockets:checks.append('testing/system/test_socket_transport.py')
if args.only:checks=args.only
checks=list(dict.fromkeys(checks))
inventory={'selected':checks,'optional':[
 {'path':'testing/main/test_camera.py','scope':'Writer/window checks run with --host; external playback also requires FFmpeg and ffprobe. Use --media to require them.'},
 {'path':'testing/system/test_socket_transport.py','scope':'Actual socket admission, stalled writers and mixed clients; POSIX only, selected with --sockets.'}],
 'timeouts':{'defaultSeconds':180,'pinProfileSeconds':900,'overrideSeconds':args.timeout}}
if args.list:
 print(json.dumps(inventory,indent=2));raise SystemExit(0)
if args.browser and (not os.environ.get('SJKS_PLAYWRIGHT') or not os.environ.get('SJKS_CHROMIUM_BINARY')):
 p.error('Set SJKS_PLAYWRIGHT and SJKS_CHROMIUM_BINARY for browser checks')
if args.media and not all(shutil.which(tool) for tool in ['ffmpeg','ffprobe']):p.error('--media requires ffmpeg and ffprobe on PATH')
if args.sockets and os.name!='posix':p.error('--sockets requires a POSIX host')
args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
os.environ['SJKS_TEST_OUTPUT']=str(args.output)
os.environ.setdefault('PYTHONUTF8','1')
(args.output/'inventory.json').write_text(json.dumps(inventory,indent=2),encoding='utf8')
def run_check(command,stream,timeout):
 child=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,start_new_session=os.name=='posix')
 try:return child.wait(timeout=timeout)
 except subprocess.TimeoutExpired:
  try:
   if os.name=='nt':
    cleanup=subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=stream,stderr=subprocess.STDOUT,timeout=15)
    if cleanup.returncode:stream.write('Process-tree cleanup returned '+str(cleanup.returncode)+'\n')
   else:os.killpg(child.pid,signal.SIGKILL)
  except (OSError,subprocess.TimeoutExpired) as error:stream.write('Process-tree cleanup failed: '+str(error)+'\n')
  finally:
   if child.poll() is None:child.kill()
   child.wait()
  stream.write(f'TIMEOUT after {timeout:g} seconds\n')
  return 124

results=[]
for check in checks:
 start=time.monotonic();path=ROOT/check
 log=args.output/(check.replace('/','__')+'.txt')
 timeout=args.timeout if args.timeout is not None else 900 if path.name=='test_pin_profile.py' else 180
 if path.suffix=='.py':
  command=[sys.executable,str(Path(__file__).resolve()),'--python-test',str(path)]
  if args.zig:command+=['--zig',str(args.zig.resolve())]
 else:command=['node',str(path)]
 with log.open('w',encoding='utf8') as stream:
  try:code=run_check(command,stream,timeout)
  except OSError as error:code=127;stream.write(str(error)+'\n')
 result={'test':check,'exitCode':code,'seconds':round(time.monotonic()-start,2)}
 results.append(result);print(json.dumps(result),flush=True)
 (args.output/'results.json').write_text(json.dumps(results,indent=2),encoding='utf8')
raise SystemExit(1 if any(result['exitCode'] for result in results) else 0)
