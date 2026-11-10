import {api,readApi,session} from './api.js';
import {$,esc,kv,renderContent,confirmAction,toast} from './dom.js';
import {startPolling} from './poller.js';

const date=value=>Number(value)>=1577836800000&&Number(value)<4102444800000?new Date(Number(value)).toLocaleString('en-PH',{timeZone:'Asia/Manila',dateStyle:'medium',timeStyle:'medium'}):'Recording time unavailable';
const space=value=>Number.isFinite(Number(value))&&value!=null?(Number(value)/1073741824).toFixed(2)+' GB':'Not available';
const name=role=>role==='rear'?'Rear':'Front';
const deletions={
 3:['normal clips','DELETE NORMAL','Protected clips are kept.'],
 4:['all saved clips','DELETE ALL','This includes all protected clips.'],
 7:['all interrupted recordings','DELETE INTERRUPTED','This includes unfinished recordings and recovery copies. Incident clips are kept.']
};
let downloadFrame=null;
const isTransfer=value=>[1,2,5,6].includes(Number(value?.state));
export function mountFileLibrary(context,root='cameraLibrary'){
 const state={};let transfer=null;
 const downloading=()=>isTransfer(transfer);
 $(root).innerHTML=`<p class="hint">Clips stay on each camera’s memory card. Times use Philippine time. Missing times were not saved. The current recording is not listed.</p><p class="hint">Download while parked, one clip at a time. Keep the vest on and connected. Recording pauses on that camera and resumes afterward.</p><div id="cameraDownloadStatus" role="status"></div>`+['front','rear'].map(role=>`<section class="panel section-gap" data-library="${role}"><div class="panel-head"><h2>${name(role)} camera recordings</h2></div><div class="panel-body"><div id="${role}Storage"><p>Connecting to the camera…</p></div>${[1,2].map(kind=>`<details data-key="${role}-files-${kind}" id="${role}Files${kind}"><summary>${kind===1?'Normal clips':'Protected clips'}</summary><div id="${role}List${kind}" class="section-gap"><p>Open this list to view saved clips.</p></div><div class="actions"><button type="button" class="secondary" data-file-action="back" data-kind="${kind}" disabled>Back</button><span id="${role}Page${kind}" class="hint">Page 1</span><button type="button" class="secondary" data-file-action="next" data-kind="${kind}" disabled>Next</button><button type="button" class="secondary" data-file-action="refresh" data-kind="${kind}">Update list</button></div></details>`).join('')}<details data-key="${role}-manage"><summary>Manage saved recordings</summary><p class="hint">Delete while parked. Keep the vest on and save clips you need first.</p><div class="actions">${[3,7,4].map(op=>`<button type="button" class="${op===4?'danger':'secondary'}" data-delete="${op}" data-write disabled>Delete ${deletions[op][0]}</button>`).join('')}</div></details><p id="${role}FileStatus" class="hint" role="status"></p></div></section>`).join('');
 function showTransfer(){
  const messages={1:'Waiting for your browser…',2:'Downloading…',3:'Transfer sent. Check your downloads.',4:'Download stopped. Try again from the clip list.',5:'Preparing download…',6:'Stopping download…'};
  const value=Number(transfer?.state)||0,percent=transfer?.size?Math.min(100,Math.floor(Number(transfer.sent)*100/Number(transfer.size))):0;
  renderContent(value?`<section class="notice ${value===3?'good':'neutral'}"><div><strong>${esc(name(transfer.camera))} camera</strong><p>${esc(messages[value]||'')}${value===2?' '+percent+'% sent.':''}</p>${downloading()&&transfer.canCancel?`<div class="actions"><button type="button" class="secondary" id="cancelClipDownload" data-cancel-download ${value===6?'disabled':''}>Cancel download</button></div>`:''}</div></section>`:'','cameraDownloadStatus');
  for(const value of Object.values(state))value.refreshControls?.();
 }
 $(root).addEventListener('click',async event=>{
  const button=event.target.closest('[data-cancel-download]');if(!button||button.disabled||!downloading())return;
  button.disabled=true;
  try{await api('cameras/files?camera='+transfer.camera,'POST',{operation:6,id:transfer.id},{signal:context.signal});if(context.isCurrent()){transfer.state=6;showTransfer();}}
  catch(error){if(context.isCurrent()&&error.name!=='AbortError')toast(error.message,true);}
 });
 const wait=()=>new Promise((resolve,reject)=>{const stop=()=>{clearTimeout(timer);reject(new DOMException('Page changed','AbortError'));};const timer=setTimeout(()=>{context.signal?.removeEventListener('abort',stop);resolve();},1500);context.signal?.addEventListener('abort',stop,{once:true});});
 for(const role of ['front','rear']){
  const container=$(root).querySelector(`[data-library="${role}"]`);
  const s=state[role]={busy:false,ready:false,stamp:'',updated:0,confirming:false,downloads:false,cleanup:false,pages:{1:{stack:[null],rows:[],more:false,loaded:false},2:{stack:[null],rows:[],more:false,loaded:false}}};
  const status=text=>{$(role+'FileStatus').textContent=text;};
  function controls(){
   for(const button of container.querySelectorAll('button')){
    if(button.hasAttribute('data-write')){const blocked=s.busy||s.confirming||!s.ready||downloading()||(button.dataset.delete==='7'&&!s.cleanup);button.dataset.blocked=String(blocked);button.disabled=blocked||!session.connected;if(button.dataset.delete==='7')button.title=s.cleanup?'':'Update this camera to clear interrupted recordings.';}
    else {const p=s.pages[button.dataset.kind];button.disabled=s.busy||!s.ready||downloading()||(button.dataset.fileAction==='back'&&p.stack.length===1)||(button.dataset.fileAction==='next'&&!p.more);}
   }
  }
  s.refreshControls=controls;
  async function job(body){
   const path='cameras/files?camera='+role,controller=new AbortController();
   let expired=false;
   const stop=()=>controller.abort(),timer=setTimeout(()=>{expired=true;stop();},125000);
   context.signal?.addEventListener('abort',stop,{once:true});if(context.signal?.aborted)stop();
   const options={signal:controller.signal};
   async function finish(id){
    while(context.isCurrent()&&!controller.signal.aborted){
     await wait();
     const result=await readApi(path+'&job='+id,{...options,onRetry:()=>status('Reconnecting to check the camera request…')});if(!result.pending)return result;
    }
    throw new DOMException('Page changed','AbortError');
   }
   try{
    const current=await api(path,'GET',undefined,options);
    if(current.busy&&current.requestId){status('Finishing the previous camera request. Please wait…');await finish(current.requestId);}
    const accepted=await api(path,'POST',body,options);
    status(body.operation===5?'Preparing your download. Please wait…':body.operation>=3?'Deleting recordings. Please wait…':'Reading saved clips. Please wait…');
    return await finish(accepted.id);
   }catch(error){if(expired)throw new Error('The camera did not finish in time. Check its connection and update the list.');throw error;}
   finally{clearTimeout(timer);context.signal?.removeEventListener('abort',stop);}
  }
  async function load(kind,move='refresh'){
   if(s.busy||s.confirming||!s.ready||downloading()||!context.isCurrent())return;
   const p=s.pages[kind],previous=p.stack.slice();
   if(move==='back')p.stack.pop();
   if(move==='next'){const last=p.rows.at(-1);if(!last)return;p.stack.push({beforeSession:last.session,beforeStart:last.start,beforeName:last.name});}
   s.busy=true;controls();
   try{
    const result=await job({operation:kind,...p.stack.at(-1)});if(!context.isCurrent())return;
    if(result.error&&!result.clips?.length)throw new Error(result.error);
    p.rows=result.clips||[];p.more=Boolean(result.more);p.loaded=true;s.updated=Date.now();
    renderContent(p.rows.length?p.rows.map((row,index)=>`<article class="history-item"><h3>${esc(date(row.utcMs))}</h3><p>${row.protected?esc(({1:'Incident protection · crash alert or test',2:'Recording interruption · recovered footage'})[row.protectionReason]||'Protection reason unavailable'):'Normal clip'}</p>${row.protected?`<p class="hint">${row.protectionReason===2?'Recovered':'Protected'}: ${row.protectedUtcMs?esc(date(row.protectedUtcMs)):'time unavailable'}</p>`:''}${s.downloads?`<button type="button" class="secondary" data-write data-download="${kind}:${index}">Download</button>`:''}</article>`).join(''):'<p>No clips in this list.</p>',role+'List'+kind);
    $(role+'Page'+kind).textContent='Page '+p.stack.length;status(result.error||'List updated.');
   }catch(error){p.stack=previous;if(context.isCurrent()&&error.name!=='AbortError')status(error.message);}
   finally{s.busy=false;if(context.isCurrent())controls();}
  }
  for(const kind of [1,2])$(role+'Files'+kind).addEventListener('toggle',()=>{if($(role+'Files'+kind).open){$(role+'Files'+(kind===1?2:1)).open=false;load(kind);}});
  container.addEventListener('click',async event=>{
   const button=event.target.closest('button');if(!button||button.disabled)return;
   if(button.dataset.fileAction){load(Number(button.dataset.kind),button.dataset.fileAction);return;}
   if(button.dataset.download!==undefined){
    const [kind,index]=button.dataset.download.split(':').map(Number),row=s.pages[kind]?.rows[index];if(!row||downloading())return;
    s.confirming=true;controls();const confirmed=await confirmAction('Download this clip?',`Recording on the ${role} camera pauses during the download. Keep your vest on and stay connected.`,'Download clip');s.confirming=false;
    if(!confirmed||!context.isCurrent()||downloading()){controls();return;}
    s.busy=true;controls();
    try{
     const result=await job({operation:5,beforeName:row.name,beforeSession:row.session,beforeStart:row.start});if(!context.isCurrent())return;
     if(result.error)throw new Error(result.error);
     if(!result.downloadReady||!result.url)throw new Error('Download permission expired. Try again.');
     const url=new URL(result.url);
     if(url.protocol!=='http:'||url.port!=='8081'||!/^192\.168\.4\.\d+$/.test(url.hostname)||url.pathname!=='/clip'||!/^[a-f0-9]{32}$/.test(url.searchParams.get('token')||''))throw new Error('Download permission expired. Try again.');
     transfer={state:1,id:result.id,camera:role,size:result.downloadSize,sent:0,canCancel:true};showTransfer();
     downloadFrame?.remove();downloadFrame=document.createElement('iframe');downloadFrame.hidden=true;downloadFrame.title='Clip download';downloadFrame.setAttribute('sandbox','allow-downloads');downloadFrame.referrerPolicy='no-referrer';downloadFrame.src=url.href;document.body.append(downloadFrame);
     status('Download started. Check your browser’s downloads.');
    }catch(error){if(context.isCurrent()&&error.name!=='AbortError')status(error.message);}
    finally{s.busy=false;if(context.isCurrent())controls();}
    return;
   }
   const operation=Number(button.dataset.delete);if(!operation)return;
   s.confirming=true;controls();
   const confirmed=await confirmAction(`Delete ${deletions[operation][0]} from the ${role} camera?`,'Deletion is permanent. '+deletions[operation][2]+' The current recording is kept.','Delete recordings');
   s.confirming=false;if(context.isCurrent())controls();
   if(!confirmed||!context.isCurrent()||s.busy||!s.ready)return;
   s.busy=true;controls();
   try{
    const result=await job({operation,confirm:deletions[operation][1]});if(!context.isCurrent())return;
    status(`${result.deleted||0} recordings deleted.${operation===7?` ${result.recoveryFilesDeleted||0} recovery files cleared.`:''}${result.error?' '+result.error:''}`);
    for(const kind of [1,2]){s.pages[kind]={stack:[null],rows:[],more:false,loaded:false};$(role+'List'+kind).textContent='Update the list to view remaining recordings.';$(role+'Page'+kind).textContent='Page 1';}
    s.stamp='';s.updated=Date.now();toast(result.error?'Deletion needs attention. Check the camera message.':'Saved recordings deleted.',Boolean(result.error));
   }catch(error){if(context.isCurrent()&&error.name!=='AbortError')status(error.message+' Update the list before trying again.');}
   finally{s.busy=false;if(context.isCurrent())controls();}
  });
  s.update=data=>{
   const t=data.telemetry||data,age=data.heartbeatAgeMs??data.ageMs;
   s.ready=Boolean(data.online&&data.sdReady!==false&&data.sdReported!==false&&Number(age)<7000&&(t.filesSupported===1||t.filesSupported===2));
   s.downloads=t.downloadsSupported===1;s.cleanup=t.filesSupported===2;
   const known=Number.isFinite(t.normalRetained)&&Number.isFinite(t.lockedRetained);
   const recording=data.recordingReported??data.recording;
   const cardReady=data.sdReported??data.sdReady;
   const frameFresh=data.frameFresh??(Number(t.currentFrames)>0&&Number(t.frameAgeMs)<=2000);
   const cameraState=!data.online?'Disconnected':Number(age)>=7000?'Last report; checking connection':cardReady===false?'Memory card unavailable':[1,2].includes(Number(t.downloadState))?'Recording paused for download':recording===false?'Recording stopped':recording&&frameFresh?'Recording':'Checking recording';
   renderContent(kv([['Camera status',cameraState],['Saved clips',known?`${t.normalRetained+t.lockedRetained} total · ${t.normalRetained} normal · ${t.lockedRetained} protected`:'Not available yet'],['Latest saved recording',known&&t.normalRetained+t.lockedRetained===0?'No saved recordings':date(t.latestUtcMs)],['Memory card space',`${space(t.freeBytesAtBoundary)} free${t.totalBytes?' of '+space(t.totalBytes):''}`]])+`<p class="hint">${!data.online?'Camera disconnected. Showing its last report.':![1,2].includes(t.filesSupported)?'Update this camera’s firmware to browse and manage recordings.':!s.ready?'Waiting for a current memory card update.':'New clips replace the oldest normal clips. Protected clips stay until you choose to delete them.'}</p>`,role+'Storage');
   const stamp=[t.normalRetained,t.lockedRetained,t.latestUtcMs].join('/');
   const kind=[1,2].find(k=>$(role+'Files'+k).open);
   if(kind&&s.ready&&!s.busy&&!s.confirming&&!downloading()&&(!s.pages[kind].loaded||(Date.now()-s.updated>10000&&(stamp!==s.stamp||Date.now()-s.updated>60000)))){s.stamp=stamp;load(kind);}
   controls();
  };
 }
 return (cameras,download)=>{transfer=download||null;showTransfer();for(const data of cameras||[])state[data.id||data.camera]?.update(data);};
}
export function mountRiderCameras(context){
 $('pageContent').innerHTML='<div id="cameraLibrary"></div>';
 const update=mountFileLibrary(context);
 startPolling('cameras/files',data=>{update(data.cameras,data.download);window.dispatchEvent(new CustomEvent('veststatus',{detail:data}));},{interval:4000,onAge:()=>window.dispatchEvent(new Event('vestage'))});
}
