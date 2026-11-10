const assert=require('node:assert/strict'),path=require('node:path'),{pathToFileURL}=require('node:url');
(async()=>{
 const m=await import(pathToFileURL(path.resolve(__dirname,'../../Main_and_Cameras/ui/developer/imu-data.js')));
 const profile={sensorModel:'BMI323',interface:'SPI',accelLsbPerG:2048,gyroLsbPerDps:16.384,temperatureLsbPerC:512,temperatureOffsetC:23,accConfig:0x70b9,gyroConfig:0x70c9};
 assert.equal(m.profileFor({schemaVersion:2,profile}),profile);assert.throws(()=>m.profileFor({schemaVersion:1,profile}));
 assert.throws(()=>m.profileFor({schemaVersion:2,profile:{...profile,gyroLsbPerDps:16.4}}));
 assert.equal(m.imuAvailability({},{}).status,'Sensing board offline');
 assert.match(m.imuAvailability({online:false},{online:true,imuReady:true}).status,/detailed live readings unavailable/);
 const state=m.boardCommunication;
 assert.equal(state({},{}).title,'Not communicating');
 assert.equal(state({online:true,healthy:true},{online:true,twoWayConfirmed:true}).title,'Communicating');
 assert.equal(state({online:true,healthy:false},{online:true,twoWayConfirmed:true}).title,'Communicating');
 assert.match(state({online:true,healthy:false},{online:true}).description,/motion sensor needs attention/);
 assert.equal(state({online:false},{online:true}).title,'Connection incomplete');
 assert.equal(state({online:true},{online:true},true).title,'Status unavailable');
 assert.equal(state({},{enabled:false}).title,'Communication disabled');
 assert.equal(m.verifyTrial,undefined);assert.equal(m.trialCsv,undefined);
 console.log('PASS compatible live profile, independent board/sensor states, partial link, stale data, disabled link and no recorder exports');
})().catch(e=>{console.error(e);process.exitCode=1;});
