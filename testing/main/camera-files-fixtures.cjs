exports.handle=(state,req,url,body,json)=>{
 if(!state.fileLibrary)state.fileLibrary={jobs:{},next:1,rows:Object.fromEntries(['front','rear'].map(role=>[role,Array.from({length:13},(_,i)=>({name:`/clips/${role}_sample_${i}`,session:13-i,start:100,utcMs:i===0?0:1700000000000+(13-i)*60000,protected:i<6,protectionReason:i===0?2:i<6?1:0,protectedUtcMs:i===0?0:i<6?1700001000000:0}))]))};
 const store=state.fileLibrary,role=url.searchParams.get('camera'),job=url.searchParams.get('job');
 if(job)return json(store.jobs[job]||{error:'Request expired'},store.jobs[job]?200:409);
 if(req.method==='POST'){
  if(body.operation===6){if(!store.download||store.download.id!==body.id)return json({error:'This download has already ended.'},409);store.download.state=4;return json({ok:true});}
  if([1,2,5,6].includes(store.download?.state))return json({error:'One clip can download at a time. Wait for it to finish.'},409);
  if(body.operation===5){const id=store.next++,token='f'.repeat(32);store.download={id,state:1,camera:role,sent:0,size:262144,canCancel:true};store.jobs[id]={id,pending:false,downloadReady:true,downloadSize:262144,url:'http://192.168.4.'+(role==='rear'?3:2)+':8081/clip?token='+token,error:''};return json({id,pending:true},202);}

  const rows=store.rows[role];if(!rows)return json({error:'Choose camera'},400);
  let selected=rows.filter(r=>r.protected===(body.operation===2)),deleted=0;
  if(body.operation>=3){if(body.confirm!==(body.operation===4?'DELETE ALL':body.operation===7?'DELETE INTERRUPTED':'DELETE NORMAL'))return json({error:'Confirmation required'},400);const keep=body.operation===4?[]:body.operation===7?rows.filter(r=>r.protectionReason!==2):rows.filter(r=>r.protected);deleted=rows.length-keep.length;store.rows[role]=keep;selected=[];
   const status=state.cameraMonitor?.cameras?.find(camera=>camera.id===role)?.telemetry;
   if(status){status.normalRetained=keep.filter(row=>!row.protected).length;status.lockedRetained=keep.filter(row=>row.protected).length;status.latestUtcMs=keep[0]?.utcMs||0;}
  }
  if(body.beforeSession!=null)selected=selected.filter(r=>r.session<body.beforeSession);
  const id=store.next++;store.jobs[id]={id,pending:false,clips:selected.slice(0,5),more:selected.length>5,deleted,recoveryFilesDeleted:body.operation===7?deleted*2:0,error:''};return json({id,pending:true},202);
 }
 const summary=role=>{const rows=store.rows[role];return {camera:role,online:true,sdReady:true,recording:true,frameFresh:true,ageMs:300,filesSupported:store.filesVersion||2,downloadsSupported:1,downloadState:store.download?.camera===role?store.download.state:0,normalRetained:rows.filter(r=>!r.protected).length,lockedRetained:rows.filter(r=>r.protected).length,latestUtcMs:rows[0]?.utcMs||0,freeBytesAtBoundary:5*1073741824,totalBytes:8*1073741824};};
 return json(role?summary(role):{download:store.download||{state:0},cameras:['front','rear'].map(summary),battery:{percent:80,ageMs:100,current:true}});
};
