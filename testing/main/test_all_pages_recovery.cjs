// Production assets, two independent sessions. Local timings are not RF timings.
const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,login,loaded,riderPaths,adminPaths}=require('./dashboard-test-helpers.cjs');
(async()=>{
 const h=createHarness();prepare(h);const base=await h.start(),browser=await launch();
 const errors=[],metrics=[];
 try{
  const clients=[];
  for(const role of ['rider','admin']){
   const context=await browser.newContext(),page=await context.newPage();clients.push({context,page,role});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('**/api/v1/**',async route=>{if(route.request().method()==='GET')await new Promise(r=>setTimeout(r,120));await route.continue().catch(()=>{});});
   await page.goto(base+(role==='admin'?'/admin':'/'));
   const at=Date.now();await login(page,role);metrics.push({role,loginMs:Date.now()-at});
  }
  for(const {page,role} of clients){
   const paths=role==='admin'?adminPaths:riderPaths;
   for(const destination of paths.slice(1).concat(paths[0])){
    let blocked=true;const held=[];
    const stall=async route=>{if(blocked&&route.request().method()==='GET')held.push(route);else await route.fallback();};
    await page.route('**/api/v1/**',stall);
    const at=Date.now();await page.click('nav a[href="'+destination+'"]');await page.waitForURL(base+destination);
    await page.waitForFunction(()=>{const p=document.getElementById('pageContent');return p?.children.length&&!p.querySelector(':scope > .loading');},{},{timeout:3000});
    metrics.push({role,page:destination,structureMs:Date.now()-at});
    assert(await page.locator('#workspace').isVisible());
    // Rendering does not depend on a first data reply. Navigation remains usable.
    await page.waitForTimeout(120);blocked=false;
    await page.unroute('**/api/v1/**',stall);
    for(const route of held)await route.fulfill({status:503,json:{error:'Response memory unavailable'}}).catch(()=>{});
    await loaded(page);
    assert.equal(await page.locator('#reloadDashboard').count(),0);
   }
  }
  const admin=clients[1].page;
  await admin.click('nav a[href="/admin/cameras"]');await loaded(admin);
  let heldExport;const stallExport=route=>{heldExport=route;};
  await admin.route('**/api/v1/cameras/monitor',stallExport);
  await admin.locator('details[data-key="camera-events"] > summary').click();await admin.click('#exportCameraLogs');
  for(let i=0;i<100&&!heldExport;i++)await admin.waitForTimeout(10);assert(heldExport);
  const at=Date.now();await admin.click('nav a[href="/admin/network"]');await loaded(admin);
  assert(Date.now()-at<2000,'leaving a download must release the request queue');
  await admin.unroute('**/api/v1/cameras/monitor',stallExport);await heldExport.abort().catch(()=>{});
  assert.equal(h.state.requests.filter(r=>r.method==='POST').length,2,'read recovery must not replay writes');
  assert.deepEqual(errors,[]);console.log(JSON.stringify(metrics));
  console.log('PASS all 19 routes render before first telemetry, recover from initial 503, and retain responsive navigation; stalled export cancels; two sessions; no JS errors or repeated writes');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
