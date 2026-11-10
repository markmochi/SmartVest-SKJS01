const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,login,loaded}=require('./dashboard-test-helpers.cjs');
(async()=>{
 const h=createHarness();prepare(h);const base=await h.start(),browser=await launch(),adminContext=await browser.newContext(),riderContext=await browser.newContext();
 const admin=await adminContext.newPage(),rider=await riderContext.newPage(),errors=[];
 for(const p of [admin,rider])p.on('pageerror',e=>errors.push(e.message));
 try{
  await rider.goto(base+'/account');await login(rider,'rider');assert.equal(await rider.inputValue('#accountUsername'),'admin');assert.equal(await rider.locator('#resetForm').count(),0);
  await admin.goto(base+'/admin/account');await login(admin,'admin');assert.equal(await admin.inputValue('#accountUsername'),'developer');
  assert.match(await admin.locator('#pageContent').innerText(),/Administrator sign-in details/);
  const forbidden=await rider.evaluate(async()=>{try{await window.smartvestRuntime.api('admin/reset-data','POST',{current:'developer-test-password',confirm:'RESET'});return 200;}catch(e){return e.status;}});assert.equal(forbidden,403);
  await admin.fill('#accountUsername','owner');await admin.fill('#currentPassword','developer-test-password');await admin.fill('#newPassword','Owner-private-2026');await admin.fill('#repeatPassword','Owner-private-2026');
  await admin.click('#saveAccount');await admin.click('#confirmYes');await admin.locator('#authWrap').waitFor({state:'visible'});
  assert.equal(h.state.riderUsername,'admin');assert.equal(h.state.riderPassword,'admin123');
  await admin.fill('#authUser','owner');await admin.fill('#authPass','Owner-private-2026');await admin.click('#loginSubmit');await loaded(admin);
  await admin.click('nav a[href="/admin/account"]');await loaded(admin);assert.equal(await admin.inputValue('#accountUsername'),'owner');
  const contacts=await adminContext.newPage();contacts.on('pageerror',e=>errors.push(e.message));await contacts.goto(base+'/admin/contacts');await loaded(contacts);assert.equal(await contacts.inputValue('#riderName'),'Alex');
  async function reset(password){await admin.fill('#resetPassword',password);await admin.fill('#resetConfirm','RESET');await admin.click('#resetData');await admin.click('#confirmYes');}
  await reset('wrong');await admin.locator('#resetMessage').filter({hasText:'incorrect'}).waitFor();assert.equal(h.state.settings.rider,'Alex');
  h.state.resetBlocked=true;await reset('Owner-private-2026');await admin.locator('#resetMessage').filter({hasText:'Finish pending'}).waitFor();assert.equal(h.state.settings.contacts.length,1);
  h.state.resetBlocked=false;h.state.resetDeferred=true;await reset('Owner-private-2026');await admin.locator('#resetMessage').filter({hasText:'saved and finishing'}).waitFor();assert(await admin.locator('#resetData').isDisabled());h.state.resetPending=false;await admin.locator('#resetMessage').filter({hasText:'completed'}).waitFor();
  assert.equal(h.state.settings.rider,'');assert.equal(h.state.settings.contacts.length,0);assert.equal(h.state.jobs.length,0);assert.equal(h.state.incidents.length,0);
  assert.equal(h.state.adminUsername,'owner');assert.equal(h.state.adminPassword,'Owner-private-2026');
  await rider.locator('#authWrap').waitFor({state:'visible'});await contacts.waitForFunction(()=>document.getElementById('riderName').value==='');
  assert.equal(await contacts.locator('.contact-row').count(),0);
  await login(rider,'rider');await rider.locator('#beginSetup').waitFor();assert.equal(h.state.riderUsername,'admin');
  await admin.click('nav a[href="/admin/cameras"]');await loaded(admin);
  const details=admin.locator('details[data-key="camera-events"]');assert.equal(await details.evaluate(e=>e.open),false);
  assert.doesNotMatch(await admin.locator('#pageContent').innerText(),/How the rolling buffer works/);
  await details.locator('summary').click();await admin.selectOption('#cameraRoleFilter','rear');await admin.waitForTimeout(3400);
  assert(await details.evaluate(e=>e.open));assert.equal(await admin.inputValue('#cameraRoleFilter'),'rear');
  await details.locator('summary').click();await admin.waitForTimeout(1700);assert.equal(await details.evaluate(e=>e.open),false);
  assert.deepEqual(errors,[]);
  console.log('PASS separate account forms and credentials, rider authorization refusal, admin relogin, password/active-work reset refusal, reset clears profile/history, rider signout/default relogin, untouched admin contacts refresh, and stable collapsible camera events');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
