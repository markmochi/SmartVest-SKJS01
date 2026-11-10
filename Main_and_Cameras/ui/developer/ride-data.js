export const RIDE_MS=20*60*1000;
export const phTime=ms=>new Date(ms+8*3600000).toISOString().replace('Z','+08:00');
const request=r=>new Promise((resolve,reject)=>{r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);});
export async function openRideStore(){
 const q=indexedDB.open('smartvest-ride-test-v1',1);
 q.onupgradeneeded=()=>{const d=q.result;d.createObjectStore('rides',{keyPath:'id'});const s=d.createObjectStore('records',{keyPath:['ride','n']});s.createIndex('ride','ride');};
 const db=await request(q);
 async function commit(meta,rows=[],claim=false){
  return new Promise((resolve,reject)=>{
   const tx=db.transaction(['rides','records'],'readwrite'),rides=tx.objectStore('rides');let reason;
   const get=rides.getAll();get.onsuccess=()=>{const old=get.result.find(r=>r.id===meta.id);
    if(get.result.some(r=>r.id!==meta.id&&r.state==='recording'&&r.owner!==meta.owner&&Date.now()-r.savedAt<6000)){reason=new Error('Another Ride Test page is recording on this phone.');tx.abort();return;}
    if(old&&old.owner!==meta.owner&&old.state==='recording'&&Date.now()-old.savedAt<6000){reason=new Error('This ride is recording in another page. Close that page before continuing here.');tx.abort();return;}
    if(old&&!claim&&old.owner!==meta.owner){reason=new Error('This ride was opened in another page. Recording stopped here.');tx.abort();return;}
    for(const row of rows)tx.objectStore('records').add(row);
    rides.put({...meta,savedAt:Date.now()});
   };
   tx.oncomplete=()=>resolve();tx.onabort=()=>reject(reason||tx.error||new Error('The phone could not save the recording.'));tx.onerror=()=>{};
  });
 }
 return {commit,async list(){return (await request(db.transaction('rides').objectStore('rides').getAll())).sort((a,b)=>b.start-a.start);},async rows(id){return request(db.transaction('records').objectStore('records').index('ride').getAll(id));},close(){db.close();}};
}
export function flatten(value,prefix='',out={}){
 if(value===null||value===undefined){if(prefix)out[prefix]='';return out;}
 if(typeof value!=='object'){out[prefix]=value;return out;}
 for(const [k,v] of Object.entries(value))flatten(v,prefix?prefix+'.'+k:k,out);
 return out;
}
const vector=(a,n)=>Array.isArray(a)?a.map(v=>Number(v)/n):[];
export function motion(data){
 const i=data.imu||{},s=i.sample||{},c=i.calibration||{},p=i.profile||{},a=vector(s.accelRaw,p.accelLsbPerG||2048),g=vector(s.gyroRaw,p.gyroLsbPerDps||16.384),bias=c.gyroBiasDps||[0,0,0];
 const corrected=g.map((v,n)=>v-((c.flags&1)?Number(bias[n]||0):0));
 return {accelerationG:a,rawGyroDps:g,correctedGyroDps:corrected,accelerationMagnitudeG:a.length===3?Math.hypot(...a):null,rotationMagnitudeDps:g.length===3?Math.hypot(...corrected):null,temperatureC:s.temperatureValid?s.temperatureRaw/(p.temperatureLsbPerC||512)+(p.temperatureOffsetC??23):null};
}
export function rideTables(meta,records){
 const tables=new Map(),add=(name,base,value)=>{const row={...base,...flatten(value)};if(!tables.has(name))tables.set(name,[]);tables.get(name).push(row);};
 const first={};
 for(const r of records.sort((a,b)=>a.n-b.n)){
  const base={'Phone time PH (UTC+08)':phTime(r.at),'Elapsed (s)':Math.round(r.elapsed)/1000,'Request duration (ms)':r.latency??'','Record number':r.n};
  if(r.kind==='fast'){
   if(r.imuNew){const i=r.data.imu||{},age=i.diagnostics?.uartLabLastAcceptedAgeMs;add('IMU',base,{...i,engineering:motion(r.data),sourceAgeMs:age,fresh:i.online&&i.healthy&&Number.isFinite(age)&&age+r.latency<650});}
   if(r.piezoNew)add('Piezo',base,r.data.piezo||{});
  }else if(r.kind==='status'){
   for(const [name,key] of [['SIM800L','gsm'],['NEO6M','gps'],['INA226','power']])add(name,base,r.data[key]||{});
   add('Main network',base,{firmware:r.data.firmwareVersion,uptimeSeconds:r.data.uptimeSeconds,freeHeapBytes:r.data.freeHeapBytes,minimumFreeHeapBytes:r.data.minimumFreeHeapBytes,largestHeapBlockBytes:r.data.largestHeapBlockBytes,network:r.data.network});
   (r.data.cameras||[]).forEach(c=>add('Cameras',base,c));
  }else if(r.kind==='camera')for(const c of r.data.cameras||[])add('Camera details',base,c);
  else if(r.kind==='trace')for(const e of r.data.entries||r.data.recent||[]){const key=(e.atMs??e.at)+':'+e.direction+':'+e.text;if(first[key])continue;first[key]=true;add('Modem trace',base,e);}
  else add('Moments',base,r.data);
 }
 const summary=[['Session',meta.id],['Dashboard address',meta.source||'Not recorded'],['Simulated preview',Boolean(meta.simulated)],['Started PH',phTime(meta.start)],['Finished PH',meta.end?phTime(meta.end):''],['State',meta.state],['Requested minutes',20],['Recorded seconds',(meta.elapsed||0)/1000],['Saved records',records.length],['Ride notes',meta.notes||''],['IMU mode',meta.mixedPreview?'Mixed preview cadence; see start/resume moments':meta.previewIntervalMs?'Decimated live preview, up to about '+(1000/meta.previewIntervalMs)+' Hz; not full 200 Hz raw acquisition':'Decimated live preview; sampling cadence not saved by this older recorder'],['GPS and module interval','2 seconds, subject to connection response time'],['Camera detail interval','5 seconds'],['Modem trace interval','10 seconds; bounded catch-up pages'],['Clock','Phone clock displayed in Philippine time (UTC+08); GPS UTC kept separately'],['Storage','This browser/origin on this phone; committed about once per second'],['Monitoring','Existing crash monitoring and alert settings are unchanged'],['Limits','Missing readings are not invented; keep the page open and phone awake.'],['Source','SmartVest integrated v1.0 live telemetry']];
 const quality=[['IMU rows',tables.get('IMU')?.length||0],['Piezo rows',tables.get('Piezo')?.length||0],['GPS rows',tables.get('NEO6M')?.length||0],['Network/visibility/clock moments',tables.get('Moments')?.length||0],['Result',meta.error||meta.state],['Interpretation','Acquisition sequence jumps are expected in the decimated live feed; inspect sensor gap/failure counters separately.'],['Saved metadata',JSON.stringify(meta.quality||{})]];
 const order=['IMU','Piezo','SIM800L','NEO6M','INA226','Cameras','Camera details','Modem trace','Main network','Moments'];
 for(const name of order.slice(0,6))if(!tables.has(name))tables.set(name,[]);
 return [{name:'Summary',headers:['Field','Value'],rows:summary},...order.filter(n=>tables.has(n)).map(name=>{const rows=tables.get(name),headers=rows.length?[...new Set(rows.flatMap(r=>Object.keys(r)))]:['Phone time PH (UTC+08)','Elapsed (s)','No observations received'];return {name,headers,rows:rows.map(r=>headers.map(k=>r[k]??''))};}),{name:'Data quality',headers:['Field','Value'],rows:quality}];
}
