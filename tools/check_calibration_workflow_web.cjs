const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const base=process.env.SIM_URL||'http://127.0.0.1:8080';
const output=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../local-notes/calibration-workflow-web');
const request=async(route,body)=>{const r=await fetch(base+route,body?{method:'POST',body:new URLSearchParams(body)}:{});assert(r.ok,await r.clone().text());return r.json()};
(async()=>{
 fs.mkdirSync(output,{recursive:true});
 await request('/api/move',{d:'S'});await request('/api/settings',{model:'G5500'});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:1000}}),errors=[];
  page.setDefaultTimeout(10000);page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
  await page.goto(base+'/#settings');await page.waitForFunction(()=>window._forkState&&live.ready);
  await page.locator('#caltoggle').click();
  for(const [axis,profiles] of [['az',[['p36',0,360],['p45',0,450],['custom',-60,450]]],['el',[['standard',0,180],['custom',-30,240]]]]){
   for(const [profile,start,span] of profiles){
    await request('/api/'+axis+'range',{profile,start,span});
    await page.waitForFunction(({axis,profile,start})=>window._forkState[axis+'Profile']===profile&&window._forkState[axis+'Start']===start,{axis,profile,start});
    for(const mode of ['2','multi']){
     await page.locator(mode==='2'?'#mode2':'#modem').click();
     await page.waitForFunction(mode=>window._forkState.calMode===mode&&!document.getElementById(mode==='2'?'panel2':'panelm').classList.contains('hide'),mode);
     await request('/api/calreset',{axis,mode});
     await page.evaluate(async()=>{await upd(true);await Promise.all([loadAzCalibration(),loadElCalibration()]);});
     const prefix=axis+(mode==='2'?'2':''),list=page.locator('#'+prefix+'PointList'),progress=page.locator('#'+prefix+'CalProgress');
     await page.waitForFunction(prefix=>document.getElementById(prefix+'CalProgress').textContent.startsWith('Записано 0 из'),prefix);
     const angles=mode==='2'?[start,start+span]:(await request('/api/'+axis+'cal')).points.map(p=>p.angle);
     assert.deepEqual(await list.locator('.point-row').evaluateAll(rows=>rows.map(r=>Number(r.dataset.angle))),angles);
     assert.equal(await list.getByRole('button',{name:'Запомнить положение'}).count(),angles.length);
     assert.equal(await list.getByRole('button',{name:'Удалить'}).count(),mode==='multi'&&profile==='custom'?angles.length-2:0);
     for(const [i,angle] of angles.entries()){
      // Move virtual hardware to the physical point, then capture through the actual UI.
      await request('/api/target',{[axis]:angle});
      let position;
      for(let step=0;step<160;++step){position=await request('/api/sim/step',{ms:1000});if(!position.moving)break;}
      assert(Math.abs(position[axis]-angle)<=5,'Virtual hardware must reach the calibration point');
      await request('/api/move',{d:'S'});
      const route=mode==='2'?'/api/cal2':profile==='custom'?'/api/'+axis+'cal':'/api/cal';
      const sent=page.waitForRequest(r=>new URL(r.url()).pathname===route&&(mode!=='multi'||profile!=='custom'||r.method()==='POST'));
      await list.locator('.point-row').nth(i).getByRole('button',{name:'Запомнить положение'}).click();
      const command=await sent,url=new URL(command.url()),body=new URLSearchParams(command.postData()||url.search);
      if(mode==='2')assert.equal(body.get('p'),(i?'f':'o')+(axis==='el'?'2':''));
      else if(profile==='custom'){assert.equal(body.get('op'),'capture');assert.equal(Number(body.get('angle')),angle)}
      else assert.equal(body.get('p'),axis+angle);
      await page.waitForFunction(({prefix,count})=>document.getElementById(prefix+'CalProgress').textContent.startsWith('Записано '+count+' из'),{prefix,count:i+1});
      assert.equal((await request('/api/status')).moving,false,'Capture must not start movement');
     }
     assert.match(await progress.innerText(),/Калибровка готова/);
    }
   }
  }
  // New custom points capture the current axis ADC in one request, without moving.
  for(const axis of ['az','el']){
   const route='/api/'+axis+'cal',input=page.locator('#'+axis+'PointAngle');
   const add=page.locator('#custom'+(axis==='az'?'Az':'El')+'Points').getByRole('button',{name:'Добавить и запомнить',exact:true});
   await request('/api/target',{[axis]:axis==='az'?15.5:60});
   let position;
   for(let step=0;step<160;++step){position=await request('/api/sim/step',{ms:1000});if(!position.moving)break;}
   await request('/api/move',{d:'S'});
   const expectedMv=Math.trunc(100+(position[axis]-position[axis+'Start'])*2700/position[axis+'Span']);
   const before=(await request(route)).points;
   const commands=[];const observe=r=>{if(new URL(r.url()).pathname===route&&r.method()==='POST')commands.push(new URLSearchParams(r.postData()))};
   page.on('request',observe);
   await input.fill('15.5');await add.click();
   await page.waitForFunction(axis=>document.getElementById(axis+'PointAngle').value==='',axis);
   page.off('request',observe);
   assert.equal(commands.length,1);assert.equal(commands[0].get('op'),'add_capture');
   assert.equal(commands[0].get('angle'),'15.5');
   const after=(await request(route)).points;assert.equal(after.length,before.length+1);
   assert.equal(after.find(p=>p.angle===15.5).mv,expectedMv);
   const row=page.locator('#'+axis+'PointList .point-row[data-angle="15.5"]');
   assert((await row.innerText()).includes(expectedMv+' мВ'));assert(await row.locator('button.good').count());
   assert.equal((await request('/api/status')).moving,false);
   assert.match(await page.locator('#settingsFeedbackText').innerText(),/ADC запомнено/);
   // Duplicate rejection must preserve the captured value and the input for correction.
   await input.fill('15.5');await add.click();
   await page.waitForFunction(()=>document.getElementById('settingsFeedback').dataset.state==='error');
   assert.equal(await input.inputValue(),'15.5');assert.deepEqual((await request(route)).points,after);
   // A failed save must not report success or clear the entered angle.
   await page.route('**'+route,r=>r.request().method()==='POST'?r.fulfill({status:503,json:{error:'calibration_save_failed'}}):r.continue());
   await input.fill('25.5');await add.click();
   await page.waitForFunction(()=>document.getElementById('settingsFeedback').dataset.state==='error'&&document.getElementById('settingsFeedbackText').textContent.includes('сохранить'));
   assert.equal(await input.inputValue(),'25.5');assert.deepEqual((await request(route)).points,after);
   await page.unroute('**'+route);await input.fill('');
  }
  // A reverse-polarity sensor is valid; equal or non-monotone readings are not.
  for(const axis of ['az','el']){
   for(const [values,ready] of [[[2800,1200,100],true],[[100,100,2800],false],[[100,2800,1200],false]]){
    await page.evaluate(({axis,values})=>renderCalibrationPoints(axis,'multi',values.map((mv,i)=>({angle:i*45,mv,endpoint:i!==1})),false,false),{axis,values});
    assert.equal((await page.locator('#'+axis+'CalProgress').innerText()).includes('Калибровка готова'),ready);
   }
  }
  await page.evaluate(()=>Promise.all([loadAzCalibration(),loadElCalibration()]));
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:1000});
   for(const mode of ['2','multi']){
    await page.locator(mode==='2'?'#mode2':'#modem').click();
    await page.waitForFunction(mode=>window._forkState.calMode===mode,mode);
    await page.evaluate(()=>document.getElementById('settingsFeedback').hidden=true);
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth&&document.querySelector('.wrap').scrollWidth<=document.querySelector('.wrap').clientWidth));
    for(const axis of ['az','el']){
     await page.locator('#'+axis+(mode==='2'?'2':'')+'PointList .point-row').first().scrollIntoViewIfNeeded();
     await page.screenshot({path:path.join(output,`points-${axis}-${mode}-${width}.png`)});
     if(mode==='multi'){
      const form=page.locator('#custom'+(axis==='az'?'Az':'El')+'Points');await form.scrollIntoViewIfNeeded();
      assert(await form.locator('button').evaluate(b=>b.scrollWidth<=b.clientWidth&&b.scrollHeight<=b.clientHeight));
      await page.screenshot({path:path.join(output,`add-capture-${axis}-${width}.png`)});
     }
    }
   }
  }
  assert.deepEqual(errors,[]);
  console.log('PASS: common AZ/EL capture procedure, all five profiles, both modes, atomic add/capture with current axis ADC, duplicate/save failures, ordered points, reverse polarity, no movement on capture, responsive lists and add buttons');
 }finally{await browser.close();await request('/api/move',{d:'S'});await request('/api/azrange',{profile:'p45'});await request('/api/elrange',{profile:'standard'});}
})().catch(e=>{console.error(e);process.exit(1)});
