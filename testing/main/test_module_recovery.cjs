const assert=require('node:assert/strict'),{createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness();h.state.factory=false;h.state.riderUsername='rider';h.state.riderPassword='rider-test-password';
 const base=await h.start(),browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader']}),errors=[];
 async function login(page,admin=false){await page.goto(base+(admin?'/admin':'/'));await page.fill('#authUser',admin?'developer':'rider');await page.fill('#authPass',admin?'developer-test-password':'rider-test-password');await page.click('#loginSubmit');}
 try{
  for(const admin of [false,true]){
   const context=await browser.newContext(),page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));let attempts=0,documents=0;
   page.on('request',r=>{if(r.resourceType()==='document')documents++;});
   await page.route('**/'+(admin?'developer':'rider')+'/app.js',route=>++attempts===1?route.abort('failed'):route.continue());
   const logins=h.state.requests.filter(r=>r.method==='POST'&&r.path==='/api/v1/login').length;
   await login(page,admin);await page.locator(admin?'.module-table':'#riderPosition').waitFor();
   assert.equal(attempts,2);assert.equal(documents,1);assert.equal(await page.locator('#reloadDashboard').count(),0);
   assert.equal(h.state.requests.filter(r=>r.method==='POST'&&r.path==='/api/v1/login').length,logins+1);
   assert.equal(await page.evaluate(()=>sessionStorage.getItem('sv-module-reload')),null);
   await context.close();
  }
  console.log('PASS rider/admin entry import failure: automatic recovery restores the authenticated view without a document reload or repeated login');
  const context=await browser.newContext(),page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));let attempts=0,documents=0;
  page.on('request',r=>{if(r.resourceType()==='document')documents++;});
  await page.route('**/shared/position.js',route=>++attempts<=2?route.abort('failed'):route.continue());
  const jobs=h.state.jobs.length,logins=h.state.requests.filter(r=>r.method==='POST'&&r.path==='/api/v1/login').length;
  await login(page);await page.locator('#riderPosition').waitFor();assert.equal(attempts,3);
  assert.equal(documents,1);assert.equal(await page.locator('#reloadDashboard').count(),0);
  assert(!/Failed to fetch|dynamically imported/.test(await page.locator('#pageContent').innerText()));
  assert.equal(h.state.jobs.length,jobs);assert.equal(h.state.requests.filter(r=>r.method==='POST'&&r.path==='/api/v1/login').length,logins+1);
  await context.close();assert.deepEqual(errors,[]);
  console.log('PASS repeated shared dependency failures recover automatically; no document reload, repeated login or SMS');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1);});
