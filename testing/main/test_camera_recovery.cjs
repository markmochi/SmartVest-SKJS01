// Executes the actual camera page module with a small DOM/poller adapter.
// This verifies automatic re-rendering, not browser layout or real networking.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const elements=new Map();let poll,endpoint,options,mounts=0;
const $=id=>{if(!elements.has(id))elements.set(id,{});return elements.get(id);};
const ctx={mountFileLibrary:()=>()=>{},$,api:()=>{},session:{},esc:String,num:(v)=>String(v),age:v=>v==null?'Never':String(v),badge:s=>s,kv:rows=>rows.flat().join(' '),notice:s=>s,panel:()=>{},renderContent:(html,id)=>$(id).innerHTML=html,toast:()=>{},startPolling:(p,callback,o)=>{endpoint=p;poll=callback;options=o;++mounts;},action:()=>{},Date,console};
let source=fs.readFileSync(path.join(__dirname,'../../Main_and_Cameras/ui/developer/cameras.js'),'utf8').replace(/\r\n/g,'\n').replace(/^import .*;\n/gm,'').replace('export function mountCameras','function mountCameras');
const diagnostics=fs.readFileSync(path.join(__dirname,'../../Main_and_Cameras/ui/shared/diagnostics.js'),'utf8').replace(/^export /gm,'');
vm.runInNewContext(diagnostics+'\n'+source+'\nmountCameras(undefined,mountFileLibrary);',ctx);
assert.equal(endpoint,'cameras/monitor');assert.equal(options.interval,1500);
const front={id:'front',online:false,connectionState:'waiting',heartbeatAgeMs:null,connections:0};
const rear={...front,id:'rear'};
const update=()=>poll({cameras:[front,rear],logs:[]});
update();assert.match($('cameraCards').innerHTML,/Waiting for first connection/);
Object.assign(front,{online:true,statusFresh:true,connectionState:'online',heartbeatAgeMs:0,connections:1,recordingReported:true,sdReported:true,telemetry:{schema:1,currentFrames:10,frameAgeMs:20,effectiveFrameAgeMs:20}});
update();assert.match($('cameraCards').innerHTML,/Recording · recently confirmed/);assert.match($('cameraCards').innerHTML,/Waiting for first connection/);
Object.assign(front,{online:true,statusFresh:false,connectionState:'online',heartbeatAgeMs:9000});update();
assert.match($('cameraCards').innerHTML,/Connected · status delayed/);assert.doesNotMatch($('cameraCards').innerHTML,/Recording · recently confirmed/);
Object.assign(front,{online:false,statusFresh:false,connectionState:'offline',heartbeatAgeMs:16000});update();
assert.match($('cameraCards').innerHTML,/Offline · current state unknown/);assert.doesNotMatch($('cameraCards').innerHTML,/Recording · recently confirmed/);
Object.assign(front,{online:true,statusFresh:true,connectionState:'online',heartbeatAgeMs:0,connections:2});
Object.assign(rear,{online:true,statusFresh:true,connectionState:'online',heartbeatAgeMs:0,connections:1,sdReported:false,recordingReported:false});update();
assert.match($('cameraCards').innerHTML,/Recording · recently confirmed/);assert.match($('cameraCards').innerHTML,/SD unavailable/);
assert.doesNotMatch($('cameraCards').innerHTML,/Waiting for first connection|Offline · current state unknown/);
assert.equal(mounts,1);console.log('PASS actual camera UI module: initial waiting, late front, offline cached status, automatic front recovery, late rear without SD; no reload/remount');
