// Embedded by generate_web_assets.py. Control/STOP requests bypass this pool.
const background=(()=>{
 try{if(window.frameElement&&window.parent.__g5500Background){
  const shared=window.parent.__g5500Background.scope('frame:'+(window.frameElement.id||location.pathname)+':',()=>!document.hidden&&!!window.frameElement?.getClientRects().length);
  window.addEventListener('pagehide',shared.close);return shared;
 }}catch(_){}
 const entries=new Map();let active=0,limited=false,paused=false;
 const visible=()=>!document.hidden&&(!window.frameElement||!!window.frameElement.getClientRects().length);
 const entry=key=>{if(!entries.has(key))entries.set(key,{next:0,failures:0,pending:null,controller:null,shown:()=>true});return entries.get(key);};
 const deferred=()=>Object.assign(new Error('Обновление отложено'),{name:'PollingDeferred'});
 const allowed=e=>!paused&&visible()&&e.shown()&&navigator.onLine!==false;
 async function read(key,url,kind='json',shown=()=>true,controller=null){
  const e=entry(key);e.shown=shown;
  if(e.pending)return e.pending;
  if(!allowed(e)||Date.now()<e.next||active>=(window.setupCommissioning?1:2))throw deferred();
  const abort=controller||new AbortController();e.controller=abort;++active;
  const timer=setTimeout(()=>abort.abort(),5000);
  e.pending=Promise.resolve().then(async()=>{
   try{
    const receive=async()=>{
     if(!allowed(e))throw deferred();
     const response=await fetch(url,{cache:'no-store',signal:abort.signal});
     const data=await response[kind==='reply'?'text':kind]();return {response,data};
    };
    let result;
    try{result=await receive();}catch(error){
     // A server can retire an idle HTTP connection just as the browser
     // reuses it. Only these read-only requests get one retry; the original
     // five-second deadline also covers the pause and the second attempt.
     if(!(error instanceof TypeError)||abort.signal.aborted||navigator.onLine===false)throw error;
     await new Promise((resolve,reject)=>{
      const cancel=()=>{clearTimeout(retry);reject(new DOMException('aborted','AbortError'));};
      const retry=setTimeout(()=>{abort.signal.removeEventListener('abort',cancel);resolve();},100);
      if(abort.signal.aborted)cancel();else abort.signal.addEventListener('abort',cancel,{once:true});
     });
     result=await receive();
    }
    const {response,data}=result;
    if(!response.ok&&kind!=='reply')throw Error('HTTP '+response.status);
    if(data&&typeof data==='object'&&typeof data.networkMemoryLimited==='boolean')limited=data.networkMemoryLimited;
    if(response.ok){e.failures=0;e.next=Date.now()+(limited?500:0);}
    else e.next=Date.now()+[1000,2000,4000,8000,15000][Math.min(e.failures++,4)];
    return kind==='reply'?{ok:response.ok,status:response.status,text:async()=>data,json:async()=>JSON.parse(data)}:data;
   }catch(error){
    if(abort.signal.reason?.name==='PollingDeferred')throw abort.signal.reason;if(paused&&abort.signal.aborted)throw deferred();
    if(allowed(e)){const delay=[1000,2000,4000,8000,15000][Math.min(e.failures++,4)];e.next=Date.now()+Math.round(delay*(0.9+Math.random()*0.2));}
    throw error;
   }finally{clearTimeout(timer);--active;e.pending=null;e.controller=null;}
  });
  return e.pending;
 }
 function poll(key,load,interval,shown=()=>true){
  const e=entry(key);e.stop?.();e.shown=shown;let pending=null,timer=null,stopped=false;
  const run=()=>{
   if(stopped)return Promise.resolve();
   if(pending)return pending;clearTimeout(timer);
   pending=Promise.resolve().then(()=>{if(allowed(e))return load();}).catch(()=>{}).finally(()=>{
    pending=null;if(!stopped)timer=setTimeout(run,Math.max(interval,limited?500:0,e.next-Date.now()));
   });return pending;
  };
  e.stop=()=>{stopped=true;clearTimeout(timer);e.run=null;};e.run=run;run();return run;
 }
 function visibility(){
  for(const e of entries.values())if(!allowed(e))e.controller?.abort(deferred());else if(e.run)e.run();
 }
 document.addEventListener('visibilitychange',visibility);
 document.addEventListener('toggle',()=>{for(const e of entries.values())if(!allowed(e))e.controller?.abort(deferred());},true);
 document.addEventListener('click',()=>queueMicrotask(()=>{for(const e of entries.values())if(!allowed(e))e.controller?.abort(deferred());}),true);
 window.addEventListener('offline',visibility);window.addEventListener('online',visibility);
 const api={read,poll,visible,refreshVisibility:visibility,cancel:key=>entry(key).controller?.abort(deferred()),limited:()=>limited,setLimited:value=>{limited=!!value;},pause:value=>{if(paused===!!value)return;paused=!!value;visibility();}};
 api.scope=(prefix,shown)=>({...api,
  visible:()=>visible()&&shown(),
  read:(key,url,kind='json',local=()=>true,controller=null)=>read(prefix+key,url,kind,()=>shown()&&local(),controller),
  poll:(key,load,interval,local=()=>true)=>poll(prefix+key,load,interval,()=>shown()&&local()),
  cancel:key=>api.cancel(prefix+key),
  close:()=>{for(const [key,e] of entries)if(key.startsWith(prefix)){e.stop?.();e.controller?.abort(deferred());entries.delete(key);}}
 });
 return api;
})();
window.__g5500Background=background;
