import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'smartvest-ui-'));
process.on('exit',()=>fs.rmSync(temp,{recursive:true,force:true}));
fs.cpSync(fileURLToPath(new URL('../../Main_and_Cameras/ui/',import.meta.url)),temp,{recursive:true});
fs.writeFileSync(path.join(temp,'package.json'),' {"type":"module"} ');
const {renderModule,createVoltageDisplay}=await import(pathToFileURL(path.join(temp,'developer/modules.js')));
const power={enabled:true,sensorModel:'INA226',viaSensing:true,responding:true,sampleValid:true,sourceV:12,busV:12,currentMA:500,powerMW:6000,batteryPercent:80,sampleAgeMs:0,nanoBaud:19200,sensingRxPin:32};
let html=renderModule('power',{power});
assert(html.includes('80%')&&html.includes('12.00'));
assert(!html.includes('data-led-test='));
assert(html.includes('INA226')&&/19,?200/.test(html));
html=renderModule('power',{power:{...power,sampleValid:false,batteryPercent:null,sourceV:null}});
assert(html.includes('Unavailable'));
console.log('PASS active power page: Nano/INA226 readings, unavailable state and no obsolete LED controls');

// Owner-observed full-charge fluctuations must settle without changing raw data.
const display=createVoltageDisplay();let tick=0,sequence=0;
const reading=value=>({sampleValid:true,sourceV:value,sampleAgeMs:20,nanoSequence:++sequence,batteryPercent:100});
const outputs=[];
for(let i=0;i<180;i++){
 const packet=reading(i%2?11.80:11.65),before=JSON.stringify(packet);
 outputs.push(display(packet,tick));tick+=2000;assert.equal(JSON.stringify(packet),before);
}
assert.equal(new Set(outputs.slice(5)).size,1,'full-charge noise should not toggle the main voltage display');
assert(outputs.every(v=>v>=11.6&&v<=11.8));
const before=outputs.at(-1),duplicate=reading(11.8);display(duplicate,tick);
assert.equal(display({...duplicate,sourceV:12.5},tick+1000),before,'duplicate samples do not advance the filter');
for(let i=0;i<30;i++){tick+=2000;display(reading(11.1),tick);}
assert.equal(display(reading(11.1),tick+2000),11.1,'a sustained real change must remain visible');
assert.equal(display(reading(10.6),tick+4000),10.6,'low voltage bypasses presentation smoothing');
assert.equal(display({...reading(11.8),sampleValid:false},tick+6000),null);
assert.equal(display(reading(11.8),tick+8000),11.8,'reconnection seeds a current value');
assert.equal(display({...reading(11.8),sampleAgeMs:6000},tick+10000),null,'stale data must be unavailable');
const raw={...power,sourceV:10.6,lowBatteryVoltage:true};html=renderModule('power',{power:raw});
assert(html.includes('Source voltage (smoothed)')&&html.includes('Source voltage (raw)')&&html.includes('10.60'));
assert(html.includes('Battery voltage is low'));
console.log('PASS voltage presentation: observed full-charge noise settles, real decline follows, low voltage is immediate, duplicate/stale readings cannot refresh, reconnect resets, raw telemetry and percentage untouched');

const {piezoStatus}=await import(pathToFileURL(path.join(temp,'developer/sensing.js')));
assert.equal(piezoStatus().front,'Unavailable');
assert.equal(piezoStatus({online:true,piezoConfigured:false,reportedPiezoFront:0}).front,'Unavailable');
const live=piezoStatus({online:true,piezoConfigured:true,reportedPiezoFront:321,reportedPiezoRear:0});
assert.equal(live.front,'321');assert.equal(live.rear,'0');assert.equal(live.valid,true);
assert.equal(piezoStatus({online:false,piezoConfigured:true,reportedPiezoFront:321}).front,'Unavailable');
console.log('PASS piezo view model: offline/unready unavailable, valid zero preserved, front/rear independent, no IMU dependency');
