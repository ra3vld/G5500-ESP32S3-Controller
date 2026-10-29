// C3 GPIO assignment through the real hardware page, using API capability metadata.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const fixture=JSON.parse(fs.readFileSync(path.join(root,'build/pin-map-8.json'),'utf8'));
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try {
  for(const mobile of [false,true]) {
   const page=await browser.newPage({viewport:{width:mobile?360:1100,height:900},locale:'ru-RU'}),errors=[];
   page.on('pageerror',e=>errors.push(e.message));
   const config={driveInterface:0,simulation:0,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:9,buttonActiveLow:1,buttonPullup:1,pwmInverted:0,pins:fixture.pins};
   const state={token:123,generation:2,recovery:false,trial:false,ready:false,reason:'none',failureCount:0,failureLimit:5,failureResetMs:900000,remainingMs:0,heapFree:80000,heapMaxBlock:50000,backend:'NONE',sppSupported:false,classicEsp32:false,crowpanel:false,crowpanelLocalDrive:false,separateDrive:true,btsDrive:true,hasConfirmed:true,configured:config};
   await page.route('http://hardware.test/**',async route=>{
    const u=new URL(route.request().url());
    if(u.pathname==='/hardware')return route.fulfill({contentType:'text/html',body:html});
    return route.fulfill({contentType:'application/json',body:JSON.stringify(u.pathname==='/api/hardware/pins'?fixture.metadata:u.pathname==='/api/hardware/modbus'?{pc:0,bus:0,address:1,node:201,first:201,count:1}:state)});
   });
   await page.goto('http://hardware.test/hardware');
   await page.waitForFunction(()=>!document.getElementById('save').disabled);
   const set=(name,value)=>page.locator(`[name="${name}"]`).selectOption(String(value));
   await set('pin.uartRx',-1);await set('pin.uartTx',-1);
   await set('pin.azNegative',20);assert(await page.locator('#save').isDisabled());
   await set('pin.azPositive',21);assert(!(await page.locator('#save').isDisabled()));
   assert.equal(await page.locator('[name="pin.azAdc"]').inputValue(),'-1');
   assert.equal(await page.locator('[name="pin.azSpeedEnable"]').inputValue(),'-1');
   assert(await page.locator('[name="pin.azNegative"] option[value="2"]').evaluate(e=>e.disabled));
   await page.locator('[name="buttonGpio"]').selectOption('8');assert(!(await page.locator('#save').isDisabled()));
   await page.locator('[name="buttonGpio"]').selectOption('9');
   await set('azSensor',1);assert(await page.locator('#save').isDisabled());
   await set('pin.azSensorSda',4);await set('pin.azSensorScl',5);
   assert(!(await page.locator('#save').isDisabled()));
   await page.locator('[name="azSensorAddress"]').fill('21');
   // The touch controller's address must not be offered as a second device.
   assert(await page.locator('#save').isDisabled());
   await page.locator('[name="azSensorAddress"]').fill('6');
   assert(!(await page.locator('#save').isDisabled()));
   assert(await page.locator('[name="pin.azNegative"] option[value="4"]').evaluate(e=>e.disabled));
   await set('pin.azSensorSda',-1);await set('pin.azSensorScl',-1);await set('azSensor',0);
   assert(!(await page.locator('#save').isDisabled()));
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);
   assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: C3 desktop/mobile GPIO assignment, relay-only form, reserved pins, free GPIO8 and shared I2C sensor');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
