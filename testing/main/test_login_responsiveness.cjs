const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,loaded}=require('./dashboard-test-helpers.cjs');
(async()=>{
 const h=createHarness();prepare(h);const base=await h.start(),browser=await launch();
 try{
  for(const role of ['rider','admin']){
   const context=await browser.newContext({viewport:{width:390,height:844}}),page=await context.newPage(),errors=[];
   page.on('pageerror',e=>errors.push(e.message));
   await page.clock.install();
   let release,seen;const gate=new Promise(resolve=>release=resolve),started=new Promise(resolve=>seen=resolve);let posts=0;
   await page.route('**/api/v1/login',async route=>{posts++;seen();await gate;await route.continue();});
   await page.goto(base+(role==='admin'?'/admin':'/'));
   await page.locator('#authWrap').waitFor({state:'visible'});
   await page.fill('#authUser',role==='admin'?'developer':'admin');
   await page.fill('#authPass',role==='admin'?'developer-test-password':'admin123');
   await page.click('#loginSubmit');await started;
   // Advance beyond the previous twelve-second deadline without waiting for
   // hardware or changing the firmware's password derivation strength.
   await page.clock.fastForward(13000);
   assert.equal(await page.locator('#loginMessage').innerText(),'Signing in...');
   assert(await page.locator('#loginSubmit').isDisabled());assert.equal(posts,1);
   release();await loaded(page);assert.equal(posts,1);
   assert(!/did not respond|Check your Wi-Fi/.test(await page.locator('body').innerText()));
   assert.deepEqual(errors,[]);await context.close();
   console.log('PASS',role,'delayed sign-in keeps its request, shows no false Wi-Fi failure, never replays credentials, and opens the dashboard');
  }
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1);});
