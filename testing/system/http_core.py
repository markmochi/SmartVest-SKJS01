"""Select exact pinned WebServer helpers for the transport host integration.

SmartVest owns the bounded parser. These helpers exercise its real core-facing
argument/header/dispatch contract, replacing only platform/socket/String classes.
"""
from pathlib import Path
import hashlib
import os
import re

def method(source, signature):
    start=source.index(signature)
    body=source.index('{',start)
    depth=0
    for token in re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27|[{}]',source[body:],re.S):
        value=token.group()
        if value=='{':depth+=1
        elif value=='}':
            depth-=1
            if not depth:return source[start:body+token.end()]
    raise AssertionError('Unclosed SDK method: '+signature)

def prepare(root, folder):
    configured=os.environ.get('SJKS_ESP32_CORE')
    assert configured,'Set SJKS_ESP32_CORE to the installed pinned ESP32 core 3.3.12 folder'
    core=Path(configured).resolve()
    sdk=core/'libraries/WebServer/src'
    assert (sdk/'WebServer.cpp').is_file(),'Set SJKS_ESP32_CORE to the pinned ESP32 core 3.3.12 folder'
    server=(sdk/'WebServer.cpp').read_text(encoding='utf8')
    parsing=(sdk/'Parsing.cpp').read_text(encoding='utf8')
    signatures=[
        'String WebServer::arg(const String &name) const',
        'String WebServer::arg(int i) const',
        'String WebServer::argName(int i) const',
        'int WebServer::args() const',
        'bool WebServer::hasArg(const String &name) const',
        'String WebServer::header(const String &name) const',
        'String WebServer::header(int i) const',
        'String WebServer::headerName(int i) const',
        'bool WebServer::hasHeader(const String &name) const',
        'int WebServer::headers() const',
        'String WebServer::hostHeader() const',
        'void WebServer::collectHeaders(const char *headerKeys[], const size_t headerKeysCount)',
        'void WebServer::collectAllHeaders()',
        'void WebServer::_clearRequestHeaders()',
        'void WebServer::_clearResponseHeaders()',
        'void WebServer::sendHeader(const String &name, const String &value, bool first)',
        'void WebServer::_prepareHeader(String &response, int code, const char *content_type, size_t contentLength)',
        'const String WebServer::version() const',
        'String WebServer::responseCodeToString(int code)',
        'bool WebServer::_handleRequest()',
        'void WebServer::_finalizeResponse()',
    ]
    body='\n\n'.join(method(server,item) for item in signatures)
    body+='\n\n'+method(parsing,'bool WebServer::_collectHeader(const char *headerName, const char *headerValue)')
    body+='\n\n'+method(parsing,'void WebServer::_parseArguments(const String &data)')
    body+='\n\n'+method(parsing,'String WebServer::urlDecode(const String &text)')
    (folder/'core_methods.inc').write_text(body,encoding='utf8')
    return {'core':str(core),'WebServer.cpp':hashlib.sha256((sdk/'WebServer.cpp').read_bytes()).hexdigest(),'Parsing.cpp':hashlib.sha256((sdk/'Parsing.cpp').read_bytes()).hexdigest(),'helpersSha256':hashlib.sha256(body.encode()).hexdigest()}
