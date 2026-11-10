// Actual delivered modules with delayed local APIs. This is not an RF benchmark.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {createHarness}=require('./preview_server.cjs'),pw=require(process.env.SJKS_PLAYWRIGHT);
const {testOutputDir}=require('../paths.cjs').suite(__filename);
(async()=>{
 const h=createHarness(),base=await h.start(),browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader']});
 const contexts=[],errors=[],metrics={fixtureGetDelayMs:120};
 try{
  const pages=[];
  for(const role of ['rider','developer']){
   const context=await browser.newContext({viewport:{width:1440,height:1000}});contexts.push(context);
   const page=await context.newPage();pages.push(page);page.on('pageerror',error=>errors.push(error.message));
   await page.route('**/api/v1/**',async route=>{if(route.request().method()==='GET')await new Promise(resolve=>setTimeout(resolve,120));try{await route.continue();}catch{}});
   await page.goto(base+(role==='developer'?'/admin':'/'));
   await page.fill('#authUser',role==='developer'?'developer':'admin');await page.fill('#authPass',role==='developer'?'developer-test-password':'admin123');
   const started=Date.now();await page.click('#loginSubmit');
   await page.locator(role==='developer'?'.module-table':'[data-component="location"] .coordinates').first().waitFor();
   await page.waitForFunction(()=>document.getElementById('connection').classList.contains('live'));
   metrics[role+'FirstDataMs']=Date.now()-started;assert(metrics[role+'FirstDataMs']<5000);
  }
  const [rider,admin]=pages;
  for(const page of pages)await page.evaluate(()=>{
   window.refreshDims=0;window.refreshNode=document.getElementById('refreshPage');
   window.refreshObserver=new MutationObserver(()=>++window.refreshDims);
   window.refreshObserver.observe(window.refreshNode,{attributes:true,attributeFilter:['disabled']});
  });
  await rider.waitForTimeout(4500);
  for(const page of pages){
   assert.equal(await page.evaluate(()=>window.refreshDims),0,'background reads must not repeatedly dim Refresh');
   assert(await page.evaluate(()=>window.refreshNode===document.getElementById('refreshPage')));
   await page.evaluate(()=>window.refreshObserver.disconnect());
  }
  console.log('PASS rider and developer background polling preserves Refresh brightness and node identity');
  assert(h.state.requests.some(r=>r.path==='/api/v1/rider/status'&&r.query==='?view=overview'));
  const writes=h.state.requests.filter(r=>r.method==='POST').length;
  let held=null;const hold=route=>{held=route;};await rider.route('**/api/v1/rider/pose',hold);
  for(let i=0;i<150&&!held;i++)await rider.waitForTimeout(20);assert(held,'pose request must be running');
  const before=h.state.requests.filter(r=>r.path==='/api/v1/rider/status').length,started=Date.now();
  await rider.click('#refreshPage');
  for(let i=0;i<60&&h.state.requests.filter(r=>r.path==='/api/v1/rider/status').length===before;i++)await rider.waitForTimeout(20);
  metrics.foregroundAfterHeldPreviewMs=Date.now()-started;
  assert(h.state.requests.filter(r=>r.path==='/api/v1/rider/status').length>before);
  assert(metrics.foregroundAfterHeldPreviewMs<1200,'foreground read must not wait for the 1500 ms preview timeout');
  await rider.unroute('**/api/v1/rider/pose',hold);await held.abort().catch(()=>{});
  console.log('PASS two dashboard clients with delayed GETs: initial data renders, scoped rider data, held pose yields immediately to foreground refresh');
  let attempts=0;const recover=async route=>{if(++attempts===1)await route.fulfill({status:503,contentType:'application/json',body:'{"error":"Response memory unavailable"}'});else await route.continue();};
  await admin.route('**/api/v1/status?view=overview',recover);
  const recovery=Date.now();await admin.reload();await admin.locator('.module-table').waitFor();
  await admin.waitForFunction(()=>document.getElementById('connection').classList.contains('live'));
  metrics.transient503RecoveryMs=Date.now()-recovery;assert(attempts>=2);assert(metrics.transient503RecoveryMs<3000,'transient 503 should recover without the old 4-second backoff');
  assert.equal(h.state.requests.filter(r=>r.method==='POST').length,writes);
  await admin.unroute('**/api/v1/status?view=overview',recover);
  console.log('PASS first-read 503 recovers promptly without reload loops, repeated writes or a permanent loading placeholder');
  // Preserve actual negative readiness after recovery; optimization cannot invent health.
  h.state.status.sensing.online=false;h.state.status.sensing.armed=false;h.state.status.crashDetection=false;
  await rider.click('#refreshPage');await rider.locator('.ride-status h2').filter({hasText:'Your vest needs attention'}).waitFor();
  assert.deepEqual(errors,[]);fs.mkdirSync(testOutputDir(''),{recursive:true});
  fs.writeFileSync(path.join(testOutputDir(''),'metrics.json'),JSON.stringify(metrics,null,2));
  console.log('PASS genuine unavailable monitoring remains visible after recovery; no browser errors; timings describe only the local delayed fixture');
 }finally{for(const context of contexts)await context.close();await browser.close();await h.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
