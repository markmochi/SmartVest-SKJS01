const assert=require('node:assert/strict'),path=require('node:path'),{pathToFileURL}=require('node:url');
(async()=>{
 const {incidentRecords,incidentDate}=await import(pathToFileURL(path.resolve(__dirname,'../../Main_and_Cameras/ui/shared/incidents.js')));
 const row={boot:42,sequence:1,occurredAtUtcMs:Date.parse('2026-10-28T12:01:02Z'),timeBasis:'estimated_detection',movement:{recorded:true,accelerationG:2.8,rotationDps:156},location:{state:'no_fix'},mobile:{state:'not_detected',service:'sim_unavailable'},impact:{recorded:true,front:true,rear:false},cameraPresence:{front:'detected',rear:'not_detected'},front:'protected_reported',rear:'pending',battery:{state:'detected',percent:60,voltage:11.2},recipients:[{name:'Jamie <script>',phone:'+639170000001',state:'waiting'}]};
 assert.equal(incidentDate(row),'October 28, 2026 | 8:01:02 PM');
 assert.equal(incidentDate({occurredAtUtcMs:null}),'Date and time unavailable');assert.equal(incidentDate({sensorUptimeMs:64000}),'Date and time unavailable');
 const admin=incidentRecords([row],{admin:true}),rider=incidentRecords([row]);
 assert.match(admin,/Crash detected/);assert.match(admin,/NEO-6M/);assert.match(admin,/SIM800L/);assert.match(admin,/2\.80/);assert.match(admin,/Recording protected/);assert.match(admin,/Not detected/);assert.match(admin,/Impact detected/);
 assert(!/NEO-6M|SIM800L|INA226|ESP32|UART|uptime|sequence|sensor boot/i.test(rider));
 assert(!rider.includes('<script>'));assert(rider.includes('&lt;script&gt;'));
 assert(!incidentRecords([{...row,diagnostic:true}]).includes('incident-card'));
 assert(incidentRecords([{...row,impact:{recorded:true,front:false,rear:false}}]).includes('No qualifying impact recorded'));
 const absent=incidentRecords([{...row,rear:'failed_or_timeout',recipients:[],noRecipients:true}]);
 assert(!absent.includes('Recording incomplete'),'an absent camera stays simply not detected');
 assert.match(absent,/>Not detected<\/span>/);
 const legacy=incidentRecords([{boot:2,sequence:1,front:'pending',rear:'pending',recipients:[]}]);assert.match(legacy,/Date and time unavailable/);assert.match(legacy,/not recorded/i);
 assert.match(incidentRecords([row],{loaded:false}),/storage needs attention/);
 for(const state of ['waiting','sending','accepted','delivered','unknown','failed','undelivered','cancelled']){
  const text=incidentRecords([{...row,recipients:[{phone:'<img onerror=x>',state}]}]);assert(!text.includes('<img onerror=x>'));assert(!/undefined|NaN/.test(text));
 }
 console.log('PASS persisted-time formatting, honest legacy/missing-module states, event readings, outcome distinctions, rider wording and escaped untrusted text');
})().catch(e=>{console.error(e);process.exitCode=1;});
