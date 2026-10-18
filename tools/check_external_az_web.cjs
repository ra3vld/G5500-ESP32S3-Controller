const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const dgram=require('node:dgram'),net=require('node:net');
const base=process.env.SIM_URL||'http://127.0.0.1:28880';
const output=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../local-notes/external-az-web');
const post=async(route,data)=>{const r=await fetch(base+route,{method:'POST',body:new URLSearchParams(data)});assert(r.ok,await r.text());};
const state=async()=>await(await fetch(base+'/api/status')).json();
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const udp=(text,port)=>new Promise((resolve,reject)=>{const socket=dgram.createSocket('udp4');socket.send(text,port,'127.0.0.1',e=>{socket.close();e?reject(e):resolve();});});
const tcp=text=>new Promise((resolve,reject)=>{const socket=net.createConnection({host:'127.0.0.1',port:Number(process.env.TCP_PORT||28533)},()=>socket.end(text+'\n'));socket.resume();socket.setTimeout(3000,()=>socket.destroy(new Error('TCP timeout')));socket.on('error',reject);socket.on('close',resolve);});
(async()=>{
 fs.mkdirSync(output,{recursive:true});
 await post('/api/move',{d:'S'});
 await post('/api/settings',{model:'G5500',externalAzMode:'shortest',n1mmManualFollow:false,pstManualFollow:false,gs232ManualFollow:false,rotctldManualFollow:false});
 await post('/api/mode',{mode:'manual'});
 await post('/api/azrange',{profile:'p45'});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:900}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/#settings');await page.waitForFunction(()=>window._forkState&&live.ready);
  await page.locator('#rotorSettings details').evaluate(e=>e.open=true);
  assert.equal(await page.locator('#externalAzMode').inputValue(),'shortest');
  for(const mode of ['mechanical','shortest']){
   await page.locator('#externalAzMode').selectOption(mode);
   await delay(1200);
   assert.equal(await page.locator('#externalAzMode').inputValue(),mode,'Unsaved choice must survive polling');
   const request=page.waitForRequest(r=>r.url().endsWith('/api/settings')&&r.method()==='POST');
   await page.locator('#forkSettings [type=submit]').click();
   const body=new URLSearchParams((await request).postData());
   assert.equal(body.get('externalAzMode'),mode);
   assert([...body].length<=32,'Settings must fit the firmware form-argument budget');
   await page.waitForFunction(()=>!document.getElementById('forkSettings').dataset.dirty);
   assert.equal((await state()).externalAzMode,mode);
   await page.reload();await page.waitForFunction(()=>window._forkState&&live.ready);
   await page.locator('#rotorSettings details').evaluate(e=>e.open=true);
   assert.equal(await page.locator('#externalAzMode').inputValue(),mode);
   for(let source=0;source<4;source++){
    await post('/api/move',{d:'S'});await post('/api/target',{az:350});await post('/api/sim/step',{ms:90000});await post('/api/move',{d:'S'});
    const before=await state();assert(Math.abs(before.az-350)<1);
    if(source===0)await tcp('M10');
    else if(source===1)await tcp('P 10 45');
    else if(source===2)await udp('<PST><AZIMUTH>10</AZIMUTH></PST>',before.pstPort);
    else await udp('<N1MMRotor><rotor>*</rotor><goazi>10</goazi></N1MMRotor>',before.n1mmPort);
    let after;
    for(let retry=0;retry<40;retry++){after=await state();if(after.pendingValid)break;await delay(25);}
    assert.equal(after.pendingValid,true,`mode=${mode}, source=${source}`);
    assert.equal(after.pending.az,mode==='shortest'?370:10);
    assert.equal(after.moving,false);
   }
  }
  const notice=page.locator('#settingsFeedback');
  if(await notice.isVisible())await notice.getByRole('button',{name:'Закрыть уведомление'}).click();
  for(const mode of ['shortest','mechanical']){
   await page.locator('#externalAzMode').selectOption(mode);
   for(const width of [1100,390,320]){
    await page.setViewportSize({width,height:900});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`Overflow at ${width}px`);
    assert(await page.locator('#externalAzMode').isVisible());
    assert(await page.locator('#externalAzMode').evaluate(e=>{
     const style=getComputedStyle(e),ctx=document.createElement('canvas').getContext('2d');ctx.font=style.font;
     return ctx.measureText(e.selectedOptions[0].textContent).width+parseFloat(style.paddingLeft)+parseFloat(style.paddingRight)+20<=e.clientWidth;
    }),`Clipped option ${mode} at ${width}px`);
    await page.locator('#externalAzSettings').screenshot({path:path.join(output,`external-az-${mode}-${width}.png`)});
   }
  }
  assert.deepEqual(errors,[]);
  console.log('PASS: saved/reloaded AZ mode, dirty-form preservation, shortest/mechanical routes via GS-232/rotctld TCP and PST/N1MM UDP, 1100/390/320px layout');
 }finally{await browser.close();await post('/api/move',{d:'S'});}
})().catch(e=>{console.error(e);process.exit(1)});
