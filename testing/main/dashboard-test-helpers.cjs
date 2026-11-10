const assert=require('node:assert/strict');
const {chromium}=require(process.env.SJKS_PLAYWRIGHT||'playwright');
const {fixture}=require('./imu-fixtures.cjs');
const riderPaths=['/','/equipment','/cameras','/contacts','/activity','/account'];
const adminPaths=['/admin','/admin/gsm','/admin/gps','/admin/sensing','/admin/power','/admin/cameras','/admin/network','/admin/ride-test','/admin/system','/admin/contacts','/admin/activity','/admin/incidents','/admin/tools','/admin/account'];
function prepare(h){
 h.state.imuLab=fixture();
 const i=h.state.imuLab.imu;
 Object.assign(i.pose,{valid:true,sideDeg:12,forwardDeg:24});
 Object.assign(i.calibration,{flags:15,operation:3,result:2,samples:240,revision:8});
 Object.assign(i.detector,{valid:true,state:2,candidates:0,automaticReady:true,automaticDetectionEnabled:true,mainAutomaticDetectionEnabled:true});
 Object.assign(i.sample,{flags:100,temperatureValid:true});
}
function launch(){return chromium.launch({executablePath:process.env.SJKS_CHROMIUM_BINARY,headless:true,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});}
async function login(page,role){
 await page.locator('#authWrap').waitFor({state:'visible'});
 await page.fill('#authUser',role==='admin'?'developer':'admin');
 await page.fill('#authPass',role==='admin'?'developer-test-password':'admin123');
 await page.click('#loginSubmit');
 await loaded(page);
}
async function loaded(page){
 await page.locator('#workspace').waitFor({state:'visible'});
 // Ride Test intentionally waits for Start before requesting telemetry.
 await page.waitForFunction(()=>{const p=document.getElementById('pageContent');return p&&p.children.length&&!p.querySelector('.loading')&&!p.querySelector('#reloadDashboard')&&(document.body.dataset.page==='ride-test'||document.getElementById('connection')?.classList.contains('live'));});
}
async function sensingReady(page){
 await page.waitForFunction(()=>{
  const text=id=>document.getElementById(id)?.textContent||'';
  return /^\d+\.\d+ g$/.test(text('accelMagnitude'))&&/^\d+\.\d+ °\/s$/.test(text('gyroMagnitude'))&&/200\.0 Hz/.test(text('imuRate'))&&/^Detection ready -/.test(text('automaticDetectionStatus'))&&/^Updated /.test(text('receivedTime'));
 });
 assert(Math.abs(parseFloat(await page.locator('#accelMagnitude').innerText())-Math.hypot(100/2048,0,1))<.001);
 assert.equal(await page.locator('#gyroMagnitude').innerText(),'0.00 °/s');
 assert.match(await page.locator('#imuLiveStatus').innerText(),/IMU online/);
 assert(await page.locator('#automaticDetectionStatus').evaluate(e=>e.classList.contains('good')));
 assert.match(await page.locator('#imuTemperature').innerText(),/23\.2 °C/);
}
async function theme(page,value){
 if(await page.evaluate(()=>document.documentElement.dataset.theme)!==value){
  const choice=page.locator('[data-theme-choice="'+value+'"]');
  if(await choice.count())await choice.click();else await page.evaluate(value=>window.smartvestBoot.setAppearance(value),value);
 }
 await page.waitForFunction(value=>document.documentElement.dataset.theme===value,value);
 const saved=await page.evaluate(()=>localStorage.getItem('sv-theme'));
 assert(saved===value||(value==='cream'&&saved===null));
}
function luminance(value){
 const text=value.trim();let parts;
 if(text.startsWith('#')){const s=text.slice(1);parts=(s.length===3?[...s].map(x=>x+x):s.match(/../g)).slice(0,3).map(x=>parseInt(x,16));}
 else parts=text.match(/[\d.]+/g).slice(0,3).map(Number);
 return parts.map(v=>v/255).map(v=>v<=.04045?v/12.92:((v+.055)/1.055)**2.4).reduce((n,v,i)=>n+v*[.2126,.7152,.0722][i],0);
}
function contrast(a,b){const x=luminance(a),y=luminance(b);return (Math.max(x,y)+.05)/(Math.min(x,y)+.05);}
module.exports={prepare,launch,login,loaded,sensingReady,theme,contrast,riderPaths,adminPaths};
