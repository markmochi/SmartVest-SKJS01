import {$,esc,known,renderContent} from '../shared/dom.js';

export function batteryAge(ms){
 if(ms<1000)return 'less than a second ago';
 const [n,unit]=ms<60000?[Math.floor(ms/1000),'second']:ms<3600000?[Math.floor(ms/60000),'minute']:ms<86400000?[Math.floor(ms/3600000),'hour']:[Math.floor(ms/86400000),'day'];
 return n+' '+unit+(n===1?'':'s')+' ago';
}
let dispose;
export function mountBattery(){
 dispose?.();
 const host=$('batterySlot');if(!host)return;
 let sample=null,at=0,lastReply=0;
 function paint(){
  const now=Date.now(),valid=known(sample?.percent),elapsed=sample?sample.ageMs+Math.max(0,now-at):0;
  const fresh=valid&&sample.current!==false&&elapsed<6000&&now-lastReply<18000;
  const percent=valid?Math.round(sample.percent):null;
  const charging=fresh&&sample.charging===true;
  const label=!valid?'Checking…':!fresh?'Last reading':charging?'Charging':sample.low?'Low battery':'';
  renderContent(`<details id="batteryStatus" class="top-battery" data-component="battery"><summary aria-label="Battery details"><span class="battery-gauge ${sample?.low?'low':''} ${fresh?'':'retained'}" ${valid?`role="meter" aria-label="Estimated battery level" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${percent}" aria-valuetext="${percent} percent${charging?' — charging':''}${fresh?'':' — last reading'}"`:'aria-hidden="true"'}><svg width="36" height="14" viewBox="0 0 112 32" aria-hidden="true" focusable="false"><rect class="battery-shell" x="1" y="1" width="104" height="30" rx="8"/><rect class="battery-fill" x="5" y="5" width="${valid?(96*sample.percent/100).toFixed(2):0}" height="22" rx="5"/><path class="battery-cap" d="M108 11v10a5 5 0 0 0 0-10Z"/>${charging?'<path class="battery-bolt" d="M56 3 45 18h8l-3 11 11-17h-8Z"/>':''}</svg></span><span class="battery-number">${valid?percent+'%':'—'}</span>${label?`<span class="battery-caption">${label}</span>`:''}</summary><div class="battery-popover"><strong>Vest battery</strong><p>${!valid?'Waiting for your first battery reading.':sample.low&&!charging?'Low battery. Charge your vest before riding.':'Battery level.'}</p>${charging?'<p>Charging</p>':''}${valid?`<p data-battery-age>Last updated ${esc(batteryAge(elapsed))}</p>`:''}${valid&&!fresh?'<p>Showing your last battery level. Waiting for an update.</p>':''}</div></details>`,'batterySlot');host.hidden=false;
 }
 function update(event){
  lastReply=Date.now();const b=event.detail?.battery;
  if(known(b?.percent)&&b.percent>=0&&b.percent<=100){sample={...b,ageMs:known(b.ageMs)?Math.max(0,b.ageMs):0};at=lastReply;}
  else if(b&&sample)sample.current=false;
  paint();
 }
 function close(event){
  const d=$('batteryStatus');
  if(event.type==='click'&&event.target.closest('a[href="#batteryStatus"]')){event.preventDefault();d.open=true;d.querySelector('summary').focus();host.scrollIntoView({block:'nearest'});return;}
  if(d?.open&&(event.type==='keydown'?event.key==='Escape':!host.contains(event.target))){d.open=false;if(event.type==='keydown')d.querySelector('summary').focus();}
 }
 function clear(){host.hidden=true;host.replaceChildren();dispose?.();}
 window.addEventListener('veststatus',update);window.addEventListener('vestage',paint);window.addEventListener('vestsignout',clear);
 document.addEventListener('click',close);document.addEventListener('keydown',close);
 dispose=()=>{window.removeEventListener('veststatus',update);window.removeEventListener('vestage',paint);window.removeEventListener('vestsignout',clear);document.removeEventListener('click',close);document.removeEventListener('keydown',close);};
 paint();
}
