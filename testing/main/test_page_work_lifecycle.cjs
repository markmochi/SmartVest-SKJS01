const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const {prepare,launch,login,loaded}=require('./dashboard-test-helpers.cjs');
(async()=>{
 const h=createHarness();prepare(h);const base=await h.start(),browser=await launch(),page=await browser.newPage(),errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 try{
  await page.goto(base+'/admin');await login(page,'admin');
  await page.click('nav a[href="/admin/cameras"]');await loaded(page);
  let heldHistory,summaryReads=0;
  const slow=async route=>{const view=new URL(route.request().url()).searchParams.get('view');if(view==='events'){heldHistory=route;return;}if(view==='summary')summaryReads++;await route.continue();};
  await page.route('**/api/v1/cameras/monitor*',slow);
  await page.click('#refreshPage');
  for(let i=0;i<150&&!heldHistory;i++)await page.waitForTimeout(20);assert(heldHistory);
  const first=summaryReads;
  await page.waitForTimeout(2200);
  assert(summaryReads>first,'a hung history read must not delay the next live status cycle until its 4.5-second timeout');
  assert(await page.locator('#connectionBanner').isHidden());
  await page.unroute('**/api/v1/cameras/monitor*',slow);await heldHistory.abort().catch(()=>{});
  await page.click('nav a[href="/admin/contacts"]');await loaded(page);
  await page.fill('#riderName','Lifecycle fixture');await page.uncheck('#notifyAdded');
  let post,read;const hold=async route=>{if(route.request().method()==='POST')post=route;else read=route;};
  await page.route('**/api/v1/settings',hold);
  await page.click('#saveContacts');await page.click('#confirmYes');
  for(let i=0;i<100&&!post;i++)await page.waitForTimeout(10);assert(post);
  await page.click('nav a[href="/admin/network"]');assert(page.url().endsWith('/admin/contacts'),'navigation must wait for a submitted save');
  await post.continue();
  for(let i=0;i<100&&!read;i++)await page.waitForTimeout(10);assert(read);
  // The write is complete; a stalled verification GET must not trap navigation.
  await page.click('nav a[href="/admin/network"]');await loaded(page);
  await page.unroute('**/api/v1/settings',hold);await read.abort().catch(()=>{});
  await page.click('nav a[href="/admin/contacts"]');await loaded(page);
  assert.equal(await page.inputValue('#riderName'),'Lifecycle fixture');
  assert.equal(h.state.requests.filter(r=>r.method==='POST'&&r.path==='/api/v1/settings').length,1);
  assert.deepEqual(errors,[]);
  console.log('PASS stalled camera history cannot block live status; submitted contact save is protected; stalled post-save read cancels on navigation; saved data retained; no repeated writes or stale DOM errors');
 }finally{await browser.close();await h.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
