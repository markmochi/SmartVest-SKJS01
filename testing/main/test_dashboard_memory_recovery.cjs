// Failed status reads must preserve usable navigation and the page structure.
const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch}=require('./dashboard-test-helpers.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
(async()=>{
 const h=createHarness();prepare(h);const base=await h.start(),browser=await launch();
 try{
  const page=await browser.newPage({viewport:{width:1640,height:950}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));await page.clock.install();
  let overview=false,tools=false,settingsAttempts=0;
  await page.route('**/api/v1/status*',async route=>{
   const view=new URL(route.request().url()).searchParams.get('view');
   assert(view,'normal pages must never poll full diagnostic status');
   if((view==='overview'&&!overview)||(view==='tools'&&!tools))return route.fulfill({status:503,contentType:'application/json',body:'{"error":"Response memory unavailable"}'});
   await route.continue();
  });
  await page.route('**/api/v1/settings',async route=>{
   if(++settingsAttempts<=2)return route.fulfill({status:503,contentType:'application/json',body:'{"error":"Response memory unavailable"}'});
   await route.continue();
  });
  await page.goto(base+'/admin');await page.fill('#authUser','developer');await page.fill('#authPass','developer-test-password');await page.click('#loginSubmit');
  await page.locator('.module-table').waitFor();
  const writes=h.state.requests.filter(r=>r.method==='POST').length;
  async function navigation(){
   assert.equal(await page.locator('#sideNav a[data-page]').count(),14);
   assert(await page.locator('#pageJump').isVisible());
   if((await page.viewportSize()).width>980)assert(await page.locator('#sideNav').isVisible());
  }
  await navigation();assert.equal(await page.locator('.module-table tbody tr').count(),6);
  assert.equal(await page.locator('.module-table .status.neutral').count(),6);
  await page.clock.fastForward(21000);await navigation();
  assert.equal(await page.locator('.module-table tbody tr').count(),6);
  assert(!await page.locator('#pageContent').innerText().then(t=>t.includes('All modules ready')));
  await page.screenshot({path:testOutput('overview-first-503.png'),fullPage:true});
  overview=true;await page.waitForFunction(()=>!document.getElementById('refreshPage').disabled);await page.click('#refreshPage');
  await page.waitForFunction(()=>document.getElementById('connection').classList.contains('live'));
  await page.click('#sideNav a[data-page="tools"]');
  for(const id of ['sendIntroduction','sendCrashSms','showKeys','queueProgress','ledPreview'])await page.locator('#'+id).waitFor({state:'visible'});
  await page.clock.fastForward(21000);await navigation();
  assert(await page.locator('#sendIntroduction').isDisabled());assert(await page.locator('#sendCrashSms').isDisabled());
  assert.equal(await page.locator('#pageContent .error').count(),0);
  await page.click('#showKeys');await page.locator('#frontKey').waitFor({state:'visible'});await page.click('#hideKeys');
  tools=true;await page.waitForFunction(()=>!document.getElementById('refreshPage').disabled);await page.click('#refreshPage');
  // Settings reads retry independently; writes remain unavailable until loaded.
  await page.clock.fastForward(6500);
  await page.waitForFunction(()=>!document.getElementById('sendIntroduction').disabled);
  assert(settingsAttempts>=3);assert.equal(await page.locator('#messageRecipient option').count(),h.state.settings.contacts.length);
  await page.screenshot({path:testOutput('tools-recovered.png'),fullPage:true});
  tools=false;await page.click('#refreshPage');
  await page.waitForFunction(()=>document.getElementById('workspace').classList.contains('stale'));
  for(const id of ['sendIntroduction','showKeys','queueProgress','ledPreview'])assert(await page.locator('#'+id).isVisible());
  assert(await page.locator('#sendIntroduction').isDisabled());await navigation();
  await page.screenshot({path:testOutput('tools-retained-after-503.png'),fullPage:true});
  await page.setViewportSize({width:390,height:844});await page.click('#mobileMenu');
  assert(await page.locator('#sideNav').isVisible());assert.equal(await page.locator('#sideNav a').count(),14);
  overview=false;await page.click('#sideNav a[data-page="overview"]');await page.locator('.module-table').waitFor();
  assert.equal(await page.locator('.module-table tbody tr').count(),6);assert(await page.locator('#mobileMenu').isVisible());
  await page.screenshot({path:testOutput('mobile-first-503.png'),fullPage:true});
  assert.deepEqual(errors,[]);assert.equal(h.state.requests.filter(r=>r.method==='POST').length,writes);
  console.log('PASS persistent initial and later 503: six overview modules, all 14 navigation links, tools panels, independent settings retry, safe write gating, automatic recovery, camera key read, mobile navigation and no repeated writes');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
