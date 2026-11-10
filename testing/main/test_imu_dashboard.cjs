const {testOutput}=require('../paths.cjs').suite(__filename);
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT||'playwright');
const root=path.resolve(__dirname,'../../Main_and_Cameras');
(async()=>{
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY||'/tmp/smartvest-chromium/chrome-headless-shell',headless:true,args:['--no-sandbox']});
 const harness=createHarness();harness.state.imuLab=require('./imu-fixtures.cjs').fixture();const base=await harness.start(),errors=[],csp=[];
 const context=await browser.newContext({viewport:{width:1440,height:1080},acceptDownloads:true}),page=await context.newPage();
 page.setDefaultTimeout(10000);
 page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error'&&/Content Security Policy|Refused to/.test(m.text()))csp.push(m.text());});
 try{
  await page.goto(base+'/admin/sensing');await page.locator('#authWrap').waitFor({state:'visible'});await page.fill('#authUser','developer');await page.fill('#authPass','developer-test-password');await page.click('#loginSubmit');
  await page.locator('#imuLiveStatus').filter({hasText:'IMU online'}).waitFor();
  await page.click('[data-rp-adjust]');assert.equal(await page.locator('[data-rp-step-title]').innerText(),'Sit upright');assert.equal(harness.state.requests.filter(r=>r.path.endsWith('/sensing/command')).length,0);
  assert.equal(await page.locator('canvas').count(),3);await page.screenshot({path:testOutput('previews/imu-lab-desktop.png'),fullPage:true});
  console.log('PASS browser: protected sensing page, no automatic calibration commands, three live visualizations, prerequisites');
  for(let step=0;step<3;step++){await page.click('[data-rp-capture]');if(step<2)await page.waitForFunction(n=>document.querySelector('[data-rp-step-count]').textContent==='STEP '+n+' OF 3',step+2);else await page.locator('[data-rp-message]').filter({hasText:'All three positions are saved'}).waitFor();}
  await page.locator('[data-rp-forward]').filter({hasText:'12°'}).waitFor();assert.equal(harness.state.requests.filter(r=>r.path.endsWith('/rider/calibration')).length,3);
  console.log('PASS browser: upright / forward / neutral actions and orientation update');
  assert.equal(await page.locator('#startTrial,#downloadTrial,#trialLabel').count(),0);
  assert.match(await page.locator('#boardLinkStatus').innerText(),/Communicating/);
  assert.equal((await page.request.get(base+'/api/v1/sensing/trial')).status(),404);
  harness.state.imuLab.link.online=false;
  await page.locator('#boardLinkStatus').filter({hasText:'Not communicating'}).waitFor();
  harness.state.imuLab.link.online=true;harness.state.imuLab.imu.online=false;
  await page.locator('#boardLinkStatus').filter({hasText:'Connection incomplete'}).waitFor();
  harness.state.imuLab.imu.online=true;
  await page.locator('#boardLinkStatus').filter({hasText:'Communicating'}).waitFor();
  console.log('PASS clear board connection status and removed recorder interface/API');
  Object.assign(harness.state.imuLab.imu.detector,{valid:true,state:4,candidates:1,rotationHoldMs:120,tiltHoldMs:810,postureChangeDeg:65});
  await page.locator('#fallDiagnostic').filter({hasText:'Fall candidate'}).waitFor();await page.click('#resetCandidate');await page.locator('#fallDiagnostic').filter({hasText:'Waiting for quiet reference'}).waitFor();assert.equal(harness.state.imuLab.imu.detector.candidates,1);
  harness.state.imuLab.imu.profile.accelLsbPerG=999;await page.locator('#imuLiveStatus').filter({hasText:'Incompatible'}).waitFor();assert.equal(await page.locator('#startTrial').count(),0);harness.state.imuLab.imu.profile.accelLsbPerG=2048;
  console.log('PASS browser: provisional candidate/reset stays diagnostic; unknown profile disables acquisition actions');
  Object.assign(harness.state.imuLab.imu,{healthy:false});harness.state.imuLab.imu.diagnostics.error=2;
  await page.locator('#imuLiveStatus').filter({hasText:'BMI323 identity mismatch'}).waitFor();assert.equal(await page.locator('#startTrial').count(),0);assert.equal(await page.locator('#retryImu').isDisabled(),false);
  assert.equal(await page.locator('#boardLinkStatus').innerText(),'Communicating');
  console.log('PASS browser: no-device diagnostic, honest unavailable readings, capture disabled, retry available');
  Object.assign(harness.state.imuLab.imu,{healthy:true});harness.state.imuLab.imu.diagnostics.error=0;await page.locator('#imuLiveStatus').filter({hasText:'IMU online'}).waitFor();
  await page.route('**/api/v1/sensing/imu',()=>{});await page.locator('#imuLiveStatus').filter({hasText:'stale'}).waitFor();assert.equal(await page.locator('#boardLinkStatus').innerText(),'Status unavailable');assert.equal(await page.locator('#snapshotImu').isDisabled(),true);assert.equal(await page.locator('#resetCandidate').isDisabled(),true);assert.equal(await page.locator('#accelMagnitude').innerText(),'Unavailable');await page.unroute('**/api/v1/sensing/imu');
  console.log('PASS browser: stalled live response freezes visualization and disables fresh-reading actions');
  await context.close();const mobile=await browser.newContext({viewport:{width:390,height:844}}),m=await mobile.newPage();
  await m.goto(base+'/admin/sensing');await m.locator('#authWrap').waitFor({state:'visible'});await m.fill('#authUser','developer');await m.fill('#authPass','developer-test-password');await m.click('#loginSubmit');await m.locator('#imuLiveStatus').filter({hasText:'IMU online'}).waitFor();
  const size=await m.evaluate(()=>({w:innerWidth,scroll:document.documentElement.scrollWidth}));assert(size.scroll<=size.w+1,JSON.stringify(size));await m.screenshot({path:testOutput('previews/imu-lab-mobile.png'),fullPage:true});await mobile.close();
  assert.deepEqual(errors,[]);assert.deepEqual(csp,[]);console.log('PASS browser: mobile layout, no JavaScript errors or CSP violations');
 }catch(error){console.error('State at failure:',JSON.stringify(harness.state.imuLab));console.error('Last command requests:',JSON.stringify(harness.state.requests.filter(r=>r.path.endsWith('/sensing/command')).slice(-4)));throw error;}finally{await browser.close();await harness.close();}
})().catch(e=>{console.error(e);process.exit(1)});
