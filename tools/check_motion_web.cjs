// Browser regression against virtual motors only.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const base=process.env.SIM_URL||'http://127.0.0.1:28880';
async function post(path,body,status=200){const r=await fetch(base+path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body)});assert.equal(r.status,status,await r.clone().text());return r.json()}
async function state(){return(await fetch(base+'/api/status')).json()}
(async()=>{
 await post('/api/move',{d:'S'});await post('/api/settings',{model:'G800DXA',speedEnabled:'true'});
 await post('/api/settings',{azRegulatorMode:'simple',azMotionReady:'false'});
 await post('/api/target',{az:90});await post('/api/move',{d:'S'});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:900}}),errors=[],forms=[];
  page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(r.url().endsWith('/api/settings')&&r.method()==='POST')forms.push(new URLSearchParams(r.postData()))});
  await page.goto(base);await page.waitForFunction(()=>window._forkState?.axes?.[0]?.motionReady===true);
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  await page.locator('#motionSettings details').evaluate(e=>e.open=true);
  await page.waitForFunction(()=>document.getElementById('azMotionVelocity').value!=='');
  assert.equal(await page.locator('#azRegulatorMode').inputValue(),'simple');
  assert.equal(await page.locator('[name=azMotionReady]').count(),0);
  assert.equal(await page.locator('#azMotionVelocity').isVisible(),false);
  for(const name of ['coastDegrees','accelerationMs','decelerationMs','decelerationFactor','speedPercent','slowdownDegrees'])assert.equal(await page.locator(`#azMotionForm [name=${name}]`).isVisible(),true,name);
  await page.locator('#azMotionForm [name=coastDegrees]').fill('3.5');
  await page.locator('#azMotionForm [name=accelerationMs]').fill('2600');
  await page.locator('#azMotionForm [name=decelerationMs]').fill('1700');
  await new Promise(r=>setTimeout(r,700));assert.equal(await page.locator('#azMotionForm [name=coastDegrees]').inputValue(),'3.5','polling must preserve edits');
  await page.locator('[name=azMotionKickEnabled]').check();
  await page.locator('#azMotionKickPercent').fill('50');
  await page.locator('#azMotionForm button[type=submit]').click();
  await page.waitForFunction(()=>window._forkState?.axes?.[0]?.motionReady===true);
  let config=await(await fetch(base+'/api/motion')).json();assert.equal(config.axes[0].kickEnabled,true);assert.equal(config.axes[0].KickPercent,50);
  await page.waitForFunction(()=>window._forkState?.coastDegrees===3.5&&window._forkState?.accelerationMs===2600&&window._forkState?.decelerationMs===1700);
  await post('/api/settings',{azSpeedPercent:'5',azApproachSpeedPercent:'5'});
  await page.setViewportSize({width:390,height:1000});await page.locator('#azMotionForm .simple-motion-fields').evaluate(e=>window.scrollTo(0,scrollY+e.getBoundingClientRect().top-20));await page.screenshot({path:'build/motion/simple-ui-390-top.png'});await page.locator('#coastDegrees').evaluate(e=>window.scrollTo(0,scrollY+e.getBoundingClientRect().top-170));await page.screenshot({path:'build/motion/simple-ui-390-bottom.png'});await page.setViewportSize({width:1100,height:900});
  await page.locator('#azRegulatorMode').selectOption('cascade');
  assert.equal(await page.locator('#azMotionVelocity').isVisible(),true);
  await page.locator('#azMotionVelocity').fill('1.25');await page.locator('[name=azMotionHoldEnabled]').check();
  await page.locator('#azMotionForm button[type=submit]').click();
  await page.waitForFunction(()=>window._forkState?.axes?.[0]?.regulatorMode==='cascade');
  config=await(await fetch(base+'/api/motion')).json();assert.equal(config.axes[0].Velocity,1.25);assert.equal(config.axes[0].holdEnabled,true);
  assert.equal((await state()).moving,false,'saving must not start movement');
  assert.equal(await page.locator('#coastDegrees').isVisible(),false);
  assert.equal(await page.locator('#elMotionForm').isVisible(),false);
  await page.waitForFunction(()=>!document.getElementById('azMotionForm').dataset.dirty);
  await page.locator('#settingsFeedback').waitFor({state:'visible'});
  await page.getByRole('button',{name:'Закрыть уведомление'}).click();
  await page.locator('#settingsFeedback').waitFor({state:'hidden'});
  fs.mkdirSync('build/motion',{recursive:true});
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:1000});await page.locator('#motionSettings summary').scrollIntoViewIfNeeded();
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,`overflow at ${width}`);
   assert.equal(await page.locator('#azMotionForm').evaluate(f=>[...f.querySelectorAll('input,select,button')].filter(e=>e.getClientRects().length).every(e=>e.getBoundingClientRect().left>=0&&e.getBoundingClientRect().right<=innerWidth)),true);
   await page.screenshot({path:`build/motion/web-${width}.png`});
  }
  assert(forms.length>=2);for(const form of forms)assert([...form].length<=32);
  const reset=page.waitForResponse(r=>r.url().endsWith('/api/motion/reset'));await page.locator('#azMotionForm button').filter({hasText:'Сбросить ошибку'}).click();assert.equal((await reset).status(),200);
  await post('/api/mode',{mode:'manual'});await post('/api/target',{az:(await state()).az});
  await page.getByRole('tab',{name:'Управление',exact:true}).click();
  await page.waitForFunction(()=>window._forkState?.axes?.[0]?.motionPhase==='holding');
  assert.equal(await page.locator('#executeTarget').textContent(),'stop','stationary hold must expose STOP');
  await page.locator('#executeTarget').click();
  await page.waitForFunction(()=>!window._forkState?.axes?.[0]?.holdArmed&&!window._forkState?.moving);
  assert.deepEqual(errors,[]);console.log('PASS: automatic motion readiness, 50% kick with 5% work, cascade/hold settings and stationary STOP, bounded forms, no save-start, 1100/390/320px');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
