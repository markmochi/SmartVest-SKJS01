const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,login}=require('./dashboard-test-helpers.cjs');
const {testOutput}=require('../paths.cjs').suite(__filename);
(async()=>{const h=createHarness();prepare(h);h.state.riderSetupComplete=false;Object.assign(h.state.settings,{rider:'',contacts:[],notifyAdded:true});const base=await h.start(),browser=await launch();try{
 const p=await browser.newPage({viewport:{width:390,height:844}}),errors=[];p.on('pageerror',e=>errors.push(e.message));await p.goto(base+'/');await login(p,'rider');await p.locator('#beginSetup').waitFor();
 assert.equal(await p.locator('.rider-nav').isVisible(),false);await p.waitForTimeout(450);await p.screenshot({path:testOutput('welcome-mobile.png'),fullPage:true});
 await p.click('#beginSetup');await p.locator('#riderName:not([disabled])').waitFor();assert.equal(await p.locator('.contact-row').count(),1);assert.equal(await p.locator('#notifyAdded').isChecked(),false);
 await p.fill('#riderName','Alex Rider');await p.fill('[data-field=name]','Jamie');await p.fill('[data-field=phone]','09170000001');await p.click('#saveContacts');await p.locator('#finishSetup').waitFor();assert(!h.state.riderSetupComplete);assert.equal(h.state.settings.contacts.length,1);assert.equal(h.state.settings.notifyAdded,false);
 await p.locator('[data-rp-canvas]').waitFor();await p.waitForTimeout(450);assert.equal(await p.evaluate(()=>scrollY),0);await p.screenshot({path:testOutput('position-mobile.png'),fullPage:true});
 let failed=true;await p.route('**/api/v1/rider/setup',async route=>{if(failed){failed=false;await route.fulfill({status:503,contentType:'application/json',body:JSON.stringify({error:'Setup could not be saved. Please try again.'})});}else await route.continue();});
 await p.click('#finishSetup');await p.locator('#setupMessage').filter({hasText:'Try again'}).waitFor();assert(!h.state.riderSetupComplete);await p.click('#finishSetup');await p.locator('.ride-status').waitFor();assert(h.state.riderSetupComplete);
 assert.equal(h.state.requests.filter(r=>r.path==='/api/v1/rider/calibration').length,0,'skip never changes saved calibration');assert.equal(await p.locator('.rider-nav').isVisible(),true);
 await p.reload();await p.locator('.ride-status').waitFor();assert.equal(await p.locator('#beginSetup').count(),0);
 await p.click('.rider-nav a[href="/contacts"]');await p.locator('.contact-row button').click();await p.click('#confirmYes');await p.click('#saveContacts');await p.click('#confirmYes');await p.locator('#contactsMessage').filter({hasText:'saved'}).waitFor();assert.equal(h.state.settings.contacts.length,0);assert(h.state.riderSetupComplete);
 await p.reload();await p.locator('#contactsForm').waitFor();assert.equal(await p.locator('#beginSetup').count(),0);
 const ac=await browser.newContext(),admin=await ac.newPage();await admin.goto(base+'/admin/account');await login(admin,'admin');await admin.fill('#resetPassword','developer-test-password');await admin.fill('#resetConfirm','RESET');await admin.click('#resetData');await admin.click('#confirmYes');await admin.locator('#resetMessage').filter({hasText:'completed'}).waitFor();assert(!h.state.riderSetupComplete);
 await p.goto(base+'/');await login(p,'rider');await p.locator('#beginSetup').waitFor();await p.emulateMedia({reducedMotion:'reduce'});assert.equal(await p.locator('#pageContent').evaluate(e=>getComputedStyle(e).animationName),'none');
 for(const width of [320,768,1440]){await p.setViewportSize({width,height:900});assert(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));}
 assert.deepEqual(errors,[]);console.log('PASS saved first-use flow, required contacts, optional unchanged calibration, failed completion retry, reload, contact removal without onboarding, admin reset restores onboarding, responsive widths and reduced motion');
 }finally{await browser.close();await h.close();}})().catch(e=>{console.error(e);process.exitCode=1});
