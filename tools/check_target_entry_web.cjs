const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const base=process.env.SIM_URL||'http://127.0.0.1:8080';
const state=async()=> (await fetch(base+'/api/status')).json();
const post=async(path,body)=>{
 const r=await fetch(base+path,{method:'POST',body:new URLSearchParams(body)});
 assert.equal(r.status,200,await r.text());
};
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const transport of ['HTTP','WebSocket']){
   await post('/api/move',{d:'S'});
   await post('/api/settings',{model:'G800DXA'});await post('/api/settings',{model:'G5500'});
   const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[];
   page.on('pageerror',e=>errors.push(e.message));
   if(transport==='HTTP')await page.route('**/api/status',async route=>{
    const j=await state();j.livePort=0;await route.fulfill({json:j});
   });
   await page.goto(base+'/#control');
   await page.waitForFunction(()=>window._forkState?.model==='G5500');
   if(transport==='WebSocket')await page.waitForFunction(()=>live.ready);
   await page.evaluate(()=>{
    window.targetSubmissions=[];
    const original=forkPost;
    forkPost=async(path,body)=>{
     if(path==='/api/target')window.targetSubmissions.push({...body});
     return original(path,body);
    };
   });
   const button=page.locator('.target-controls button');
   async function submit(az,el,expected){
    await post('/api/move',{d:'S'});
    const before=await state(),count=await page.evaluate(()=>targetSubmissions.length);
    await page.locator('#taz').fill(az);await page.locator('#tel').fill(el);
    await button.click();
    await page.waitForFunction(n=>targetSubmissions.length===n+1,count,{timeout:1500});
    assert.deepEqual(await page.evaluate(()=>targetSubmissions.at(-1)),expected);
    await page.waitForFunction(expected=>Object.entries(expected).every(([axis,v])=>
     window._forkState[axis==='az'?'targetAz':'targetEl']===Number(v)),expected);
    const after=await state();
    if(!('az' in expected))assert.equal(after.targetAz,before.targetAz,'Empty AZ must preserve its target');
    if(!('el' in expected))assert.equal(after.targetEl,before.targetEl,'Empty EL must preserve its target');
   }
   await submit('100','',{az:'100'});
   await submit('','65',{el:'65'});
   await submit('120.5','80.5',{az:'120.5',el:'80.5'});
   await post('/api/move',{d:'S'});
   let count=await page.evaluate(()=>targetSubmissions.length);
   await page.locator('#taz').fill('');await page.locator('#tel').fill('');await button.click();
   assert.equal(await page.locator('#forkMessage').textContent(),'Введите угол AZ или EL');
   assert.equal(await page.locator('#taz').evaluate(e=>e===document.activeElement),true);
   for(const [az,el] of [['451',''],['','181'],['-1','65']]){
    await page.locator('#taz').fill(az);await page.locator('#tel').fill(el);await button.click();
   }
   assert.equal(await page.evaluate(()=>targetSubmissions.length),count,'Empty/out-of-range input must not send a target');
   await page.locator('#taz').fill('100');await page.locator('#tel').fill('');await page.locator('#tel').pressSequentially('e');
   assert.equal(await page.locator('#tel').evaluate(e=>e.validity.badInput),true);
   await button.click();assert.equal(await page.evaluate(()=>targetSubmissions.length),count,'Malformed EL must not send AZ');
   // Reproduce a controller refusing the combined command because EL is uncalibrated.
   await page.locator('#taz').fill('150');await page.locator('#tel').fill('90');
   const beforeRefusal=await state();
   if(transport==='HTTP')await page.route('**/api/target',async route=>{
    await page.evaluate(()=>{window._forkState.axes[0].calibrated=true;window._forkState.axes[1].calibrated=false;});
    await route.fulfill({status:400,json:{ok:false,result:'not_calibrated'}});
   });
   else await page.evaluate(()=>{
    window.savedSendLive=sendLive;
    sendLive=async(path,body)=>{
     if(path!=='/api/target')return savedSendLive(path,body);
     window._forkState.axes[0].calibrated=true;window._forkState.axes[1].calibrated=false;
     return {status:409,json:async()=>({ok:false,result:'not_calibrated'})};
    };
   });
   await button.click();
   await page.waitForFunction(()=>document.getElementById('forkMessage').textContent==='Сначала выполните калибровку EL');
   const afterRefusal=await state();
   assert.equal(afterRefusal.targetAz,beforeRefusal.targetAz);assert.equal(afterRefusal.targetEl,beforeRefusal.targetEl);
   assert.equal(afterRefusal.moving,false);
   if(transport==='HTTP')await page.unroute('**/api/target');
   else await page.evaluate(()=>{sendLive=savedSendLive;});
   count=await page.evaluate(()=>targetSubmissions.length);
   await post('/api/settings',{model:'G800DXA'});
   await page.waitForFunction(()=>window._forkState?.model==='G800DXA');
   await page.locator('#taz').fill('90');
   await page.locator('#tel').evaluate(e=>e.value='999');await button.click();
   await page.waitForFunction(n=>targetSubmissions.length===n+1,count);
   assert.deepEqual(await page.evaluate(()=>targetSubmissions.at(-1)),{az:'90'});
   await page.waitForFunction(()=>window._forkState?.targetAz===90);
   await post('/api/move',{d:'S'});
   await page.locator('#taz').fill('');await button.click();
   assert.equal(await page.locator('#forkMessage').textContent(),'Введите угол AZ');
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
   assert.deepEqual(errors,[]);
   await page.close();
   console.log('PASS '+transport+': target-entry AZ-only/EL-only/combined, validation, uncalibrated EL feedback, G800 hidden EL; no JS errors');
  }
 }finally{await post('/api/move',{d:'S'});await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
