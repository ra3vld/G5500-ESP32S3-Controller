// The first response can read status and send STOP without the full UI.
(manifest=>{
 const root=document.getElementById('webBoot'),message=document.getElementById('webBootMessage');
 const state=document.getElementById('webBootState'),retry=document.getElementById('webBootRetry');
 let done=false,statusPending=false,statusTimer,latest=null;
 const controllers=new Set();
 async function request(url,options={},timeout=8000){
  const controller=new AbortController();controllers.add(controller);
  const timer=setTimeout(()=>controller.abort(),timeout);
  try{
   const response=await fetch(url,{...options,signal:controller.signal});
   if(!response.ok)throw new Error('HTTP '+response.status);
   return await response[options.method?'json':url.startsWith('/api/')?'json':'text']();
  }finally{clearTimeout(timer);controllers.delete(controller);}
 }
 async function status(){
  if(done||statusPending||document.hidden||window.frameElement&&!window.frameElement.getClientRects().length)return;
  statusPending=true;
  try{
   const data=await request('/api/status?view=bootstrap',{cache:'no-store'},4000);latest=data;
   document.getElementById('webBootName').textContent=data.rotorName||'Контроллер поворотки';
   state.textContent=data.deviceRole==='hud'?(data.hudOnline?'Пульт и поворотка на связи':'Пульт на связи · поворотка недоступна'):
    data.fault?'Ошибка: '+data.fault:data.moving?'Привод движется':'Привод остановлен';
   for(const [i,axis] of ['az','el'].entries())document.getElementById('webBoot'+axis).textContent=
    data.axes?.[i]?.calibrated===false?'NC':Number.isFinite(data[axis])?data[axis].toFixed(1)+'°':'—';
  }catch(e){if(!done)state.textContent='Нет свежего ответа устройства';}
  finally{statusPending=false;}
 }
 document.getElementById('webBootStop').onclick=async()=>{
  state.textContent='Отправляем STOP…';
  try{
   const body=new URLSearchParams({d:'S'});
   if(Number.isFinite(latest?.hudSelectionEpoch))body.set('selection',latest.hudSelectionEpoch);
   const result=await request('/api/move',{method:'POST',body},4000);
   state.textContent=result.ok?'STOP принят':'Нет подтверждения STOP';
  }catch(e){state.textContent='Нет подтверждения STOP. Проверьте связь.';}
 };
 const resource=(tag,url)=>new Promise((resolve,reject)=>{
  const node=document.createElement(tag),timer=setTimeout(()=>{node.remove();reject(new Error('timeout'));},15000);
  node.onload=()=>{clearTimeout(timer);resolve();};
  node.onerror=()=>{clearTimeout(timer);node.remove();reject(new Error('load'));};
  if(tag==='link'){node.rel='stylesheet';node.href=url;}else{node.src=url;node.async=false;}
  document.head.append(node);
 });
 retry.onclick=()=>location.reload();
 async function load(){
  await status();statusTimer=setInterval(status,2500);
  try{
   await resource('link',manifest.style);
   const html=await request(manifest.body);
   const template=document.createElement('template');template.innerHTML=html;
   const nodes=[...template.content.children];
   for(const node of nodes){node.inert=true;node.style.visibility='hidden';}
   document.body.append(template.content);
   // Only one bulk resource at a time; leave room for status and STOP.
   for(const url of manifest.scripts)await resource('script',url);
   if(!window.__webUiReady)throw new Error('init');
   done=true;clearInterval(statusTimer);for(const controller of controllers)controller.abort();
   for(const node of nodes){node.inert=false;node.style.removeProperty('visibility');}
   root.remove();document.documentElement.dataset.webReady='1';
  }catch(e){
   message.textContent='Не удалось загрузить интерфейс. Проверьте связь и повторите загрузку.';
   retry.hidden=false;
  }
 }
 load();
})(__WEB_MANIFEST__);
