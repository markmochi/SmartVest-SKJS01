"""Validate the actual flash gzip payloads, route aliases and bounded storage.

Run after tools/build_ui.py. No Arduino hardware or network is required.
"""
from pathlib import Path
import gzip
import hashlib
import json
import re

root = Path(__file__).resolve().parents[2]/'Main_and_Cameras'
manifest = json.loads((root / 'ui/manifest.json').read_text(encoding='utf8'))
assert manifest['minifier'] == {'name':'terser','version':'5.48.0','settings':{'compress':{'passes':2,'unsafe':False},'mangle':{'properties':False},'module':True,'format':{'comments':False}}}
assert manifest['gzip']=={'name':'zopfli','version':'0.2.3.post1','iterations':100}
assert (root/'tools/ui-build/requirements.txt').read_text(encoding='utf8').strip()=='zopfli==0.2.3.post1'
javascript = [asset for asset in manifest['assets'] if asset['path'].endswith('.js')]
assert {asset['inputSource'] for asset in javascript} == {p.relative_to(root).as_posix() for p in (root/'ui').rglob('*.js') if 'pages' not in p.parts and p.name!='bootstrap.js'}
assert {asset['servedSource'] for asset in javascript} == {p.relative_to(root).as_posix() for p in (root/'ui-built').rglob('*.js') if p.name!='bootstrap.js'}
package = json.loads((root/'tools/ui-build/package.json').read_text(encoding='utf8'))
lock = json.loads((root/'tools/ui-build/package-lock.json').read_text(encoding='utf8'))
assert package['private'] is True and package['devDependencies'] == {'terser':'5.48.0','esbuild':'0.28.2'}
assert lock['packages']['node_modules/terser']['version'] == '5.48.0'
assert all(item.get('integrity') and item.get('dev') is True for key,item in lock['packages'].items() if key)
header = (root / 'SmartVest_SJKS01_Main/src/web/generated/WebAssets.h').read_text(encoding='utf8')
payloads = {
    int(index): bytes(int(byte, 16) for byte in re.findall(r'0x([0-9a-f]{2})', body))
    for index, body in re.findall(r'WEB_ASSET_(\d+)\[\] PROGMEM = \{(.*?)\};', header, re.S)
}
prefix=re.search(r'WEB_ASSET_PREFIX\[\] = "([^"]+)";',header)[1]
assert prefix=='/assets/'+manifest['version']+'/'
types=json.loads('['+re.search(r'WEB_ASSET_TYPES\[\] = \{(.*?)\};',header)[1]+']')
routes = [(path if path.startswith('/') else prefix+path,types[int(mime)],index,size_index)
          for path,index,size_index,mime in re.findall(r'\{"([^"]+)",WEB_ASSET_(\d+),sizeof\(WEB_ASSET_(\d+)\),(\d+)\}', header)]
assert len(routes) == len(manifest['assets'])
assert len({path for path, *_ in routes}) == len(routes)
for (path, mime, index, size_index), asset in zip(routes, manifest['assets']):
    assert path == asset['path'] and index == size_index
    assert int(index) == asset['payload']
    packed = payloads[int(index)]
    raw = gzip.decompress(packed)
    assert len(packed) == asset['gzipBytes'] and len(raw) == asset['sourceBytes']
    assert hashlib.sha256(raw).hexdigest() == asset['sha256']
    source = (root / asset['servedSource']).resolve()
    input_source = (root / asset['inputSource']).resolve()
    assert source.is_relative_to(root) and input_source.is_relative_to(root), path
    assert hashlib.sha256(input_source.read_bytes()).hexdigest() == asset['inputSha256'], path
    if path.startswith('/assets/'):
        relative = path.split('/', 3)[3]
        assert asset['inputSource'] == 'ui/' + relative, path
        assert asset['servedSource'] == ('ui-built/' if relative.endswith('.js') else 'ui/') + relative, path
    else:
        area = 'developer' if path.startswith('/admin') else 'rider'
        page = path.rsplit('/', 1)[-1] if path not in ('/', '/admin') else 'overview'
        assert asset['servedSource'] == asset['inputSource'] == f'ui/pages/{area}-{page}.html', path
    assert raw == source.read_bytes(), path

html = [asset for asset in manifest['assets'] if not asset['path'].startswith('/assets/')]
assert len(html) == 20
assert len({asset['payload'] for asset in html}) == 1
shell=gzip.decompress(payloads[html[0]['payload']]).decode('utf8')
assert 'data-area="auto"' in shell
links=re.findall(r'<a href="([^"]+)" data-page="([^"]+)"',shell)
assert {route for route,_ in links} == {asset['path'] for asset in html}
assert len(links)==20
assert '<style>' in shell and '<script data-assets=' in shell
assert 'rel="stylesheet"' not in shell and 'type="module" src=' not in shell
import base64
inline_script=re.search(r'<script data-assets="[^"]+">(.*?)</script>',shell,re.S)[1]
inline_style=re.search(r'<style>(.*?)</style>',shell,re.S)[1]
for text in [inline_script,inline_style]:
    digest=base64.b64encode(hashlib.sha256(text.encode()).digest()).decode()
    assert "'sha256-"+digest+"'" in manifest['contentSecurityPolicy']
assert "'unsafe-inline'" not in manifest['contentSecurityPolicy']
assert manifest['bundler']=={'name':'esbuild','version':'0.28.2'}
assert 'id="startupReload"' in shell and 'id="appearanceToggle"' not in shell
assert 'Built around' not in shell and 'auth-art' not in shell
assert sum(map(len, payloads.values())) == manifest['storedGzipBytes']
assert manifest['storedGzipBytes'] <= 100 * 1024, 'Embedded UI flash budget exceeded'
print(f"PASS all {len(routes)} actual flash assets round-trip byte-for-byte; 20 routes share one layout, with navigation selected before binding")
print(f"PASS embedded gzip storage: {manifest['storedGzipBytes']:,} bytes (100 KiB budget; firmware reserve checked separately)")
