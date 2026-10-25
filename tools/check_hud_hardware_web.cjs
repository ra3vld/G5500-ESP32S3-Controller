// HUD validates its own peripherals, including in recovery, without a local drive.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  for(const profile of ['1','5','8'])for(const recovery of [false,true]){
   const fixture=JSON.parse(fs.readFileSync(path.join(root,`build/pin-map-${profile}.json`)));
   const free=fixture.buttonGpio>=0?fixture.buttonGpio:0;
   const config={driveInterface:0,simulation:0,pwmInverted:1,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:-1,buttonActiveLow:1,buttonPullup:1,azSensor:2,elSensor:2,azSensorAddress:1,elSensorAddress:1,azSensorZero:9000,elSensorZero:9000,pins:{...fixture.pins}};
   for(const d of fixture.metadata.roles)if(['drive','sensorAz','sensorEl'].includes(d.group))config.pins[d.name]=free;
   const state={role:'hud',token:123,generation:2,recovery,trial:false,ready:true,reason:recovery?'requested':'none',heapFree:100000,heapMaxBlock:40000,backend:'NONE',classicEsp32:false,sppSupported:false,configured:config};
   const posts=[],errors=[],page=await browser.newPage({viewport:{width:390,height:950}});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://hud-hardware.test/**',route=>{
    const req=route.request(),url=new URL(req.url());
    if(url.pathname==='/hardware'||url.pathname==='/recovery')return route.fulfill({contentType:'text/html',body:html});
    if(req.method()==='POST'){posts.push(Object.fromEntries(new URLSearchParams(req.postData())));return route.fulfill({json:{ok:true}});}
    return route.fulfill({json:url.pathname==='/api/hardware/pins'?fixture.metadata:url.pathname==='/api/hardware/modbus'?{pc:0,bus:0,address:1,node:201,first:201,count:1}:state});
   });
   await page.goto('http://hud-hardware.test/'+(recovery?'recovery':'hardware'));
   await page.waitForFunction(()=>!document.getElementById('save').disabled);
   assert(await page.locator('[name=driveMode]').isHidden());assert(await page.locator('#position-sensors').isHidden());
   assert(await page.locator('[data-pin-group=drive]').isHidden());
   await page.locator('[name=buttonGpio]').selectOption(String(free));
   assert(!await page.locator('#save').isDisabled(),'Unused local drive does not reserve a button GPIO');
   await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Сохранено'));
   assert.equal(posts.length,1);assert.equal(posts[0].simulation,'0');assert.equal(posts[0].buttonGpio,String(free));
   await page.evaluate(()=>{busy=false;setPinValue(form.elements['pin.uartRx'],-1);setPinValue(form.elements['pin.uartTx'],0);updatePins();});
   assert(await page.locator('#save').isDisabled());assert.match(await page.locator('#pin-validation').textContent(),/оба пина UART/);
   assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: HUD hardware/recovery without simulation, ignored local drive/sensors, free GPIO and active UART validation');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
