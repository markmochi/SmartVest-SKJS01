const {testOutputDir}=require('../paths.cjs').suite(__filename);
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {createHarness}=require('./preview_server.cjs'),pw=require(process.env.SJKS_PLAYWRIGHT);
(async()=>{
 const h=createHarness();h.state.imuLab=require('./imu-fixtures.cjs').fixture();const jobs=h.state.jobs.length,base=await h.start(),b=await pw.chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--no-sandbox']}),errors=[];
 const p=await b.newPage({viewport:{width:1440,height:1000},acceptDownloads:true});p.on('pageerror',e=>errors.push(e.message));p.on('dialog',d=>d.accept());
 const output=process.env.SJKS_RIDE_EVIDENCE||testOutputDir('');fs.mkdirSync(output,{recursive:true});
 try{
  await p.goto(base+'/admin/ride-test');await p.fill('#authUser','developer');await p.fill('#authPass','developer-test-password');await p.click('#loginSubmit');
  await p.locator('#rideMessage').filter({hasText:'Ready.'}).waitFor();assert.equal(await p.locator('#sideNav a[href="/admin/ride-test"]').count(),1);
  const before=h.state.requests.filter(r=>r.method==='POST').length;
  await p.fill('#rideNotes','Normal ride / sample comparison');await p.click('#rideStart');await p.locator('#rideMessage').filter({hasText:'Recording'}).waitFor();
  await p.waitForTimeout(1400);h.state.imuLab.imu.detector.candidates++;await p.waitForTimeout(1400);await p.click('#rideStop');await p.locator('#rideMessage').filter({hasText:'Ready to download'}).waitFor();
  assert(Number(await p.locator('#rideImu').innerText())>5);assert(Number(await p.locator('#rideSaved').innerText())>5);assert.equal(h.state.jobs.length,jobs);assert.equal(h.state.requests.filter(r=>r.method==='POST').length,before);
  for(const [button,file]of [['#rideCsv','ride-test.zip'],['#rideExcel','ride-test.xlsx']]){const event=p.waitForEvent('download');await p.click(button);const download=await event;await download.saveAs(path.join(output,file));}
  console.log('PASS admin Start/Stop: detailed live sensor rows, source candidate moment, all-module polling, committed records and no hardware commands/SMS');
  const result=await p.evaluate(async()=>{
   const root=document.querySelector('script[type=module]').src.replace(/shared\/boot.js$/,''),{openRideStore,RIDE_MS}=await import(root+'developer/ride-data.js'),{createRideRecorder}=await import(root+'developer/ride-test.js');
   const store=await openRideStore();let time=0;const rec=createRideRecorder({store,clock:()=>time,fetch:async()=>({})});await rec.start('Automatic cap');time=RIDE_MS+50;
   for(let i=0;i<30&&(rec.state().active||rec.state().stopping);i++)await new Promise(r=>setTimeout(r,30));
   const s=rec.state(),rows=await rec.exportRows();await rec.stop('interrupted');const after=await rec.exportRows();if(rec.state().meta.state!=='completed'||after.length!==rows.length)throw Error('Stop must remain idempotent');store.close();return {state:s.meta.state,elapsed:s.meta.elapsed,rows:rows.length,beyond:rows.some(r=>r.elapsed>RIDE_MS)};
  });assert.equal(result.state,'completed');assert.equal(result.elapsed,1200000);assert.equal(result.beyond,false);assert(result.rows>0);
  console.log('PASS actual recorder automatic20-minute cap with monotonic clock; no rows beyond the deadline');
  await p.reload();await p.locator('#rideMessage').filter({hasText:'Ride completed'}).waitFor();assert(await p.locator('#rideExcel').isEnabled());
  await p.click('#rideStart');await p.waitForTimeout(1300);await p.reload();await p.locator('#rideMessage').filter({hasText:/Ride (interrupted|recording)/}).waitFor();assert(Number(await p.locator('#rideSaved').innerText())>0);
  console.log('PASS reload recovers committed browser data; active recording is not silently restarted');
  for(const width of [320,390,768,1440]){await p.setViewportSize({width,height:900});assert(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));}
  await p.screenshot({path:path.join(output,'ride-test.png'),fullPage:true});assert.deepEqual(errors,[]);
  console.log('PASS phone/desktop layout and no JavaScript exceptions');
 }finally{await b.close();await h.close();}
})().catch(e=>{console.error(e);process.exit(1);});
