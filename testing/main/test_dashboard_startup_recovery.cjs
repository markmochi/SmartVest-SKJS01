// Transient local asset/API failures; no physical Wi-Fi or hardware claim.
const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,login,loaded,riderPaths,adminPaths}=require('./dashboard-test-helpers.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
(async()=>{const h=createHarness();prepare(h);const base=await h.start(),browser=await launch();
try{
 for(const role of ['rider','admin']){
  const context=await browser.newContext({viewport:{width:390,height:844}}),page=await context.newPage(),errors=[];
  let documents=0,css=0;const attempts=new Map(),apiFailures=new Map();
  page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(r.resourceType()==='document')documents++;if(r.url().endsWith('.css'))css++;});
  const interrupted=new Set(['shared/runtime.js','shared/boot.js','shared/runtime-ref.js','shared/views.js',role==='rider'?'rider/app.js':'developer/app.js','shared/contacts.js','shared/account.js','developer/sensing.js','developer/tools.js','developer/cameras.js','developer/ride-test.js']);
  await page.route('**/assets/**',async route=>{
   const key=new URL(route.request().url()).pathname.split('/').slice(-2).join('/');
   if(interrupted.has(key)){
    const count=(attempts.get(key)||0)+1;attempts.set(key,count);
    if(count===1){await route.fulfill({status:503,contentType:'application/json',body:'{"error":"busy"}'});return;}
   }
   await route.continue();
  });
  await page.route('**/api/v1/**',async route=>{
   const key=new URL(route.request().url()).pathname;
   if(route.request().method()==='GET'&&['/api/v1/session','/api/v1/settings','/api/v1/rider/settings','/api/v1/rider/account','/api/v1/admin/account'].includes(key)){
    const count=(apiFailures.get(key)||0)+1;apiFailures.set(key,count);
    if(count===1){await route.fulfill({status:503,json:{error:'Response memory unavailable'}});return;}
    if(count===2){await route.fulfill({status:200,contentType:'application/json',body:'{"interrupted":'});return;}
   }
   await route.continue();
  });
  await page.goto(base+(role==='admin'?'/admin':'/'),{waitUntil:'domcontentloaded'});
  assert.equal(await page.evaluate(()=>getComputedStyle(document.body).margin),'0px','HTML must arrive with its styles even while scripts fail');
  await page.locator('#authWrap').waitFor({state:'visible',timeout:30000});
  assert.equal(css,0,'no separate stylesheet download can strip the login page');
  await page.evaluate(()=>window.originalRuntime=window.smartvestRuntime);
  const before=h.state.requests.filter(r=>r.method==='POST').length;
  await login(page,role);
  const appKey=role==='rider'?'rider/app.js':'developer/app.js',appReads=attempts.get(appKey);
  for(const url of role==='admin'?adminPaths:riderPaths){
   if(new URL(page.url()).pathname!==url){const link=page.locator(`nav a[href="${url}"]`).first();if(!await link.isVisible())await page.click('#mobileMenu');await link.click();await loaded(page);}
   assert.equal(await page.locator('#reloadDashboard').count(),0,url+' must recover without a manual reload screen');
   if(url.endsWith('/contacts'))await page.waitForFunction(()=>!document.getElementById('riderName').disabled);
   if(url.endsWith('/account'))await page.waitForFunction(()=>!document.getElementById('accountUsername').disabled);
  }
  assert.equal(documents,1,'cold startup and every same-role page must need no reload');
  assert.equal(attempts.get(appKey),appReads,'a successfully recovered app is reused on every later page');
  assert(await page.evaluate(()=>window.originalRuntime===window.smartvestRuntime));
  const shared=await page.evaluate(async()=>{
   const prefix=document.querySelector('script[type="module"]').src.replace(/shared\/boot.js$/,'');const module=await import(prefix+'shared/api.js');
   return module.session===window.smartvestRuntime.session&&module.session.logged;
  });assert(shared,'retry namespaces must share one authenticated session');
  assert.equal(h.state.requests.filter(r=>r.method==='POST').length,before+1,'only the explicit login may write');
  assert.deepEqual(errors,[]);
  for(const key of ['shared/runtime.js','shared/boot.js','shared/runtime-ref.js','shared/views.js',role==='rider'?'rider/app.js':'developer/app.js'])assert(attempts.get(key)>=2,key+' must actually retry');
  await page.screenshot({path:testOutput(role+'-recovered.png'),fullPage:true});await context.close();
 }
 console.log('PASS both roles: styled cold HTML, transient runtime/entry/dependency/page failures recover automatically, all 19 routes, initial settings read recovery, one document/runtime/session, and no repeated writes');
}finally{await browser.close();await h.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
