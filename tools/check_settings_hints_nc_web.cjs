const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const base=process.env.SIM_URL||'http://127.0.0.1:8080';
const out=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../local-notes/settings-hints-nc-web');
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:950}}),errors=[];
  page.setDefaultTimeout(10000);page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>window.G5500_HTTP_ONLY=true);
  let azCal=false,elCal=true,online=true,hud=false,model='G5500';
  await page.route('**/api/status',async route=>{
   const r=await route.fetch(),j=await r.json();j.model=model;j.deviceRole=hud?'hud':'controller';j.hudOnline=online;
   j.az=123.4;j.el=56.7;j.axes[0].calibrated=azCal;j.axes[1].calibrated=elCal;
   j.selectedCalOk=azCal&&(model==='G800DXA'||elCal);
   j.targetValid=false;j.calMode='2';await route.fulfill({json:j});
  });
  await page.goto(base+'/#settings');await page.waitForFunction(()=>window._forkState);
  const missing=await page.locator('#settingsPanel input,#settingsPanel select').evaluateAll(fields=>fields.flatMap(field=>{
   const ids=(field.getAttribute('aria-describedby')||'').split(/\s+/).filter(Boolean);
   return ids.length&&ids.every(id=>document.getElementById(id)?.textContent.trim().length>20)?[]:[field.id||field.name];
  }));assert.deepEqual(missing,[],'Every setting must have associated explanatory text');
  for(const id of ['azm36','azm45','azmc','elmstd','elmc','mode2','modem'])assert(await page.locator('#'+id).getAttribute('aria-describedby'));
  assert.equal(await page.locator('#az').innerText(),'NC');assert.equal(await page.locator('#el').innerText(),'56.7');
  assert.equal(await page.locator('#azt').innerText(),'---','No target is not an uncalibrated current position');
  await page.locator('#caltoggle').click();
  assert.equal(await page.locator('#panel2 [data-cal-position=az]').innerText(),'NC');
  assert(await page.locator('#panel2 [data-cal-position=az] + span').isHidden(),'NC must not have a degree suffix');
  azCal=true;elCal=false;await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>el.textContent==='NC'&&az.textContent==='123.4');
  hud=true;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>window._forkState.deviceRole==='hud');
  assert.equal(await page.locator('#el').innerText(),'NC');
  online=false;await page.evaluate(()=>upd(true));await page.waitForFunction(()=>az.textContent==='—'&&el.textContent==='—');
  hud=false;online=true;azCal=true;elCal=true;await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>az.textContent==='123.4'&&el.textContent==='56.7');
  const peerText=await page.evaluate(()=>hudRow({id:'test',name:'Test',online:true,ageMs:0,az:100,el:20,azCalibrated:false,elCalibrated:true,hasEl:true},true).textContent);
  assert(peerText.includes('AZ NC / EL 20.0°'));
  await page.locator('#settingsPanel details').evaluateAll(nodes=>nodes.forEach(n=>n.open=true));
  assert.equal(await page.locator('#speedEnabled-help').isVisible(),await page.locator('#analogSpeedToggle').isVisible());
  for(const width of [320,390,1100]){
   await page.setViewportSize({width,height:950});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth&&document.querySelector('.wrap').scrollWidth<=document.querySelector('.wrap').clientWidth));
   // Stock G-5500 hides optional speed controls; coasting is available for every drive.
   for(const [name,selector] of [['drive','#coastDegrees'],['range','#azRangeForm-start'],['network','#ssid']]){
    await page.locator(selector).scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${name}-${width}.png`)});
   }
  }
  azCal=false;elCal=false;await page.evaluate(()=>upd(true));await page.getByRole('tab',{name:'Управление',exact:true}).click();
  await page.waitForFunction(()=>az.textContent==='NC'&&el.textContent==='NC');
  await page.screenshot({path:path.join(out,'nc-both-1100.png')});
  model='G800DXA';await page.evaluate(()=>upd(true));await page.waitForFunction(()=>window._forkState.model==='G800DXA');
  assert(!await page.locator('#el').isVisible());assert.equal(await page.locator('#az').innerText(),'NC');
  assert.deepEqual(errors,[]);
  console.log('PASS: help for every setting, NC per axis in control/calibration/HUD, offline vs NC, unchanged target placeholder, calibration recovery, G800 and responsive hints');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
