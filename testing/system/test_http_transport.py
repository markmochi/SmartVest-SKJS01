"""Production bounded HTTP transport with exact pinned SDK compatibility helpers.

The inherited request parser is deliberately never called. Platform networking
is a consuming byte-queue model; routing/header/argument/response functions are
read from the pinned SDK, not rewritten as a synthetic dispatcher.
"""
from pathlib import Path
import json,os,subprocess,tempfile,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from http_core import prepare
r=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as t:
 folder=Path(t);out=folder/'http_transport'
 core=prepare(r,folder)
 results=Path(os.environ.get('SJKS_TEST_OUTPUT',r/'testing/results/http_transport'))
 results.mkdir(parents=True,exist_ok=True)
 (results/'http-core-source.json').write_text(json.dumps(core,indent=2),encoding='utf8')
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG','-I'+str(r/'testing/system/http_model'),'-I'+str(folder),'-I'+str(r/'testing/system/platform'),str(r/'testing/system/test_http_transport.cpp'),'-lcrypto','-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
