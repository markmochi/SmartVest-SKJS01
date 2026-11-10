// Actual delivered modules: page changes share a document and dispose old work.
const assert=require('node:assert/strict'),{createHarness}=require('./preview_server.cjs');
const {prepare,launch,login,loaded,sensingReady}=require('./dashboard-test-helpers.cjs');
(async()=>{
 const h=createHarness();prepare(h);const base=await h.start(),browser=await launch(),page=await browser.newPage();
 const errors=[],requests=[];page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>requests.push({url:r.url(),type:r.resourceType()}));
 try{
  await page.goto(base+'/admin');await login(page,'admin');
  await page.evaluate(()=>window.navigationIdentity={});const identity=await page.evaluateHandle(()=>window.navigationIdentity);
  const documents=()=>requests.filter(r=>r.type==='document').length;
  const baseline=documents(),sessions=h.state.requests.filter(r=>r.path==='/api/v1/session').length;
  for(const route of ['/admin/sensing','/admin/incidents','/admin/cameras','/admin/network','/admin','/admin/sensing']){
   await page.click('nav a[href="'+route+'"]');await page.waitForURL(base+route);await loaded(page);
   if(route==='/admin/sensing')await sensingReady(page);
  }
  assert.equal(documents(),baseline);assert.equal(h.state.requests.filter(r=>r.path==='/api/v1/session').length,sessions);
  assert(await page.evaluate(object=>window.navigationIdentity===object,identity));
  const poses=h.state.requests.filter(r=>r.path==='/api/v1/rider/pose').length;
  await page.waitForTimeout(1800);assert.equal(h.state.requests.filter(r=>r.path==='/api/v1/rider/pose').length,poses,'Sensing must share its IMU update with the rider view');
  await page.locator('[data-rp-live]').filter({hasText:'Live position'}).waitFor();
  console.log('PASS repeated tab changes reuse document/session/modules; Sensing has one shared telemetry stream');
  let held=null;const hold=route=>{held=route;};await page.route('**/api/v1/sensing/imu',hold);
  for(let n=0;n<100&&!held;n++)await page.waitForTimeout(20);assert(held);
  const started=Date.now();await page.click('nav a[href="/admin/incidents"]');await page.waitForURL(base+'/admin/incidents');await loaded(page);
  assert(Date.now()-started<1500,'new foreground page must not wait for an old held telemetry request');
  await page.unroute('**/api/v1/sensing/imu',hold);await held.abort().catch(()=>{});
  await page.goBack();await page.waitForURL(base+'/admin/sensing');await sensingReady(page);
  await page.goForward();await page.waitForURL(base+'/admin/incidents');await loaded(page);
  assert.equal(documents(),baseline);
  console.log('PASS old GET cancellation, fresh page rendering and Back/Forward without reloading the shell');
  await page.click('nav a[href="/admin/ride-test"]');await page.waitForURL(base+'/admin/ride-test');await page.locator('#rideStart:not([disabled])').waitFor();
  await page.click('#rideStart');await page.locator('#rideMessage').filter({hasText:'Recording'}).waitFor();
  await page.click('nav a[href="/admin/incidents"]');await page.locator('#confirmDialog').waitFor({state:'visible'});await page.click('#confirmNo');assert(page.url().endsWith('/admin/ride-test'));
  await page.click('nav a[href="/admin/incidents"]');await page.locator('#confirmDialog').waitFor({state:'visible'});await page.click('#confirmYes');await page.waitForURL(base+'/admin/incidents');await loaded(page);
  await page.click('nav a[href="/admin/ride-test"]');await page.waitForURL(base+'/admin/ride-test');await page.locator('#rideMessage').filter({hasText:/Ride (interrupted|recording)/}).waitFor();
  assert.equal(documents(),baseline);assert.deepEqual(errors,[]);
  console.log('PASS recording departure confirmation, committed ride recovery and no JavaScript errors');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
