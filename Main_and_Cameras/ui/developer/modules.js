import {esc,known,num,age,yes,badge,kv,metrics,panel,notice,positionBlock} from '../shared/dom.js';
import {icon} from '../shared/icons.js';
function batteryIssue(p){
 const reasons={2:'INA226 did not reply',3:'INA226 rejected a read request',5:'INA226 communication timed out',6:'Unexpected sensor identity',7:'INA226 setup failed',8:'INA226 settings mismatch',9:'Measurement overflow',10:'Reading outside the expected battery range',11:'Incomplete INA226 reading',12:'INA226 read timed out'};
 return reasons[p.nanoI2cError]||'Waiting for a valid battery reading';
}
export function moduleStates(s={}){
 const g=s.gsm||{},gps=s.gps||{},sensor=s.sensing||{},p=s.power||{},n=s.network||{};
 const recentGsm=known(g.rxAgeMs)&&g.rxAgeMs<5000;
 const sensingReady=sensor.online&&sensor.armed&&sensor.imuReady&&sensor.piezoConfigured&&sensor.outboxWritable&&sensor.pending<(sensor.outboxCapacity||4);
 const missingSensors=[sensor.imuReady===true?'':'IMU',sensor.piezoConfigured===true?'':'Piezo'].filter(Boolean);
 const sensingLabel=missingSensors.length?'Connected / '+missingSensors.join(' & ')+' not ready':'Connected';
 const detectionLabel=!sensor.outboxWritable?'Event storage not ready':sensor.pending>=(sensor.outboxCapacity||4)?'Event queue full':!sensor.armed?'Detection not armed':'Detection armed';
 const cameras=Array.isArray(s.cameras)?s.cameras:[],linked=cameras.filter(c=>c.online).length,currentCameras=cameras.filter(c=>c.online&&c.statusFresh===true).length;
 return [
 {key:'gsm',name:'SIM800L',label:g.enabled===undefined?'Not reported':g.enabled===false?'Disabled':g.simReady&&g.readyToSend?'SMS ready':g.online?(g.registration===3?'Registration denied':(!g.simReady?g.simStatus||'Checking SIM card':g.registrationText||'Checking registration')):recentGsm?'RX data / no reply':'No UART reply',tone:g.enabled===undefined||g.enabled===false?'neutral':g.simReady&&g.readyToSend?'good':g.online||recentGsm?'warn':'bad',evidence:g.enabled===false?'Disabled':g.online?'AT replies received':recentGsm?'RX bytes received':'No recent AT reply'},
 {key:'gps',name:'NEO-6M GPS',label:gps.enabled===undefined?'Not reported':gps.enabled===false?'Disabled':!gps.receiving?'No recent data':!gps.decoding?'Data not decoding':gps.state==='fresh'?'Position acquired':gps.state==='stale'?'Last known position':'Receiving / no fix',tone:gps.enabled===undefined||gps.enabled===false?'neutral':gps.state==='fresh'?'good':gps.receiving?'warn':'bad',evidence:gps.enabled===false?'Disabled':gps.decoding?'Valid NMEA received':gps.receiving?'UART bytes received':'No recent GPS bytes'},
 {key:'sensing',name:'Sensing board',label:sensor.enabled===undefined?'Not reported':sensor.enabled===false?'Disabled':sensor.online?sensingLabel:'No heartbeat',tone:sensor.enabled===undefined||sensor.enabled===false?'neutral':sensingReady?'good':sensor.online?'warn':'bad',evidence:sensor.enabled===false?'Disabled':sensor.online?'Heartbeat received / '+detectionLabel:sensor.rxBytes?'RX data / no recent heartbeat':'No valid heartbeat'},
 {key:'power',name:p.sensorModel||'INA226',label:p.enabled===undefined?'Not reported':p.enabled===false?'Disabled':p.sampleValid?'Measuring':p.responding?'Responding / no sample':p.nanoConnected?'Nano connected / monitor unavailable':p.viaSensing?'No battery telemetry':'No I2C response',tone:p.enabled===undefined||p.enabled===false?'neutral':p.sampleValid?'good':p.responding||p.nanoConnected?'warn':'bad',evidence:p.enabled===false?'Disabled':p.sampleValid?(p.viaSensing?'Fresh Nano sample':'Fresh register sample'):p.nanoConnected?batteryIssue(p):p.responding?'Address responding':'No valid sample'},
 {key:'cameras',name:'Cameras',label:currentCameras+' / 2 current',tone:currentCameras===2?'good':'warn',evidence:linked&&!currentCameras?'Camera reports delayed':linked>currentCameras?'Some camera reports delayed':linked?'Fresh authenticated heartbeats':'No recent camera heartbeat'},
 {key:'network',name:'Wi-Fi',label:n.stationConnected?'Connected':n.hotspotActive?'Hotspot active':n.connecting?'Connecting':'Offline',tone:n.stationConnected||n.hotspotActive?'good':n.connecting?'warn':'bad',evidence:n.mode==='home_wifi_only'?'Home network':n.mode==='vest_hotspot'?'Vest hotspot':'Mode not reported'}
 ];
}
function pin(value){return Number.isInteger(value)?'GPIO'+value:'Unknown';}
function stateFor(s,key){return moduleStates(s).find(row=>row.key===key);}
function uartErrors(e){return e?num(e.total)+' total · '+num((e.fifoOverflow||0)+(e.bufferFull||0))+' overflow · '+num(e.framing)+' frame · '+num(e.parity)+' parity':'Not reported';}
function summary(state,title){return `<div class="diagnostic-summary" data-live><div class="large-icon">${icon(state.key)}</div><div>${badge(state.label,state.tone)}<h2>${esc(title)}</h2></div></div>`;}
function moduleSection(state,title,body){return `<section class="panel">${summary(state,title)}<div class="panel-body" data-live>${body}</div></section>`;}
function details(key,title,body){return `<details class="details-panel" data-key="${key}"><summary>${esc(title)}</summary>${body}</details>`;}
function wire(lines){return `<div class="wiring">${lines.map(([from,to])=>`<div class="wire-line"><span>${esc(from)}</span><span class="flow" aria-hidden="true">→</span><span>${esc(to)}</span></div>`).join('')}</div>`;}
function gsmPage(s){
 const g=s.gsm||{},state=stateFor(s,'gsm');
 const registration=g.simReady?({0:'Not registered',1:'Registered (home)',2:'Searching',3:'Registration denied',4:'Unknown',5:'Registered (roaming)'})[g.registration]||'Not reported':'Not confirmed';
 const carrier=g.simReady&&g.registered?(g.operator||'Checking network'):'Not registered';
 const number=g.simReady?(g.subscriberNumber||g.subscriberNumberStatus||'Not available'):'Unavailable';
 let action='';
 if(g.enabled===false)action='Enable SIM800L in Config.h after connecting it.';
 else if(!g.online)action='Check modem power, common ground, baud and crossed TX/RX wiring.';
 else if(g.simStatus==='SIM card not detected')action='Power the modem off before reseating the SIM, then restart it.';
 else if(g.simStatus?.includes('PIN required'))action='The SIM is detected but locked. Disable its PIN on a phone before using it here.';
 else if(g.simStatus?.includes('PUK required'))action='The SIM is detected but locked. Use the carrier-provided PUK on a phone.';
 else if(!g.simReady)action=g.simDetail||'Wait for the SIM status query.';
 else if(g.registration===3)action='The SIM is readable, but the network denied registration. Check SIM activation and local 2G service.';
 else if(!g.registered)action='The SIM is ready; the modem is still waiting for a cellular network.';
 else if(!g.readyToSend)action='SIM and network are available; SMS setup is still in progress.';
 return `<div class="split">${moduleSection(state,'SIM & mobile network',kv([['SIM card',g.simStatus||'Checking'],['SIM carrier',g.simReady?(g.simCarrier||'Checking SIM'):'Unavailable'],['Current network',carrier],['SIM phone number',number],['Registration',registration],['Signal',known(g.csq)&&g.csq>=0&&g.csq<=31?g.csq+' / 31':'Unknown'],['SMS',yes(g.simReady&&g.readyToSend,'Ready','Not ready')]])+(action?`<p class="hint section-gap">${esc(action)}</p>`:''))}${panel('UART activity',kv([['Modem replies',g.online?'Received':'No verified reply'],['RX bytes',num(g.rxBytes)],['TX bytes',num(g.txBytes)],['AT commands',num(g.commandsSent)],['Replies',num(g.repliesReceived)],['Last RX',age(g.rxAgeMs)],['Last reply',age(g.responseAgeMs)]])+'<div class="actions"><a class="button secondary" href="/admin/activity">Message history</a></div>')}</div>`+
 details('gsm-details','SIM, UART & modem details',kv([['Raw CPIN state',g.sim||'Not reported'],['SIM identity',g.iccidStatus||'Not queried'],['ICCID ending',g.simReady&&g.iccidLast4?'•••• '+g.iccidLast4:'Unavailable'],['SIM operator code',g.simReady?(g.simCarrierCode||'Unavailable'):'Unavailable'],['Network operator code',g.simReady&&g.registered?(g.operatorCode||'Unavailable'):'Unavailable'],['Access technology',g.simReady&&g.registered&&known(g.accessTechnology)&&g.accessTechnology>=0?(g.accessTechnology===0?'GSM':String(g.accessTechnology)):'Unavailable'],['Port','UART'+(g.uart??2)],['Baud',num(g.baud)],['UART errors',uartErrors(g.uartErrors)],['Last TX',age(g.txAgeMs)],['Last command',g.lastCommand||'None'],['Last error',g.lastError||'None'],['Command timeouts',num(g.commandTimeouts)],['Stage',g.stage||'Idle'],['Last registration reply',g.lastRegistrationResponse||'None'],['Last SMS error',g.lastSmsError||'None'],['Last power event',g.lastPowerEvent||'None'],['Recovery attempts',num(g.synchronizationRecoveries)],['Modem restarts',num(g.modemRestarts)],['Delivery reports',num(g.deliveryReportsReceived)],['Matched reports',num(g.deliveryReportsMatched)],['Service centre',g.serviceCenter||'Not reported']])+wire([['SIM800L TXD','Main RX '+pin(g.rxPin)],['Main TX '+pin(g.txPin),'SIM800L RXD']]));
}
function gpsPage(s){
 const g=s.gps||{};
 return `<section class="panel"><div class="panel-head"><h2>Coordinates</h2></div>${positionBlock(g)}</section>`+
 `<div class="section-gap">${metrics([['RX bytes',num(g.rxBytes)],['Valid sentences',num(g.validSentences)],['Satellites used',num(g.satellites)],['Checksum errors',num(g.checksumFailures)]])}</div>`+
 details('gps-details','UART & fix details',kv([['Port','UART'+(g.uart??1)],['Baud',num(g.baud)],['Receiving bytes',yes(g.receiving)],['NMEA decoding',yes(g.decoding)],['Last RX',age(g.lastByteAgeMs)],['Last valid sentence',age(g.lastSentenceAgeMs)],['UART errors',uartErrors(g.uartErrors)],['Satellites visible',num(g.satellitesInView)],['HDOP',num(g.hdop,1)],['GPS UTC',g.utc||'Unavailable'],['USB log baud',num(s.serialMonitor?.baud)]])+wire([['GPS TX','Main RX '+pin(g.rxPin)],['UART1 TX '+pin(g.consoleTxPin),g.consoleTxPin>=0?'USB serial bridge':'Unused']])+'<p class="hint">GPS RX is unused. GPIO1 is reserved for USB logs.</p>');
}
// Browser presentation only. Raw telemetry and Nano's percentage are untouched.
export function createVoltageDisplay(){
 let samples=[],lastAt=null,lastSequence=null,filtered=0,shown=0;
 return (p,now=Date.now())=>{
  if(!p.sampleValid||!known(p.sourceV)||known(p.sampleAgeMs)&&p.sampleAgeMs>=6000){samples=[];lastAt=null;lastSequence=null;return null;}
  const value=p.sourceV;
  if(lastAt===null||now-lastAt>=6000||value<=10.8){samples=[value];filtered=value;shown=Math.round(value*10)/10;}
  else {
   if(known(p.nanoSequence)&&p.nanoSequence===lastSequence)return shown;
   samples.push(value);if(samples.length>5)samples.shift();
   const median=[...samples].sort((a,b)=>a-b)[Math.floor(samples.length/2)],elapsed=Math.max(0,now-lastAt);
   filtered+=(median-filtered)*elapsed/(6000+elapsed);
   if(Math.abs(filtered-shown)>=.08)shown=Math.round(filtered*10)/10;
  }
  lastAt=now;lastSequence=p.nanoSequence;return shown;
 };
}
const voltageDisplay=createVoltageDisplay();
function powerPage(s){
 const p=s.power||{},state=stateFor(s,'power');
 return `<a class="text-link" href="/admin/tools#led-test">Test the vest indicator light</a>`+moduleSection(state,'Power monitor',(p.nanoConnected&&!p.sampleValid?notice('Nano is communicating. '+batteryIssue(p)+'.','warn'):'')+(p.sampleValid&&p.lowBatteryVoltage?notice('Battery voltage is low. Charge the vest before riding.','warn'):'')+kv([['Source voltage (smoothed)',num(voltageDisplay(p),1,' V')],['Battery estimate',known(p.batteryPercent)?Math.round(p.batteryPercent)+'%':'Unavailable'],['Charging status',!p.sampleValid?'Unavailable':p.charging===true?'Charging'+(p.chargingEstimated===true?' (estimated)':''):p.charging===false?'Not charging'+(p.chargingEstimated===true?' (estimated)':''):'Unavailable'],['Last sample',age(p.sampleAgeMs)]]))+
 details('power-details' ,'Connection & estimate details',kv([['Source voltage (raw)',num(p.sourceV,2,' V')],['Bus voltage (raw)',num(p.busV,2,' V')],['Current',num(p.currentMA,1,' mA')],['Power',num(p.powerMW,1,' mW')],['Address',known(p.address)?'0x'+p.address.toString(16).toUpperCase():'Unknown'],['Nano serial link',yes(p.nanoConnected,'Connected','Not receiving updates')],['Device responding',yes(p.responding)],['Fresh sample',yes(p.sampleValid)],['Samples',num(p.samples)],['Failures',num(p.failures)],['Retries',num(p.retries)],['Pack calibration',yes(p.batteryCalibrated)],['Estimate method',p.estimateMethod||'Not reported'],['Net current (+ charging)',num(p.netCurrentMA,0,' mA')],['Path','INA226 → Nano → Sensing → Main'],['Nano UART',num(p.nanoBaud)+' baud'],['Nano I2C error',num(p.nanoI2cError)],['Battery-pack monitor',yes(p.monitorsBatteryPack)]])+wire([['Nano A4','INA226 SDA'],['Nano A5','INA226 SCL'],['Nano D1 TX through 5 V → 3.3 V divider','Sensing RX '+pin(p.sensingRxPin)]]));
}
function networkPage(s){
 const n=s.network||{},state=stateFor(s,'network');
 return moduleSection(state,'Wi-Fi connection',kv([['Mode',n.mode==='home_wifi_only'?'Home Wi-Fi':n.mode==='vest_hotspot'?'Vest hotspot':'Unknown'],['IP address',n.ip||'Not assigned'],['Address',n.dashboardUrl||'Unavailable'],['Hostname',n.hostname?n.hostname+'.local':'Unavailable'],['Signal',num(n.signalDbm,0,' dBm')],['Last issue',n.error||'None']]))+
 details('network-details','Connection details',kv([['mDNS',yes(n.mdnsActive)],['Hotspot',yes(n.hotspotActive)],['Captive portal',yes(n.captivePortalActive)],['Attempts',num(n.attempts)],['Next retry',num(n.retryInMs,0,' ms')],['Hotspot clients',n.hotspotActive?num(s.stations):'Not applicable']]));
}
function systemPage(s){
 const m=s.serialMonitor||{},resets={1:'Power-on',2:'External',3:'Software',4:'Panic',5:'Interrupt watchdog',6:'Task watchdog',7:'Watchdog',8:'Deep sleep',9:'Brownout',10:'SDIO'};
 return panel('Network / HTTP health',kv([['AP starts / stops',num(s.radio?.apStarts)+' / '+num(s.radio?.apStops)],['Client joins / leaves',num(s.radio?.clientJoins)+' / '+num(s.radio?.clientLeaves)],['Connected clients',num(s.radio?.clients)],['HTTP requests',num(s.http?.requests)],['Expired / rejected requests',num(s.http?.expired)+' / '+num(s.http?.rejected)],['Waiting connections',num(s.http?.waiting)],['Longest HTTP handler',num(s.http?.maxHandleMs,0,' ms')]]))+metrics([['Uptime',num(s.uptimeSeconds,0,' s')],['Free heap',num(s.freeHeapBytes,0,' B')],['Lowest heap',num(s.minimumFreeHeapBytes,0,' B')],['Largest block',num(s.largestHeapBlockBytes,0,' B')]])+
 `<div class="split">${panel('Main ESP32',kv([['Reset reason',resets[s.resetReason]||'Unknown'],['Settings',yes(s.storageHealthy,'Loaded','Unavailable')],['Settings writes',yes(s.storageWritable,'Available','Unavailable')],['Settings error',s.storageError||'None'],['Incident storage',s.emergency?.loaded&&s.emergency?.writable?'Available':'Unavailable'],['Incident error',s.emergency?.error||'None']])+'<div class="actions"><button id="retryStorage" class="secondary" data-write>Retry settings write</button></div>')}${panel('Serial Monitor',kv([['Logging',yes(m.enabled,'Enabled','Disabled')],['Baud',num(m.baud)],['Output','UART1 TX · '+pin(m.txPin)+' · USB'],['Output policy','State changes only'],['TX bytes',num(m.txBytes)],['Dropped lines',num(m.droppedLines)]])+'<p class="hint section-gap">Arduino: Core Debug Level = None.</p>')}</div>`;
}
export function renderModule(page,s){return ({gsm:gsmPage,gps:gpsPage,power:powerPage,network:networkPage,system:systemPage})[page]?.(s)||notice('Page unavailable.','bad');}
