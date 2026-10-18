const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const base=process.env.SIM_URL||'http://127.0.0.1:28080';
const out=process.env.ARTIFACT_DIR||path.resolve(__dirname,'../local-notes/settings-layout-web');
const request=async(route,body)=>{const r=await fetch(base+route,{method:'POST',body:new URLSearchParams(body)});assert(r.ok,await r.clone().text());};
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 await request('/api/settings',{model:'G5500'});
 await request('/api/azrange',{profile:'custom',start:0,span:450});await request('/api/azrange',{profile:'p45'});
 await request('/api/elrange',{profile:'custom',start:0,span:180});await request('/api/elrange',{profile:'standard'});await request('/api/calmode',{m:'2'});
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:950}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/#settings');await page.waitForFunction(()=>window._forkState&&live.ready);
  const ids=['rotorSettings','calibrationSettings','presetSettings','hudPanel','networkSettings','otaSettings','signalSettings'];
  assert.deepEqual(await page.locator('#settingsPanel > .settings-section').evaluateAll(nodes=>nodes.map(n=>n.id)),ids);
  assert.equal(await page.locator('#settingsPanel details').count(),7,'Every section has one consistent disclosure level');
  for(const id of ids){
   const section=page.locator('#'+id),summary=section.locator(':scope > details > summary');
   assert.equal(await summary.locator('h2').count(),1);assert.equal(await summary.locator('.section-caption').count(),1);
   await summary.click();assert(await section.locator('.settings-content').isVisible());
   await summary.focus();await page.keyboard.press('Space');assert(!await section.locator('.settings-content').isVisible());
  }
  const check=async()=>{
   const issues=await page.evaluate(()=>{
    const errors=[],root=document.getElementById('settingsPanel'),wrap=document.querySelector('.wrap');
    if(document.documentElement.scrollWidth>innerWidth||wrap.scrollWidth>wrap.clientWidth+1)errors.push('horizontal overflow');
    const shown=e=>e.getClientRects().length&&getComputedStyle(e).visibility!=='hidden';
    for(const field of root.querySelectorAll('.field')){
     const label=field.querySelector('label'),input=field.querySelector('input,select');
     if(label&&input&&shown(input)&&label.getBoundingClientRect().bottom>input.getBoundingClientRect().top-4)errors.push('label/input gap: '+input.id);
    }
    const buttons=[...root.querySelectorAll('button')].filter(shown);
    const texts=[],walker=document.createTreeWalker(root,NodeFilter.SHOW_TEXT);
    while(walker.nextNode()){
     const node=walker.currentNode;if(!node.textContent.trim()||node.parentElement.closest('button,select,option')||!shown(node.parentElement))continue;
     const range=document.createRange();range.selectNodeContents(node);for(const rect of range.getClientRects())if(rect.width&&rect.height)texts.push({rect,text:node.textContent.trim().slice(0,40)});
    }
    const overlap=(a,b)=>Math.min(a.right,b.right)-Math.max(a.left,b.left)>1&&Math.min(a.bottom,b.bottom)-Math.max(a.top,b.top)>1;
    for(let i=0;i<buttons.length;i++){
     const button=buttons[i],rect=button.getBoundingClientRect();
     if(button.scrollWidth>button.clientWidth+1||button.scrollHeight>button.clientHeight+1)errors.push('clipped button: '+button.textContent.trim());
     if(rect.height<44)errors.push('short button: '+button.textContent.trim());
     for(const other of buttons.slice(i+1))if(overlap(rect,other.getBoundingClientRect()))errors.push('buttons overlap');
     for(const text of texts)if(overlap(rect,text.rect))errors.push('button/text overlap: '+button.textContent.trim()+' / '+text.text);
    }
    const feedback=document.getElementById('settingsFeedback');
    if(!feedback.hidden&&wrap.getBoundingClientRect().bottom>feedback.getBoundingClientRect().top+1)errors.push('feedback overlays page');
    return errors;
   });assert.deepEqual(issues,[]);
  };
  for(const width of [320,390,600,760,1100]){
   await page.setViewportSize({width,height:950});
   await page.locator('#settingsPanel details').evaluateAll(nodes=>nodes.forEach(n=>n.open=false));
   await page.locator('.wrap').evaluate(e=>e.scrollTop=0);await page.screenshot({path:path.join(out,`overview-${width}.png`)});
   await page.locator('#settingsPanel details').evaluateAll(nodes=>nodes.forEach(n=>n.open=true));
   await page.waitForFunction(()=>{const c=document.getElementById('rchart');return c.clientWidth>0&&c.width===Math.round(c.clientWidth*devicePixelRatio);});
   assert(await page.locator('.period-options .btn').evaluateAll(nodes=>nodes.every(n=>n.getBoundingClientRect().height===44)),'Period names must fit on one line');
   await check();
   for(const [name,selector] of [['drive','#coastSettings'],['range','#azRangeForm'],['endpoints','#b2_elo'],['presets','#presetEditor'],['hud','#hudConfig'],['network','#wifiSaveButton'],['ota','#otaFile'],['signal','#rchart']]){
    await page.locator(selector).scrollIntoViewIfNeeded();
    if([320,390,1100].includes(width))await page.screenshot({path:path.join(out,`${name}-${width}.png`)});
   }
  }
  await request('/api/elrange',{profile:'custom',start:-30,span:241});await request('/api/elrange',{profile:'custom',start:-30,span:240});await request('/api/elcal',{op:'add',angle:15.5});
  await page.locator('#modem').click();await page.waitForFunction(()=>window._elCal?.points.some(p=>p.angle===15.5));
  for(const width of [320,390,1100]){
   await page.setViewportSize({width,height:950});await check();
   await page.locator('#elPointList').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`points-${width}.png`)});
   await page.evaluate(()=>settingsFeedback('Положение EL 15,5° запомнено','success'));await check();
   await page.screenshot({path:path.join(out,`feedback-${width}.png`)});
  }
  // A changed model must still hide all elevation controls after regrouping.
  await request('/api/settings',{model:'G800DXA'});await page.waitForFunction(()=>window._forkState?.model==='G800DXA');
  assert.equal(await page.locator('.el-setting:visible').count(),0);
  assert.deepEqual(errors,[]);
  const retina=await browser.newPage({viewport:{width:390,height:950},deviceScaleFactor:2});
  await retina.goto(base+'/#settings');await retina.locator('#signalSettings summary').click();
  await retina.waitForFunction(()=>{const c=document.getElementById('rchart');return devicePixelRatio===2&&c.clientWidth>0&&c.width===c.clientWidth*2;});
  await retina.locator('#rchart').screenshot({path:path.join(out,'signal-retina.png')});await retina.close();
  console.log('PASS: seven consistent disclosures, keyboard toggles, preserved forms, no button/text overlap, no clipped controls, reserved feedback area, readable responsive RSSI chart at DPR 1/2, 320/390/600/760/1100px and G800 EL hiding');
 }finally{await browser.close();await request('/api/settings',{model:'G5500'});await request('/api/elrange',{profile:'standard'});}
})().catch(e=>{console.error(e);process.exit(1)});
