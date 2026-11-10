import {friendlyError} from './errors.js';
export const session={logged:false,csrf:'',role:'',generation:0,connected:false,minPasswordLength:12,writing:false,riderSetupComplete:true};
export function setSession(data){Object.assign(session,{logged:Boolean(data),csrf:data?.csrf||'',role:data?.role||'',generation:session.generation+1,connected:Boolean(data),writing:false,riderSetupComplete:data?.riderSetupComplete!==false,minPasswordLength:data?.minPasswordLength||12});}
async function request(path,method='GET',body,options={}){const generation=session.generation;const controller=new AbortController(),cancel=()=>controller.abort();options.signal?.addEventListener('abort',cancel,{once:true});if(options.signal?.aborted)cancel();const timer=setTimeout(cancel,Math.max(500,Math.min(path==='login'?30000:12000,options.timeoutMs||(path==='login'?30000:method==='GET'?4500:12000))));try{const response=await fetch('/api/v1/'+path,{method,credentials:'same-origin',cache:'no-store',signal:controller.signal,headers:{...(body!==undefined?{'Content-Type':'application/json'}:{}),...(method!=='GET'?{'X-CSRF-Token':session.csrf}:{})},...(body!==undefined?{body:JSON.stringify(body)}:{})});let data;try{data=await response.json()}catch(error){if(error.name==='AbortError'||error instanceof TypeError)throw error;const invalid=new Error('Your vest could not provide an update. Please try again.');invalid.status=response.status;invalid.retryable=method==='GET'&&(response.ok||response.status>=500);throw invalid}if(!response.ok){const error=new Error(session.role==='developer'?(data.error||'The request could not be completed.'):friendlyError(response.status,path,data.error));error.status=response.status;if(response.status===401&&session.logged&&generation===session.generation)window.dispatchEvent(new Event('sessionexpired'));throw error;}if(generation!==session.generation)throw new Error('This session changed. Reload the page.');return data;}catch(error){if(options.signal?.aborted)throw new DOMException('Request cancelled.','AbortError');if(error.name==='AbortError'||error instanceof TypeError){const offline=new Error('Your vest did not respond. Check your Wi-Fi connection and try again.');offline.offline=true;throw offline;}throw error;}finally{clearTimeout(timer);options.signal?.removeEventListener('abort',cancel)}}

// One request per tab. Foreground reads/actions take precedence over pose
// previews. A higher-priority request can interrupt a GET; writes are never replayed.
const pending=[];let activeRequest=null;
const cancelled=()=>new DOMException('Request cancelled.','AbortError');
function drain(){
 if(activeRequest||!pending.length)return;
 let index=0;for(let i=1;i<pending.length;i++)if(pending[i].priority<pending[index].priority)index=i;
 const item=pending.splice(index,1)[0];
 if(item.controller.signal.aborted||item.generation!==session.generation){item.cleanup();item.reject(item.controller.signal.aborted?cancelled():new Error('This session changed. Reload the page.'));queueMicrotask(drain);return;}
 activeRequest=item;
 request(...item.args).then(item.resolve,item.reject).finally(()=>{item.cleanup();activeRequest=null;drain();});
}
export function api(...args){
 const options=args[3]||{},method=args[1]||'GET',controller=new AbortController();
 return new Promise((resolve,reject)=>{
  const item={args:[args[0],method,args[2],{...options,signal:controller.signal}],controller,generation:session.generation,priority:method!=='GET'?0:options.priority==='background'?2:1,resolve,reject,cleanup:()=>options.signal?.removeEventListener('abort',abort)};
  function abort(){controller.abort();if(item!==activeRequest){const index=pending.indexOf(item);if(index>=0)pending.splice(index,1);item.cleanup();reject(cancelled());}}
  if(options.signal?.aborted){reject(cancelled());return;}
  options.signal?.addEventListener('abort',abort,{once:true});pending.push(item);
  if(activeRequest&&activeRequest.args[1]==='GET'&&item.priority<activeRequest.priority)activeRequest.controller.abort();
  queueMicrotask(drain);
 });
}

// Initial settings reads recover without rebuilding a form or replaying a write.
export async function readApi(path,options={}){
 let attempt=0;
 while(!options.signal?.aborted){
  try{return await api(path,'GET',undefined,options);}
  catch(error){
   if(error.name==='AbortError'||(!error.offline&&!error.retryable&&!(error.status>=500)&&![408,429].includes(error.status)))throw error;
   options.onRetry?.(error);
   await new Promise((resolve,reject)=>{
    const abort=()=>{clearTimeout(timer);options.signal?.removeEventListener('abort',abort);reject(new DOMException('Page changed','AbortError'));};
    const timer=setTimeout(()=>{options.signal?.removeEventListener('abort',abort);resolve();},Math.min(6000,300*2**Math.min(attempt++,5)));
    if(options.signal?.aborted)abort();else options.signal?.addEventListener('abort',abort,{once:true});
   });
  }
 }
 throw new DOMException('Page changed','AbortError');
}
