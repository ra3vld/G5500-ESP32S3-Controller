const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const base=process.env.SIM_URL||'http://127.0.0.1:8080';
const out=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../local-notes/favicon-web');

async function expectIcon(page,online){
 await page.waitForFunction(state=>document.getElementById('connectionFavicon').dataset.state===state,online?'online':'offline');
 const samples=await page.evaluate(async()=>{
  const link=document.getElementById('connectionFavicon'),image=new Image();
  image.src=link.href;await image.decode();
  return [16,32].map(size=>{
   const canvas=document.createElement('canvas');canvas.width=canvas.height=size;
   const context=canvas.getContext('2d');context.drawImage(image,0,0,size,size);
   const pixels=context.getImageData(0,0,size,size).data;
   let green=0,red=0;
   for(let i=0;i<pixels.length;i+=4){
    if(pixels[i+1]>150&&pixels[i+1]>pixels[i]*2)green++;
    if(pixels[i]>150&&pixels[i]>pixels[i+1]*2)red++;
   }
   return {size,green,red,center:[...context.getImageData(size/2,size/2,1,1).data]};
  });
 });
 for(const sample of samples){
  assert(sample[online?'green':'red']>5,`Missing visible ${online?'green':'red'} favicon at ${sample.size}px`);
  assert.equal(sample[online?'red':'green'],0,'Favicon must use only the current connection colour');
  assert.deepEqual(sample.center,online?[8,16,24,255]:[231,76,60,255],'Only the disconnected icon must have a visible slash across its centre');
 }
}

(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 let heartbeat;
 try{
  const errors=[],page=await browser.newPage();
  page.on('pageerror',e=>errors.push(e.message));
  const sample=await(await page.request.get(base+'/api/status')).json();
  await page.addInitScript(()=>window.G5500_HTTP_ONLY=true);
  let state={...sample,deviceRole:'controller'},failure=null,release;
  const initial=new Promise(resolve=>release=resolve);
  await page.route('**/api/status',async route=>{
   await initial;
   if(failure==='network')return route.abort('failed');
   if(failure==='http')return route.fulfill({status:503,json:state});
   if(failure==='invalid')return route.fulfill({json:{ok:false}});
   return route.fulfill({json:state});
  });
  await page.goto(base,{waitUntil:'domcontentloaded'});
  assert.equal(await page.locator('link[rel=icon]').count(),1);
  await expectIcon(page,false);release();await expectIcon(page,true);
  const refresh=()=>page.evaluate(()=>upd(true));
  for(const problem of ['network','http','invalid']){
   failure=problem;await refresh();await expectIcon(page,false);
   failure=null;await refresh();await expectIcon(page,true);
  }
  state={...state,deviceRole:'hud',hudSelected:'123456abcdef',hudOnline:false};
  await refresh();await expectIcon(page,false);
  state={...state,hudOnline:true};await refresh();await expectIcon(page,true);
  state={...state,hudSelected:'000000000000'};await refresh();await expectIcon(page,false);
  state={...state,hudSelected:'123456abcdef',hudTransport:'uart'};
  await refresh();await expectIcon(page,true);
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();await expectIcon(page,true);
  failure='network';await refresh();await expectIcon(page,false);
  await page.evaluate(()=>queueLiveRender());await expectIcon(page,false);
  failure=null;await refresh();await expectIcon(page,true);

  // Live telemetry remains sufficient when the HTTP fallback fails.
  const livePage=await browser.newPage();livePage.on('pageerror',e=>errors.push(e.message));
  let socket,revision=0,httpFails=false;
  let liveState={...sample,deviceRole:'hud',hudSelected:'123456abcdef',hudOnline:true};
  await livePage.route('**/api/status',route=>httpFails?route.abort('failed'):route.fulfill({json:liveState}));
  await livePage.routeWebSocket('**/api/live',ws=>socket=ws);
  await livePage.goto(base);await livePage.waitForFunction(()=>window._forkState);
  const send=()=>socket?.send(JSON.stringify({v:1,boot:1,type:'state',revision:++revision,state:liveState}));
  heartbeat=setInterval(send,200);
  await livePage.waitForFunction(()=>live.ready);await expectIcon(livePage,true);
  httpFails=true;await livePage.evaluate(()=>upd(true));await expectIcon(livePage,true);
  liveState={...liveState,hudOnline:false};send();await expectIcon(livePage,false);
  liveState={...liveState,hudOnline:true};send();await expectIcon(livePage,true);
  clearInterval(heartbeat);heartbeat=null;
  await livePage.waitForFunction(()=>!live.ready);await expectIcon(livePage,false);
  httpFails=false;await livePage.evaluate(()=>upd(true));await expectIcon(livePage,true);

  // Export the actual favicon images from the page for visual inspection.
  for(const [name,online] of [['online',true],['offline',false]]){
   failure=online?null:'network';await refresh();await expectIcon(page,online);
   const image=await page.evaluate(async()=>{
    const image=new Image();image.src=document.getElementById('connectionFavicon').href;await image.decode();
    const canvas=document.createElement('canvas');canvas.width=canvas.height=128;
    canvas.getContext('2d').drawImage(image,0,0,128,128);return canvas.toDataURL('image/png');
   });
   fs.writeFileSync(path.join(out,`favicon-${name}.png`),Buffer.from(image.split(',')[1],'base64'));
  }
  assert.deepEqual(errors,[]);
  console.log('PASS: embedded 16/32px favicon; initial/disconnected red slash; HTTP/LIVE failure, freshness and recovery; HTTP fallback; HUD availability/selection; both tabs');
 }finally{clearInterval(heartbeat);await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
