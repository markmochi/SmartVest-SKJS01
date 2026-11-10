"""Run production diagnostic handlers with full rings, cursor races and limited heap."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'system'))
from http_core import method as source_method
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras';main=root/'SmartVest_SJKS01_Main'
source=(main/'src/web/DashboardServer.cpp').read_text(encoding='utf8')
def method(name):
    start=source.index('void DashboardServer::'+name+'(')
    body=source.index('{',start);depth=0
    for token in re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27|[{}]',source[body:],re.S):
        text=token.group()
        if text=='{':depth+=1
        elif text=='}':
            depth-=1
            if not depth:return source[start:body+token.end()]
    raise AssertionError('Unclosed method: '+name)
types=(main/'src/gsm/SIM800LService.h').read_text(encoding='utf8')
types=types[types.index('  static constexpr size_t TRACE_CAPACITY'):types.index('  uint32_t responseAgeMs()')]
with tempfile.TemporaryDirectory() as temporary:
    folder=Path(temporary);host=folder/'host';shutil.copytree(root/'../testing/main/host',host)
    p=host/'Arduino.h';text=p.read_text().replace('#pragma once','#pragma once\nbool allowReserve(size_t);',1)
    # Include the live JSON tree in the output reservation budget.
    text=text.replace('bool allowReserve(size_t);','')
    text=text.replace('class String {','bool allowReserve(size_t);\nclass String {')
    text=text.replace('bool reserve(size_t n){v.reserve(n);return true;}','bool reserve(size_t n){if(!allowReserve(n))return false;v.reserve(n);return true;}')
    p.write_text(text,encoding='utf8')
    (folder/'service_types.inc').write_text(types,encoding='utf8')
    (folder/'handlers.inc').write_text(source_method(source,'template <typename Camera>')+'\n'+'\n'.join(method(n) for n in ['sendJson','sendError','sendCameraMonitor','sendModemTrace'])+'\n'+source_method(source,'void DashboardServer::sendError(int statusCode,const char* message)'),encoding='utf8')
    binary=folder/'test'
    command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG','-I'+str(host),'-I'+os.environ['SJKS_ARDUINOJSON'],'-I'+str(folder),'-I'+str(main/'src/web'),'-I'+str(main),'-I'+str(root.parent/'common'),'-I'+str(root.parent/'testing/system/platform'),str(root/'../testing/main/host/diagnostic_pages.cpp'),'-lcrypto','-o',str(binary)]
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
