// Browser checks with an in-memory controller; no device or motor access.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/autotune-panel-web');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const sample=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8',timeout:5000}).trim()).state;
const motion={regulatorMode:'cascade',ready:true,kickEnabled:true,holdEnabled:true,OutputLimitPercent:50,KickPercent:40,MovementDegrees:1,
 Velocity:30,Acceleration:10,Deceleration:10,Jerk:8,PositionKp:2,PositionKi:0,PositionKd:.2,VelocityKp:10,VelocityKi:5,VelocityKd:0,Feedforward:7.46,
 PositionTolerance:.5,HoldResumeDegrees:1,StoppedVelocity:1,KickMs:1000,KickReleaseMs:20,VelocityWindowMs:200,DerivativeFilterMs:100,FeedbackTimeoutMs:500,SettleMs:300,StallMs:3000};
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox'],timeout:10000});
 try{
  const page=await browser.newPage({viewport:{width:390,height:950}}),posts=[],errors=[];
  page.setDefaultTimeout(10000);page.setDefaultNavigationTimeout(10000);
  page.on('pageerror',e=>{errors.push(e.message);console.error('PAGE ERROR',e.message);});await page.addInitScript(()=>window.G5500_HTTP_ONLY=true);
  const state={...sample,deviceRole:'controller',model:'G5500',configurationLocked:false,controlMode:'manual',moving:false,pendingValid:false,presetActive:false,fault:'',
   az:0,el:12.25,azStart:-50,azMax:190,elStart:-45,elMax:190,azRangeRevision:7,elRangeRevision:7,autotune:{phase:'idle',active:false,axis:-1,id:0},
   build:{...sample.build,drive:'BTS7960'},
   axes:[{...sample.axes[0],current:0,calibrated:true,speedEnabled:true,regulatorMode:'cascade',motionReady:true,motionPhase:'idle',motionFault:'',direction:0,tracking:false,holdArmed:false,measuredVelocity:0,requestedVelocity:0,speedOutputPercent:0},
    {...sample.axes[1],current:12.25,calibrated:true,speedEnabled:true,regulatorMode:'cascade',motionReady:true,motionPhase:'idle',motionFault:'',direction:0,tracking:false,holdArmed:false,measuredVelocity:0,requestedVelocity:0,speedOutputPercent:0}]};
  await page.route('http://autotune.test/**',async route=>{
   const req=route.request(),url=new URL(req.url());
   if(req.method()==='POST'||url.pathname==='/api/move'){
    const form=Object.fromEntries(req.method()==='POST'?new URLSearchParams(req.postData()):url.searchParams);posts.push({path:url.pathname,form});
    if(url.pathname==='/api/motion/reset'){state.fault='';state.axes.forEach(a=>{a.motionFault='';a.motionPhase='idle';});}
    if(url.pathname==='/api/motion/autotune'&&form.action==='center'){const i=form.axis==='az'?0:1;state.moving=true;state.axes[i].tracking=true;state.axes[i].motionPhase='run';}
    if(url.pathname==='/api/move'){state.moving=false;state.axes.forEach(a=>{a.tracking=false;a.holdArmed=false;a.motionPhase='idle';});}
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(url.pathname==='/api/motion')return route.fulfill({json:{axes:[motion,motion]}});
   if(url.pathname==='/api/motion/autotune')return route.fulfill({json:state.autotune});
   if(url.pathname==='/api/rssi')return route.fulfill({json:{values:[],count:0,min:null,max:null,avg:null}});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,points:[],items:[],peers:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://autotune.test/#settings');await page.locator('#motionSettings summary').click();
  await page.waitForFunction(()=>document.getElementById('azTuneCenter').textContent==='В середину (70°)'&&!document.getElementById('azTuneCenter').disabled);
  assert.equal(await page.locator('#azTunePosition').textContent(),'0.00°');assert.equal(await page.locator('#azTuneVelocity').textContent(),'0.00 °/с');
  assert.equal(await page.locator('#elTuneCenter').textContent(),'В середину (72.5°)');
  // A legacy response has no sustaining field: show a safe zero default.
  await page.waitForFunction(()=>document.getElementById('azMotionSustainingPercent').value==='0');
  assert(await page.locator('#azMotionSustainingPercent').isVisible());
  await page.locator('#azRegulatorMode').selectOption('simple');
  assert(!(await page.locator('#azTuneStageSlowLabel').isVisible()));
  assert(!(await page.locator('#azTuneStageFastLabel').isVisible()));
  // The sustaining floor also applies to manual PWM in Simple mode.
  assert.equal(await page.locator('#azMotionSustainingPercent').isVisible(),true);
  await page.locator('#azRegulatorMode').selectOption('cascade');
  assert(await page.locator('#azTuneStageSlowLabel').isVisible());
  await page.locator('#azMotionSustainingPercent').fill('16');
  await page.locator('#azMotionForm button[type=submit]').click();
  await page.waitForTimeout(150);
  assert(posts.some(p=>p.path==='/api/settings'&&p.form.azMotionSustainingPercent==='16'));

  await page.locator('#elTuneCenter').click();assert.equal(posts.at(-1).form.axis,'el');assert.equal(posts.at(-1).form.action,'center');assert.equal(posts.at(-1).form.rangeRevision,'7');
  await page.waitForFunction(()=>document.getElementById('azTuneStart').disabled&&!document.getElementById('elTuneStop').disabled);
  await page.locator('#elTuneStop').click();await page.waitForFunction(()=>!window._forkState?.moving);
  assert.equal(posts.at(-1).path,'/api/move');assert.equal(posts.at(-1).form.d,'S');
  state.axes[0].current=70;await page.evaluate(()=>upd(true));
  assert(await page.locator('#azTuneStageKick').isDisabled());
  await page.locator('#azTuneStageModel').uncheck();
  for(const suffix of ['Kick','Slow','Fast'])await page.locator('#azTuneStage'+suffix).uncheck();
  assert(!(await page.locator('#azTuneStart').isDisabled()));
  await page.locator('#azTuneStart').click();
  await page.waitForFunction(()=>!document.getElementById('azTuneStart').disabled);
  assert.equal(posts.at(-1).path,'/api/motion/autotune');assert.equal(posts.at(-1).form.tuneStages,'4');
  await page.locator('#azTuneStageMinimum').uncheck();assert(await page.locator('#azTuneStart').isDisabled());
  await page.locator('#azTuneStageModel').check();assert(await page.locator('#azTuneStageKick').isChecked());
  assert(await page.locator('#azTuneStageKick').isDisabled());
  await page.locator('#azTuneStart').click();await page.waitForFunction(()=>!document.getElementById('azTuneStart').disabled);
  assert.equal(posts.at(-1).form.tuneStages,'3');
  for(const suffix of ['Minimum','Slow','Fast'])await page.locator('#azTuneStage'+suffix).check();
  state.fault='az_stalled';state.axes[0].motionFault='stalled';state.axes[0].motionPhase='fault';
  await page.evaluate(()=>upd(true));await page.waitForFunction(()=>!document.getElementById('azTuneReset').disabled);
  assert.match(await page.locator('#azTuneDriveState').textContent(),/Ошибка: AZ: движение прекратилось/);
  await page.locator('#azTuneReset').click();assert.equal(posts.at(-1).path,'/api/motion/reset');assert.equal(posts.at(-1).form.axis,'all');
  await page.waitForFunction(()=>document.getElementById('azTuneDriveState').textContent==='Остановлен');
  state.autotune={phase:'low',active:true,axis:0,id:1,direction:1,output:18};state.axes[0].measuredVelocity=3.25;state.axes[0].speedOutputPercent=18;
  state.axes[0].motionPhase='run';state.moving=true;await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>document.getElementById('azTuneVelocity').textContent==='3.25 °/с');
  assert.equal(await page.locator('#azTuneVelocity').textContent(),'3.25 °/с');assert.equal(await page.locator('#azTuneRequestedVelocity').textContent(),'—');
  assert(await page.locator('#azTuneCenter').isDisabled());assert(await page.locator('#azTuneReset').isDisabled());
  assert(await page.locator('#azTuneStageMinimum').isDisabled());
  state.autotune.phase='tracking';state.autotune.stages=8;state.autotune.trackingTrial=5;state.autotune.trackingTrials=12;await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>document.getElementById('azTuneStatus').textContent.includes('5/12'));
  assert.match(await page.locator('#azTuneStatus').textContent(),/Подбор слежения за серией команд/);
  assert.equal(await page.locator('#azTuneRequestedVelocity').textContent(),'0.00 °/с');
  assert(await page.locator('#azTuneStageSlow').isChecked());assert(!(await page.locator('#azTuneStageFast').isChecked()));
  assert(!(await page.locator('#azTuneStageMinimum').isChecked()));
  await page.locator('#azTunePosition').evaluate(e=>e.closest('.settings-group').id='autotune-preview');
  for(const width of [320,390,1100]){
   await page.setViewportSize({width,height:950});
   const issues=await page.locator('#autotune-preview').evaluate(root=>{
    const errors=[];if(document.documentElement.scrollWidth>innerWidth)errors.push('horizontal page overflow');
    for(const button of root.querySelectorAll('button')){
     if(button.scrollWidth>button.clientWidth+1||button.scrollHeight>button.clientHeight+1)errors.push('clipped button '+button.id);
     if(button.getBoundingClientRect().height<44)errors.push('short touch target '+button.id);
    }
    for(const e of root.querySelectorAll('.kv>div'))if(e.scrollWidth>e.clientWidth+1)errors.push('clipped telemetry');
    return errors;
   });assert.deepEqual(issues,[]);
   await page.locator('#azMotionSustainingPercent').locator('..').screenshot({path:path.join(out,`minimum-${width}.png`)});
   await page.locator('#autotune-preview').screenshot({path:path.join(out,`autotune-${width}.png`)});
   await page.locator('#azTuneStageMinimumLabel').locator('..').screenshot({path:path.join(out,`stages-${width}.png`)});
  }
  state.autotune={phase:'complete',active:false,axis:0,id:1,mode:'cascade',validationError:.2,
   options:{tuneStages:8},result:{...motion,minimumVelocity:0,minimumOutput:0,minimumSetpoint:0,holdEnabled:true},
   tracking:{verified:true,selected:1,slowVelocity:1.5,fastVelocity:0,candidates:[{}, {meanError:.4,overshoot:.2}]}};
  state.moving=false;await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>document.getElementById('azTuneResult').textContent.includes('вариант 2 из 3'));
  assert.match(await page.locator('#azTuneResult').textContent(),/200 \/ 1500 мс/);
  assert.match(await page.locator('#azTuneResult').textContent(),/Этапы: слежение на малой скорости/);
  assert.doesNotMatch(await page.locator('#azTuneResult').textContent(),/Проверенная минимальная скорость|высокая скорость/);
  assert.match(await page.locator('#azTuneResult').textContent(),/Удержание сохранено включённым/);
  assert(!(await page.locator('#azTuneApply').isDisabled()));
  await page.setViewportSize({width:320,height:950});
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  await page.setViewportSize({width:320,height:2200});
  await page.locator('#azTuneResult').screenshot({path:path.join(out,'tracking-result-320.png')});
  state.autotune={phase:'failed',active:false,axis:0,id:1,reason:'feedback_stale'};
  state.fault='az_feedback_stale';state.axes[0].motionFault='feedback_stale';await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>document.getElementById('azTuneVelocity').textContent==='—');
  assert.equal(await page.locator('#azTuneVelocity').textContent(),'—');
  delete state.axes[1].measuredVelocity;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>document.getElementById('elTuneVelocity').textContent==='—');
  assert.equal(await page.locator('#elTuneVelocity').textContent(),'—');
  assert.deepEqual(errors,[]);
  console.log('PASS: browser telemetry, zero/missing/stale speed, signed midpoint, reset/STOP requests, active-tune guards, 320/390/1100px');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
