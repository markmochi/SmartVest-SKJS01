const assert=require('node:assert/strict'),{createHarness}=require('./preview_server.cjs');
const pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness(),base=await h.start(),browser=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox','--enable-unsafe-swiftshader']});
 try{
  const page=await browser.newPage();await page.goto(base+'/admin');
  await page.fill('#authUser','developer');await page.fill('#authPass','developer-test-password');await page.click('#loginSubmit');
  await page.locator('.module-table').waitFor();
  const writes=h.state.requests.filter(r=>r.method==='POST').length;
  const outcome=await page.evaluate(async()=>{
   const prefix=document.querySelector('script[type="module"]').src.replace(/boot\.js$/,''),{startPolling,stopPolling}=await import(prefix+'poller.js'),{session}=await import(prefix+'api.js');
   stopPolling();let fail=true;const view=document.getElementById('pageContent'),detail=document.getElementById('connectionDetail');
   const renderer=()=>{if(fail)throw new Error('<img src=x onerror=alert(1)> display fixture');};
   const poll=startPolling('status',renderer,{load:async()=>({ok:true})});
   await poll.refresh();
   const display={label:document.getElementById('connection').textContent,detail:detail.textContent,connected:session.connected,injected:detail.querySelectorAll('img').length};
   fail=false;await poll.refresh();
   const recovered=session.connected;
   stopPolling();session.role='rider';
   const rider=startPolling('rider/status',renderer,{load:async()=>{const error=new Error('private modem fixture');error.status=503;throw error;}});
   await rider.refresh();
   const riderResult={detail:detail.textContent,connected:session.connected};
   stopPolling();session.role='developer';
   view.innerHTML='<div class="loading">Loading</div>';
   const pending=startPolling('status',renderer,{load:async()=>{const error=new Error('Transport timeout');error.offline=true;throw error;}});
   await pending.refresh();
   const firstTimeout={label:document.getElementById('connection').textContent,connected:session.connected,loading:view.querySelectorAll('.loading').length};
   stopPolling();return {display,recovered,riderResult,firstTimeout};
  });
  assert.equal(outcome.display.label,'Display unavailable');assert.equal(outcome.display.connected,false);assert.equal(outcome.display.injected,0);
  assert.match(outcome.display.detail,/display fixture/);assert(!/Wi-Fi/.test(outcome.display.detail));assert(outcome.recovered);
  assert.equal(outcome.riderResult.connected,false);assert(!/private modem fixture|HTTP 503/.test(outcome.riderResult.detail));
  assert.equal(outcome.firstTimeout.connected,false);assert.equal(outcome.firstTimeout.label,'Connecting to Vest');assert.equal(outcome.firstTimeout.loading,0);
  assert.equal(h.state.requests.filter(r=>r.method==='POST').length,writes);
  console.log('PASS actual poller: render failures and HTTP errors are visible, text is escaped, rider details remain plain, first timeouts never claim connected, recovery clears failure, no writes repeated');
 }finally{await browser.close();await h.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
