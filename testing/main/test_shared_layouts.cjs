// User-visible routing must stay correct when HTML payloads share flash data.
const assert=require('node:assert/strict');
const {createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start();
 const browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox']});
 try{
  for(const [username,password,start,count] of [['admin','admin123','/contacts',6],['developer','developer-test-password','/admin/gps',14]]){
   const context=await browser.newContext(),page=await context.newPage();
   const errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.goto(base+start);await page.locator('#authWrap').waitFor({state:'visible'});
   await page.fill('#authUser',username);await page.fill('#authPass',password);await page.click('#loginSubmit');
   await page.locator('#workspace').waitFor({state:'visible'});
   const links=await page.locator('nav a[data-page]').evaluateAll(all=>all.map(a=>({route:a.getAttribute('href'),page:a.dataset.page,title:a.querySelector('span').textContent})));
   assert.equal(links.length,count);
   async function selected(link){
    await page.locator('#workspace').waitFor({state:'visible'});
    assert.equal(await page.locator('nav a[aria-current="page"]').count(),1);
    assert.equal(await page.locator('nav a[aria-current="page"]').getAttribute('href'),link.route);
    assert.equal(await page.locator('body').getAttribute('data-page'),link.page);
    assert.equal(await page.title(),link.title+' | SmartVest');
   }
   for(const link of links){await page.goto(base+link.route);await selected(link);}
   await page.reload();await selected(links.at(-1));
   await page.goBack();await selected(links.at(-2));
   assert.deepEqual(errors,[]);await context.close();
  }
  console.log('PASS shared layouts: direct non-overview login, all 20 route titles/current navigation/page selection, reload and Back; both roles');
 }finally{await browser.close();await h.close();}
})().catch(error=>{console.error(error);process.exit(1);});
