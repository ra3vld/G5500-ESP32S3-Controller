// Exercise the actual settings form with a stubbed status/settings transport.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const status=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
status.build.display='CROWPANEL_21';status.build.drive='SIMULATED';status.encoderInverted=true;status.encoderStepDegrees=20;
(async()=>{
 const browser=await chromium.launch({...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{}),headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage(),errors=[],requests=[];let failure=false;
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('**/*',async route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/api/status')return route.fulfill({json:status});
   if(url.pathname==='/api/rssi')return route.fulfill({json:{values:[],min:null,max:null,avg:null,count:0}});
   if(url.pathname==='/api/settings'){
    const form=new URLSearchParams(route.request().postData());requests.push(form);
    if(failure)return route.fulfill({status:503,json:{ok:false,result:'motion_storage_failed'}});
    if(form.has('encoderInverted'))status.encoderInverted=form.get('encoderInverted')==='true';
    if(form.has('encoderStepDegrees'))status.encoderStepDegrees=Number(form.get('encoderStepDegrees'));
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,items:[],values:[],samples:[]}});
   if(url.pathname==='/hardware')return route.fulfill({contentType:'text/html',body:'<!doctype html><html><body></body></html>'});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  async function open(){
   await page.goto('http://controller.test/');
   await page.waitForFunction(()=>!!window._forkState);
   await page.getByRole('tab',{name:'Настройки',exact:true}).click();
   await page.locator('#equipmentSettings > details > summary').click();
  }
  await open();
  const box=page.locator('[name=encoderInverted]');
  const step=page.locator('[name=encoderStepDegrees]'),save=page.getByRole('button',{name:'Сохранить энкодер',exact:true});
  assert(await box.isVisible());assert(await box.isChecked());
  assert.equal(await step.inputValue(),'20');
  await step.fill('2.5');await page.evaluate(()=>upd(true));assert.equal(await step.inputValue(),'2.5');
  await step.fill('0');assert.equal(await step.evaluate(e=>e.checkValidity()),false);
  const invalidCount=requests.length;await save.click();assert.equal(requests.length,invalidCount);
  await step.fill('2.5');
  await box.uncheck();
  await save.click();
  await page.waitForFunction(()=>window._forkState.encoderInverted===false);
  assert.equal(requests.at(-1).get('encoderInverted'),'false');
  assert.equal(requests.at(-1).get('encoderStepDegrees'),'2.5');
  await open();assert.equal(await box.isChecked(),false);assert.equal(await step.inputValue(),'2.5');
  await box.check();
  await save.click();
  await page.waitForFunction(()=>window._forkState.encoderInverted===true);
  assert.equal(requests.at(-1).get('encoderInverted'),'true');
  failure=true;await step.fill('3.5');await save.click();
  await page.waitForFunction(()=>document.getElementById('settingsFeedback').dataset.state==='error');
  assert.equal(await step.inputValue(),'3.5');assert(await page.locator('#forkSettings').getAttribute('data-dirty'));
  failure=false;
  status.deviceRole='hud';await open();await step.fill('0.5');await save.click();
  await page.waitForFunction(()=>window._forkState.encoderStepDegrees===.5);
  assert.equal(requests.at(-1).get('encoderStepDegrees'),'0.5');assert.equal(requests.at(-1).has('model'),false);
  await open();assert.equal(await step.inputValue(),'0.5');
  const out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/encoder-step-web');fs.mkdirSync(out,{recursive:true});
  for(const width of [390,1100]){
   await page.setViewportSize({width,height:1000});
   assert(await page.evaluate(()=>document.querySelector('.wrap').scrollWidth<=document.querySelector('.wrap').clientWidth));
   await page.locator('#encoderSettings').screenshot({path:path.join(out,`encoder-${width}.png`)});
  }
  status.deviceRole='controller';
  status.model='G800DXA';status.build.singleAxis=true;status.build.drive='BTS7960';
  await open();
  await page.locator('#rotorSettings > details').evaluate(e=>e.open=true);
  assert.equal(await page.locator('[name=model] option[value="G5500"]').evaluate(e=>e.disabled),true);
  assert.equal(await page.locator('[name=model]').inputValue(),'G800DXA');
  status.build.singleAxis=false;status.build.drive='SIMULATED';
  await open();
  await page.locator('#rotorSettings > details').evaluate(e=>e.open=true);
  assert.equal(await page.locator('[name=model] option[value="G5500"]').evaluate(e=>e.disabled),false);
  status.build.display='NONE';status.build.drive='YAESU';delete status.encoderInverted;
  await open();assert.equal(await box.isVisible(),false);
  assert(await step.isDisabled());
  await page.locator('#rotorSettings > details').evaluate(e=>e.open=true);
  const count=requests.length;
  await page.getByRole('button',{name:'Сохранить настройки',exact:true}).click();
  await page.waitForResponse(r=>r.url().endsWith('/api/status'));
  assert.equal(requests.length,count+1);assert.equal(requests.at(-1).has('encoderInverted'),false);
  assert.equal(requests.at(-1).has('encoderStepDegrees'),false);
  assert.deepEqual(errors,[]);
  console.log('PASS: encoder step/inversion, validation, dirty polling, save/error/reload, HUD payload, hidden on other boards, 390/1100px');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
