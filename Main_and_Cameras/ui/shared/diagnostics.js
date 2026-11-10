// Bounded server pages; cursors never silently bridge a Main reboot or ring loss.
export function diagnosticPath(base,{before=0,after=0,epoch,section,revision}={}){
 const q=new URLSearchParams();
 for(const [key,value]of Object.entries({before,after,epoch,section,revision}))if(value!==undefined&&value!==null&&value!==0)q.set(key,String(value));
 return base+(q.size?'?'+q:'');
}
export function createDiagnosticReader(base,key='entries'){
 let after=0,before=0,epoch,through=0,cache=[],lost=false,pagesRead=0;
 return {
  path(){return diagnosticPath(base,{after,before,epoch});},
  get pending(){return before!==0;},
  accept(data){
   const page=data.page;
   if(!page)return data; // Older saved exports and local fixtures remain readable.
   const reset=page.reset||(epoch!==undefined&&epoch!==page.epoch);
   if(!before){through=page.total;pagesRead=0;}
   if(reset){cache=[];through=page.total;after=0;}
   // A bad/stale cursor must not turn a periodic read into an endless catch-up.
   if(page.more&&(!Number.isInteger(page.nextBefore)||page.nextBefore<=0||(!reset&&before&&page.nextBefore>=before)||++pagesRead>=8)){
    before=0;pagesRead=0;lost=true;
    throw new Error('Diagnostic history did not advance. Collection will retry later.');
   }
   epoch=page.epoch;lost=lost||Boolean(page.lost);
   const rows=[...(data[key]||[]),...cache],unique=new Map();
   for(const row of rows)if(Number.isInteger(row.serial)&&row.serial>=page.oldest&&!unique.has(row.serial))unique.set(row.serial,row);
   cache=[...unique.values()].sort((a,b)=>b.serial-a.serial).slice(0,page.retained);
   before=page.more?page.nextBefore:0;
   if(!before){after=through;pagesRead=0;}
   return {...data,[key]:cache,history:{lost,complete:!before&&cache.length===page.retained,collected:cache.length}};
  }
 };
}
async function pages(fetch,base,{section,revision}={}){
 let first=null,before=0,epoch,rows=[],lost=false;
 const key=base==='cameras/monitor'?'logs':'entries';
 for(let n=0;n<8;n++){
  const data=await fetch(diagnosticPath(base,{before,epoch,section,revision}));
  if(!data.page)return data;
  if(!first){first=data;epoch=data.page.epoch;if(section)revision=data.lastFailure?.revision;}
  else if(data.page.reset||epoch!==data.page.epoch||(section&&revision!==data.lastFailure?.revision))throw Error('Diagnostic history changed during download. Please try again.');
  rows.push(...(section?data.lastFailure?.entries||[]:data[key]||[]));lost=lost||Boolean(data.page.lost);
  if(!data.page.more){
   const unique=[...new Map(rows.map(row=>[row.serial,row])).values()].sort((a,b)=>section||key==='entries'?a.serial-b.serial:b.serial-a.serial);
   const history={lost,complete:!lost&&unique.length===first.page.retained,collected:unique.length};
   return section?{...first,page:{...first.page,more:false,nextBefore:0,assembled:true},lastFailure:first.lastFailure?{...first.lastFailure,entries:unique}:null,history}:{...first,page:{...first.page,more:false,nextBefore:0,assembled:true},[key]:unique,history};
  }
  if(!Number.isInteger(data.page.nextBefore)||data.page.nextBefore<=0||(before&&data.page.nextBefore>=before))throw Error('Diagnostic cursor did not advance.');
  before=data.page.nextBefore;
 }
 throw Error('Diagnostic history exceeded its retained page limit.');
}
export async function collectDiagnostics(fetch,base){
 const result=await pages(fetch,base);
 if(base==='gsm/trace'&&result.page&&result.lastFailure){
  const failure=await pages(fetch,base,{section:'failure',revision:result.lastFailure.revision});
  if(failure.page.epoch!==result.page.epoch||failure.lastFailure?.revision!==result.lastFailure.revision)throw Error('Modem failure history changed during download. Please try again.');
  result.lastFailure=failure.lastFailure;result.failureHistory=failure.history;
 }
 return result;
}
export function redactTrace(data){
 return JSON.parse(JSON.stringify(data,(_key,value)=>typeof value==='string'?value
  .replace(/(AT\+CMGS=)"[^"]*"/g,'$1"[recipient hidden]"')
  .replace(/(?:\+63|0)9\d{9}\b/g,'[recipient hidden]'):value));
}
