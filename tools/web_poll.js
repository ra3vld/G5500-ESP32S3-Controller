// Embedded by generate_web_assets.py. Control/STOP requests bypass this pool.
const background=(()=>{
 const entries=new Map();let active=0,limited=false;
 const visible=()=>!document.hidden&&(!window.frameElement||!!window.frameElement.getClientRects().length);
 const entry=key=>{if(!entries.has(key))entries.set(key,{next:0,failures:0,pending:null,controller:null,shown:()=>true});return entries.get(key);};
 const deferred=()=>Object.assign(new Error('Обновление отложено'),{name:'PollingDeferred'});
 const allowed=e=>visible()&&e.shown()&&navigator.onLine!==false;
 async function read(key,url,kind='json',shown=()=>true,controller=null){
  const e=entry(key);e.shown=shown;
  if(e.pending)return e.pending;
  if(!allowed(e)||Date.now()<e.next||active>=2)throw deferred();
  const abort=controller||new AbortController();e.controller=abort;++active;
  const timer=setTimeout(()=>abort.abort(),5000);
  e.pending=Promise.resolve().then(async()=>{
   try{
    const response=await fetch(url,{cache:'no-store',signal:abort.signal});
    const data=await response[kind==='reply'?'text':kind]();
    if(!response.ok&&kind!=='reply')throw Error('HTTP '+response.status);
    if(data&&typeof data==='object'&&typeof data.networkMemoryLimited==='boolean')limited=data.networkMemoryLimited;
    if(response.ok){e.failures=0;e.next=Date.now()+(limited?500:0);}
    else e.next=Date.now()+[1000,2000,4000,8000,15000][Math.min(e.failures++,4)];
    return kind==='reply'?{ok:response.ok,status:response.status,text:async()=>data,json:async()=>JSON.parse(data)}:data;
   }catch(error){
    if(allowed(e)){const delay=[1000,2000,4000,8000,15000][Math.min(e.failures++,4)];e.next=Date.now()+Math.round(delay*(0.9+Math.random()*0.2));}
    throw error;
   }finally{clearTimeout(timer);--active;e.pending=null;e.controller=null;}
  });
  return e.pending;
 }
 function poll(key,load,interval,shown=()=>true){
  const e=entry(key);e.shown=shown;let pending=null,timer=null;
  const run=()=>{
   if(pending)return pending;clearTimeout(timer);
   pending=Promise.resolve().then(()=>{if(allowed(e))return load();}).catch(()=>{}).finally(()=>{
    pending=null;timer=setTimeout(run,Math.max(interval,limited?500:0,e.next-Date.now()));
   });return pending;
  };
  e.run=run;run();return run;
 }
 function visibility(){
  for(const e of entries.values())if(!allowed(e))e.controller?.abort();else if(e.run)e.run();
 }
 document.addEventListener('visibilitychange',visibility);
 document.addEventListener('toggle',()=>{for(const e of entries.values())if(!allowed(e))e.controller?.abort();},true);
 document.addEventListener('click',()=>queueMicrotask(()=>{for(const e of entries.values())if(!allowed(e))e.controller?.abort();}),true);
 window.addEventListener('offline',visibility);window.addEventListener('online',visibility);
 return {read,poll,visible,cancel:key=>entry(key).controller?.abort(),limited:()=>limited,setLimited:value=>{limited=!!value;}};
})();
