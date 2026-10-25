// Recovery must allow simulation without compatible physical drive/sensor wiring.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const profile of ['1','5','8'])for(const width of [390,1100]){
   const fixture=JSON.parse(fs.readFileSync(path.join(root,`build/pin-map-${profile}.json`)));
   const config={driveInterface:2,simulation:1,pwmInverted:1,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:fixture.buttonGpio,buttonActiveLow:1,buttonPullup:1,azSensor:2,elSensor:2,azSensorAddress:54,elSensorAddress:54,pins:{...fixture.pins}};
   for(const d of fixture.metadata.roles)if(['drive','sensorAz','sensorEl'].includes(d.group))config.pins[d.name]=1;
   const state={role:'controller',token:123,generation:2,recovery:true,trial:false,ready:false,reason:'invalid_config',heapFree:100000,heapMaxBlock:40000,backend:'NONE',classicEsp32:false,sppSupported:false,configured:config};
   const posts=[],errors=[],page=await browser.newPage({viewport:{width,height:950}});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://simulation.test/**',route=>{
    const req=route.request(),url=new URL(req.url());
    if(url.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:html});
    if(req.method()==='POST'){posts.push(Object.fromEntries(new URLSearchParams(req.postData())));return route.fulfill({json:{ok:true}});}
    return route.fulfill({json:url.pathname==='/api/hardware/pins'?fixture.metadata:url.pathname==='/api/hardware/modbus'?{pc:0,bus:0,address:1,node:201,first:201,count:1}:state});
   });
   await page.goto('http://simulation.test/recovery');await page.waitForFunction(()=>!document.getElementById('save').disabled);
   const mode=page.locator('[name=driveMode]');
   assert.equal(await mode.inputValue(),'simulation');
   assert.equal(await page.locator('#hardware fieldset').first().locator('select:visible').count(),1);
   assert(await page.locator('#position-sensors').isHidden());assert(await page.locator('[data-pin-group=drive]').isHidden());
   assert.equal(await page.locator('[name="pin.azAdc"]').inputValue(),'-1','Simulation releases the saved ADC');
   await page.evaluate(()=>refresh());assert.equal(await mode.inputValue(),'simulation');
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   if(profile==='5'){
    const out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/simulation-config-web');fs.mkdirSync(out,{recursive:true});
    await page.screenshot({path:path.join(out,`crowpanel-recovery-${width}.png`)});
   }
   await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено'));
   assert.equal(posts.length,1);assert.equal(posts[0].simulation,'1');assert.equal(posts[0].driveInterface,'2');assert.equal(posts[0].pwmInverted,'1');
   for(const d of fixture.metadata.roles)if(await page.locator(`[name="pin.${d.name}"]`).count())assert.equal(posts[0]['pin.'+d.name],['drive','sensorAz','sensorEl'].includes(d.group)?'-1':String(config.pins[d.name]));
   await page.evaluate(()=>{busy=false;dirty=false;return refresh();});
   await mode.selectOption('0');assert(await page.locator('#save').isDisabled());assert(await page.locator('#position-sensors').isVisible());
   await mode.selectOption('simulation');assert(!await page.locator('#save').isDisabled());
   await page.locator('[name=buttonGpio]').selectOption('0');
   await page.evaluate(()=>{setPinValue(form.elements['pin.led'],0);updatePins();});
   assert(await page.locator('#save').isDisabled(),'Active hardware remains validated in simulation');
   assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: one drive selector, simulation with incompatible saved wiring, released GPIO in POST, physical revalidation, active GPIO safety at 390/1100 px');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
