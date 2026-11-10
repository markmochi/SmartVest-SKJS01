const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start();
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true});
 const context=await browser.newContext(),page=await context.newPage(),errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 await context.addInitScript(()=>Object.defineProperty(Navigator.prototype,'onLine',{get:()=>false,configurable:true}));
 try{
  await page.goto(base+'/');await page.locator('#authWrap').waitFor({state:'visible'});
  await page.fill('#authUser','admin');await page.fill('#authPass','admin123');await page.click('#loginSubmit');
  await page.locator('#workspace').waitFor({state:'visible'});
  await page.getByText('Monitoring and alerts ready',{exact:true}).waitFor();
  await page.waitForTimeout(2500);
  assert.equal(await page.locator('#connection').innerText(),'Connected to Vest');
  assert.equal(await page.locator('#workspace').evaluate(el=>el.classList.contains('stale')),false);
  await page.goto(base+'/equipment');await page.locator('[data-component="cellular"]').waitFor();
  await page.waitForTimeout(2500);
  assert.match(await page.locator('[data-component="cellular"]').innerText(),/Text alerts available/);
  await page.evaluate(()=>dispatchEvent(new Event('offline')));await page.waitForTimeout(1300);
  assert.equal(await page.locator('#connectionBanner').isVisible(),false);
  assert.equal(await page.locator('#connection').innerText(),'Connected to Vest');
  console.log('PASS normal rider sign-in enters dashboard; fresh local responses stay connected/available with browser Internet flag false and offline events');
  await page.route('**/api/v1/rider/status*',r=>r.abort());
  await page.locator('#connectionBanner').waitFor({state:'visible',timeout:25000});
  assert.equal(await page.locator('#workspace').evaluate(el=>el.classList.contains('stale')),true);
  assert.match(await page.locator('[data-component="cellular"]').innerText(),/Status unavailable/);
  await page.unroute('**/api/v1/rider/status*');
  await page.locator('#connectionBanner').waitFor({state:'hidden',timeout:12000});
  assert.equal(await page.locator('#connection').innerText(),'Connected to Vest');
  assert.deepEqual(errors,[]);
  console.log('PASS actual local-response loss still expires dashboard data; local service recovery reconnects without requiring Internet validation');
 }finally{await context.close();await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1)});
