#!/usr/bin/env python3

"""Build route documents with shared flash payloads and deterministic gzip assets."""

from pathlib import Path

import base64, gzip, hashlib, html, json, re, shutil, subprocess, zlib
import importlib.metadata
try:
 import zopfli.gzip
 assert importlib.metadata.version("zopfli")=="0.2.3.post1"
except (ImportError,AssertionError):
 raise SystemExit("Install the pinned build-only compressor: python -m pip install -r Main_and_Cameras/tools/ui-build/requirements.txt")

ROOT = Path(__file__).resolve().parents[1]

VERSION = re.search(r'VERSION\[\] = "([^"]+)"', (ROOT/'SmartVest_SJKS01_Main/src/core/Version.h').read_text(encoding='utf8'))[1]

node=shutil.which('node')
if not node: raise SystemExit('Node.js is required to build UI assets. Then run: npm ci --ignore-scripts --prefix Main_and_Cameras/tools/ui-build')
minified=subprocess.run([node,str(ROOT/'tools/minify_ui.cjs')],capture_output=True,text=True,encoding='utf8')
if minified.returncode: raise SystemExit(minified.stderr.strip() or 'JavaScript minification failed; previous embedded assets were preserved.')
minifier=json.loads(minified.stdout)
javascript={item['inputSource']:item for item in minifier['files']}

(ROOT/'ui/shared/favicon.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><rect width="32" height="32" rx="3" fill="#252824"/><path d="M14 9H9v7h5v7H7m11-14 3 14 4-14" fill="none" stroke="#F3EFE5" stroke-width="2.5" stroke-linejoin="miter"/></svg>',encoding='utf8')

fingerprint=hashlib.sha256(json.dumps(minifier,sort_keys=True,separators=(',',':')).encode())
for script in (Path(__file__),ROOT/'tools/minify_ui.cjs'):fingerprint.update(script.read_bytes())
for p in sorted((ROOT/'ui').rglob('*')):
 if not p.is_file() or p.suffix not in {'.js','.css','.svg'}:continue
 relative=p.relative_to(ROOT).as_posix()
 fingerprint.update(relative.encode())
 fingerprint.update((ROOT/javascript[relative]['servedSource']).read_bytes() if p.suffix=='.js' else p.read_bytes())
ASSET_VERSION=VERSION+'-'+fingerprint.hexdigest()[:10]
ASSETS = f'/assets/{ASSET_VERSION}'
INLINE_STYLE='\n'.join((ROOT/'ui/shared'/name).read_text(encoding='utf8') for name in ['styles.css','rider-position.css'])
css_minifier="const esbuild=require(process.argv[1]);let s='';process.stdin.setEncoding('utf8');process.stdin.on('data',c=>s+=c);process.stdin.on('end',()=>process.stdout.write(esbuild.transformSync(s,{loader:'css',minify:true,target:'chrome90'}).code));"
INLINE_STYLE=subprocess.run([node,'-e',css_minifier,str(ROOT/'tools/ui-build/node_modules/esbuild')],input=INLINE_STYLE,capture_output=True,text=True,encoding='utf8',check=True).stdout
INLINE_BOOT=(ROOT/'ui-built/shared/bootstrap.js').read_text(encoding='utf8')
assert '</style' not in INLINE_STYLE.lower() and '</script' not in INLINE_BOOT.lower()
policy="default-src 'self'; script-src 'self' 'sha256-"+base64.b64encode(hashlib.sha256(INLINE_BOOT.encode()).digest()).decode()+"'; style-src 'self' 'sha256-"+base64.b64encode(hashlib.sha256(INLINE_STYLE.encode()).digest()).decode()+"'; img-src 'self' data:; connect-src 'self'; frame-src http://*:8081; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"
policy_file=ROOT/'SmartVest_SJKS01_Main/src/web/generated/WebPolicy.h'
policy_file.write_text('#pragma once\n// Generated hashes authorize only the exact embedded startup code and styles.\nstatic const char WEB_CONTENT_SECURITY_POLICY[] = '+json.dumps(policy)+';\n',encoding='utf8')

RIDER = [('/', 'overview', 'Overview', 'overview'),('/equipment','equipment','Equipment','equipment'),('/cameras','cameras','Cameras','cameras'),('/contacts','contacts','Contacts','contacts'),('/activity','activity','Activity','activity'),('/account','account','Account','account')]

DEVELOPER = [('/admin','overview','Overview','overview'),('/admin/gsm','gsm','Cellular / SMS','gsm'),('/admin/gps','gps','GPS location','gps'),('/admin/sensing','sensing','Sensing board','sensing'),('/admin/power','power','Power monitor','power'),('/admin/cameras','cameras','Cameras','cameras'),('/admin/network','network','Wi-Fi network','network'),('/admin/ride-test','ride-test','Ride Test','activity'),('/admin/system','system','Main ESP32','system'),('/admin/contacts','contacts','Contacts','contacts'),('/admin/activity','activity','Message history','activity'),('/admin/incidents','incidents','Incident records','incidents'),('/admin/tools','tools','Pairing / SMS Test','tools'),('/admin/account','account','Account','account')]

SVG_PATHS = {'overview':'M3 10l7-7 7 7v8H5v-8 M8 18v-6h4v6','equipment':'M6 3L2 7l3 4v7h10v-7l3-4-4-4-4 3-4-3z M5 13h10 M5 15h10','contacts':'M2 18v-2a6 6 0 0112 0v2 M15 3a3 3 0 010 6 M16 12a5 5 0 013 4v2','activity':'M3 4h14v12H9l-4 3v-3H3z M6 8h8 M6 11h5','account':'M3 19v-2a7 7 0 0114 0v2','gsm':'M3 18v-3 M7 18v-7 M11 18V7 M15 18V3','gps':'M10 1v4 M10 15v4 M1 10h4 M15 10h4','sensing':'M1 10h4l2-6 4 13 3-7h5','power':'M17 8h2v5h-2 M6 8v5 M10 8v5 M13 8v5','cameras':'M14 9l5-3v10l-5-3z','network':'M2 7a12 12 0 0116 0 M5 11a7 7 0 0110 0 M8 15a3 3 0 014 0','system':'M7 1v3 M13 1v3 M7 16v3 M13 16v3 M1 7h3 M1 13h3 M16 7h3 M16 13h3','tools':'M13 2a5 5 0 00-5 6L2 14a2.5 2.5 0 004 4l6-6a5 5 0 006-5l-3 3-4-4 3-3z','incidents':'M6 3h8v3h3v13H3V6h3z M7 10h6 M7 14h4','refresh':'M17 9a7 7 0 10-1 6 M17 3v6h-6','menu':'M3 5h14 M3 10h14 M3 15h14'}

def icon(name):

 extra = {'contacts':'<circle cx="8" cy="6" r="3"/>','account':'<circle cx="10" cy="7" r="3"/>','gps':'<circle cx="10" cy="10" r="6"/><circle cx="10" cy="10" r="2"/>','power':'<rect x="2" y="5" width="14" height="11" rx="2"/>','cameras':'<rect x="2" y="5" width="12" height="12" rx="2"/><circle cx="8" cy="11" r="3"/>','system':'<rect x="4" y="4" width="12" height="12" rx="1"/><rect x="7" y="7" width="6" height="6"/>'}.get(name,'')

 return f'<svg class="icon" viewBox="0 0 20 20" aria-hidden="true" focusable="false"><path d="{SVG_PATHS[name]}"/>{extra}</svg>'

def brand(): return '<span class="brand-mark" aria-hidden="true">SV</span><span><span class="brand-name">SmartVest</span><span class="brand-model">SJKS-01</span></span>'


def link(item,current):

 path,page,name,symbol=item

 return f'<a href="{path}" data-page="{page}"'+(' aria-current="page"' if page==current else '')+f'>{icon(symbol)}<span>{html.escape(name)}</span></a>'

def build_page(item,developer):

 path,page,title,symbol=item;area='developer' if developer else 'rider'

 if developer:

  nav='<nav class="rider-nav" aria-label="Pages">'+''.join(link(x,page) for x in RIDER)+'</nav>'+'<nav class="side-nav" id="sideNav" aria-label="Pages"><p class="nav-group">Device</p>'+link(DEVELOPER[0],page)+'<p class="nav-group">Modules</p>'+''.join(link(x,page) for x in DEVELOPER[1:7])+'<p class="nav-group">Settings & records</p>'+''.join(link(x,page) for x in DEVELOPER[7:])+'</nav>'

 else:nav='<nav class="rider-nav" aria-label="Pages">'+''.join(link(x,page) for x in RIDER)+'</nav>'

 menu='<button id="mobileMenu" class="mobile-menu" aria-label="Pages" aria-controls="sideNav" aria-expanded="false" hidden>'+icon('menu')+'<span>Pages</span></button>' if developer else ''

 battery_slot='<div id="batterySlot" hidden></div>'

 header=f'<header class="topbar"><div class="topbar-inner"><a class="brand" href="/" aria-label="SmartVest home">{brand()}</a><div class="topbar-actions">{menu}{battery_slot}<span class="connection" id="connection" hidden>Connected</span><button id="pageJump" aria-label="Find a page" aria-haspopup="dialog" aria-controls="pageSearch" hidden><svg class="icon" viewBox="0 0 20 20" aria-hidden="true"><circle cx="8.5" cy="8.5" r="5.5"/><path d="m13 13 4 4"/></svg><span>Find a page</span><kbd aria-hidden="true">Ctrl K</kbd></button><button id="logout" hidden>Sign out</button></div></div></header>'

 auth=f'''<div id="authBoot" class="auth-boot" role="status"><p id="startupMessage">Connecting to your vest...</p><p id="startupRecovery"><a class="button secondary" href="" id="startupReload">Reload dashboard</a></p></div><main id="authWrap" class="auth-wrap" tabindex="-1" hidden><div class="auth-split"><aside class="auth-identity"><div><h2 class="auth-wordmark">SmartVest</h2><p class="auth-model">SJKS-01</p></div><ul class="auth-capabilities"><li><span>Crash monitoring</span><span>01</span></li><li><span>Camera recording</span><span>02</span></li><li><span>Emergency SMS</span><span>03</span></li></ul><p class="auth-connection-note">Local device access</p></aside><section class="auth-card"><h1>Sign in</h1><p class="auth-intro" id="consoleLabel">Rider dashboard</p><form id="loginForm"><div class="field"><label for="authUser">Username</label><input id="authUser" name="username" autocomplete="username" autocapitalize="none" spellcheck="false" required maxlength="32"></div><div class="field"><label for="authPass">Password</label><div class="password-field"><input id="authPass" name="password" type="password" autocomplete="current-password" required maxlength="64"><button type="button" id="loginReveal" data-reveal="authPass" aria-controls="authPass" aria-pressed="false">Show</button></div></div><button class="submit" id="loginSubmit" type="submit">Sign in</button><p id="loginMessage" class="form-message" role="alert" aria-live="polite"></p></form><p class="auth-footnote">Connect this device and your vest to the same Wi-Fi network.</p></section></div></main>'''


 body=f'''<div id="workspace" hidden>{'<div class="dev-layout">' if developer else ''}{nav}<main id="main" class="main" tabindex="-1"><div id="connectionBanner" class="notice warn" role="status" hidden><p id="connectionDetail">Connection lost. Readings are from the last update.</p></div><div class="page-head"><div><h1 id="pageTitle">{html.escape(title)}</h1><span id="mobileConnection" class="connection mobile-connection">Waiting for vest</span></div><div class="head-tools"><span id="updateIndicator" class="update-indicator" role="status" hidden>Updating…</span><span id="receivedTime" class="refresh-time">Waiting for data</span><button id="refreshPage" class="secondary">{icon('refresh')}<span>Refresh</span></button></div></div><div id="pageContent" class="page-content"><div class="loading"><h2>Loading…</h2></div></div><footer class="page-footer"><span>SmartVest SJKS-01</span></footer></main>{'</div>' if developer else ''}</div>'''

 search='<dialog id="pageSearch" class="dialog page-search" aria-labelledby="pageSearchTitle"><div class="page-search-head"><h2 id="pageSearchTitle">Find a page</h2><button id="pageSearchClose" aria-label="Close page search">×</button></div><div class="page-search-field"><label class="visually-hidden" for="pageSearchInput">Search pages</label><input id="pageSearchInput" type="search" placeholder="Where would you like to go?" autocomplete="off" role="combobox" aria-controls="pageSearchResults" aria-autocomplete="list" aria-expanded="false"></div><div id="pageSearchResults" class="page-search-results" role="listbox" aria-label="Pages"></div><div class="page-search-footer"><p>↑ ↓ to choose · Enter to open · Esc to close</p><button id="viewDensity" class="secondary" aria-pressed="false">Compact spacing</button></div></dialog>'

 return f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><meta name="theme-color" content="#F3EFE5"><title>Sign in | SmartVest</title><style>{INLINE_STYLE}</style><script data-assets="{ASSETS}">{INLINE_BOOT}</script><link rel="icon" type="image/svg+xml" href="{ASSETS}/shared/favicon.svg"></head><body data-area="auto" data-page="{page}"><a class="skip-link" id="skipContent" href="#authWrap">Skip to content</a>{header}<noscript><p class="no-script">Enable JavaScript to sign in.</p></noscript>{auth}{body}<button id="navBackdrop" class="nav-backdrop" aria-label="Close navigation" tabindex="-1" hidden></button><dialog id="confirmDialog" class="dialog" aria-labelledby="confirmTitle" aria-describedby="confirmText"><div class="panel-body"><h2 id="confirmTitle"></h2><p id="confirmText"></p><div class="actions"><button id="confirmNo" class="secondary">Cancel</button><button id="confirmYes">Continue</button></div></div></dialog>{search}<div id="toast" class="toast" role="status" aria-live="polite" hidden></div></body></html>'''


page_dir=ROOT/'ui/pages';page_dir.mkdir(exist_ok=True)

entries=[]

for developer,pages in [(False,RIDER),(True,DEVELOPER)]:

 # One shell per role. boot.js selects the current page from the exact route
 # before revealing the workspace. Same-role navigation reuses this document.
 shell=build_page(('/', '', 'Overview', 'overview'),True)

 for item in pages:

  text=shell

  file=page_dir/(('developer-' if developer else 'rider-')+item[1]+'.html')
  file.write_text(text, encoding='utf8', newline='\n')

  relative=file.relative_to(ROOT).as_posix()
  entries.append((item[0],'text/html; charset=utf-8',text.encode(),relative,relative,hashlib.sha256(text.encode()).hexdigest()))

for p in sorted((ROOT/'ui').rglob('*')):

 if not p.is_file() or 'pages' in p.parts or p.suffix not in ('.css','.js','.svg'): continue
 if p.suffix=='.css' or p.name=='bootstrap.js':continue # Embedded once in the shared HTML shell.

 relative=p.relative_to(ROOT/'ui').as_posix()

 mime={'css':'text/css; charset=utf-8','js':'text/javascript; charset=utf-8','svg':'image/svg+xml'}[p.suffix[1:]]

 input_source=p.relative_to(ROOT).as_posix()
 served_source=javascript[input_source]['servedSource'] if p.suffix=='.js' else input_source
 entries.append((ASSETS+'/'+relative,mime,(ROOT/served_source).read_bytes(),served_source,input_source,hashlib.sha256(p.read_bytes()).hexdigest()))

types=list(dict.fromkeys(item[1] for item in entries))
out=['#pragma once','#include <Arduino.h>','// Generated by tools/build_ui.py. Edit ui/ source files and rebuild.',
     'struct WebAsset { const char* path; const uint8_t* data; uint16_t size; uint8_t type; };',
     f'static const char WEB_ASSET_PREFIX[] = "{ASSETS}/";',
     'static const char* const WEB_ASSET_TYPES[] = {'+','.join(json.dumps(mime) for mime in types)+'};']

manifest=[]
payloads={}
indices=[]
total_packed=0
packed_cache={}

for i,(path,mime,data,served_source,input_source,input_sha256) in enumerate(entries):

 # Select the smallest standard gzip stream at build time. This costs no
 # ESP32 RAM/CPU and changes no HTML/JS content. Every choice round-trips below.
 packed=packed_cache.get(data)
 if packed is None:
  packed=zopfli.gzip.compress(data,numiterations=100)
  packed_cache[data]=packed
 assert gzip.decompress(packed)==data
 assert len(packed)<=65535, 'Asset exceeds compact flash descriptor size'

 # Identical documents share the same PROGMEM array; never expand HTML in RAM.
 index=payloads.get(data)
 if index is None:
  index=i
  payloads[data]=index
  total_packed+=len(packed)
  out.append(f'static const uint8_t WEB_ASSET_{index}[] PROGMEM = {{')

  for n in range(0,len(packed),24): out.append(','.join(f'0x{byte:02x}' for byte in packed[n:n+24])+',')

  out.append('};')
 indices.append(index)

 manifest.append({'path':path,'sourceBytes':len(data),'gzipBytes':len(packed),'payload':index,'sha256':hashlib.sha256(data).hexdigest(),'servedSource':served_source,'inputSource':input_source,'inputSha256':input_sha256})

out.append('static const WebAsset WEB_ASSETS[] = {')

for i,(path,mime,data,*_) in enumerate(entries):
 index=indices[i]
 # Store the common version prefix once. The manifest retains complete URLs.
 stored_path=path[len(ASSETS)+1:] if path.startswith(ASSETS+'/') else path
 out.append(f'{{"{stored_path}",WEB_ASSET_{index},sizeof(WEB_ASSET_{index}),{types.index(mime)}}},')

out.append('};')

dest=ROOT/'SmartVest_SJKS01_Main/src/web/generated/WebAssets.h';dest.parent.mkdir(exist_ok=True);dest.write_text('\n'.join(out)+'\n', encoding='utf8')

(ROOT/'ui/manifest.json').write_text(json.dumps({'version':ASSET_VERSION,'productVersion':VERSION,'minifier':{'name':'terser','version':minifier['version'],'settings':minifier['settings']},'bundler':minifier['bundler'],'contentSecurityPolicy':policy,'gzip':{'name':'zopfli','version':'0.2.3.post1','iterations':100},'storedGzipBytes':total_packed,'assets':manifest},indent=2)+'\n', encoding='utf8')

print(f'Built {len(RIDER)+len(DEVELOPER)} route documents, {len(entries)} assets, {len(payloads)} shared payloads; {total_packed:,} compressed bytes stored.')

