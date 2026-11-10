// Browser-only contract fixtures. Production paging is exercised separately in C++.
function page(rows,query,epoch,{revision,total=rows.length}={}){
 let before=Number(query.get('before'))||0,after=Number(query.get('after'))||0;
 const reset=(query.has('epoch')&&Number(query.get('epoch'))!==epoch)||(revision!==undefined&&query.has('revision')&&Number(query.get('revision'))!==revision)||after>total||before>total+1;
 if(reset)before=after=0;
 const all=rows.map((row,index)=>({...row,serial:row.serial??total-index})).sort((a,b)=>b.serial-a.serial);
 const oldest=all.at(-1)?.serial||0,available=all.filter(row=>(!before||row.serial<before)&&row.serial>after),selected=available.slice(0,8);
 return {rows:selected,page:{epoch,total,retained:rows.length,oldest,after,nextBefore:available.length>8?selected.at(-1).serial:0,more:available.length>8,reset,lost:!reset&&Boolean(after&&oldest&&after<oldest-1),...(revision===undefined?{}:{revision})}};
}
const cameraCounters=new WeakMap();
function camera(data,query,epoch=77){
 let counter=cameraCounters.get(data);if(!counter){counter={total:0,seen:new Map()};cameraCounters.set(data,counter);}
 // Fixture arrays may be replaced between polls. New events must receive new
 // serials instead of reusing array positions, just as Main's journal does.
 for(const row of [...data.logs||[]].sort((a,b)=>(a.receivedAtMainMs||0)-(b.receivedAtMainMs||0))){
  const key=[row.role,row.stream,row.session,row.seq].join(':');if(!counter.seen.has(key))counter.seen.set(key,++counter.total);
 }
 const rows=(data.logs||[]).map(row=>({...row,serial:row.serial??counter.seen.get([row.role,row.stream,row.session,row.seq].join(':'))}));
 const p=page(rows,query,epoch,{total:data.logTotal??Math.max(counter.total,...rows.map(row=>row.serial))}),summary=query.get('view')==='summary';
 return {...data,cameras:query.get('view')==='events'?[]:data.cameras,logs:summary?[]:p.rows,page:{...p.page,...(summary?{omitted:true,more:false,nextBefore:0}:{omitted:false})}};
}
function trace(data,query,epoch=77){
 const failure=query.get('section')==='failure',last=data.lastFailure;
 const p=failure?page((last?.entries||[]).slice().reverse(),query,epoch,{revision:last?.revision??1}):page(data.entries||[],query,epoch,{total:data.totalEntries??data.entries?.length??0});
 return {...data,entries:failure?[]:p.rows,page:p.page,lastFailure:last?{...last,revision:last.revision??1,entryCount:last.entries?.length||0,entries:failure?p.rows:[]}:null};
}
module.exports={camera,trace};
