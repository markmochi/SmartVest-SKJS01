const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,login,loaded}=require('./dashboard-test-helpers.cjs');
(async()=>{const h=createHarness();prepare(h);const base=await h.start(),browser=await launch();
try{
 const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
 let failHistory=true,holdSummary=true,release,historyCalls=0,summaryCalls=0;
 await page.route('**/api/v1/cameras/monitor*',async route=>{
  const view=new URL(route.request().url()).searchParams.get('view');
  if(view==='summary'){summaryCalls++;if(holdSummary){holdSummary=false;await new Promise(resolve=>release=resolve);}}
  if(view==='events'){historyCalls++;if(failHistory){await route.fulfill({status:503,json:{error:'Response memory unavailable'}});return;}}
  await route.continue();
 });
 await page.goto(base+'/admin');await login(page,'admin');await page.click('nav a[href="/admin/cameras"]');
 await page.locator('details[data-key="camera-diagnostics"] > summary').click();
 await page.locator('.camera-card').nth(1).waitFor();
 await page.locator('details[data-key="camera-events"] > summary').click();
 assert.match(await page.locator('.camera-card').nth(0).innerText(),/Front camera/);
 assert.match(await page.locator('.camera-card').nth(1).innerText(),/Rear camera/);
 for(let i=0;i<100&&!release;i++)await page.waitForTimeout(10);assert(release);release();await loaded(page);
 await page.locator('#cameraLogLimits').filter({hasText:'history is delayed'}).waitFor();
 const initial=summaryCalls;await page.waitForFunction(()=>document.getElementById('connection').classList.contains('live'));
 await page.waitForTimeout(2000);assert(summaryCalls>initial,'live camera status continues while history fails');
 assert(await page.locator('#connectionBanner').isHidden());assert.equal(await page.locator('.camera-card').count(),2);
 failHistory=false;
 await page.waitForFunction(()=>!document.getElementById('cameraLogLimits').textContent.includes('delayed'),{},{timeout:12000});assert(historyCalls>=2);
 assert.equal(h.state.requests.filter(r=>r.method==='POST').length,1,'only login writes');
 assert.deepEqual(errors,[]);
 console.log('PASS camera structure before first response; failed history does not hide or stop live status; bounded history retry recovers without reload or writes');
}finally{await browser.close();await h.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
