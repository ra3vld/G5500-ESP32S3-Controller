const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..'),http=require('node:http');
const html=fs.readFileSync(path.join(root,'G5500_Fork/hardware_page.hpp'),'utf8').match(/R"HWHTML\(([\s\S]*?)\)HWHTML"/)[1];
const asset=fs.readFileSync(path.join(root,'G5500_Fork/web_assets.hpp'),'utf8').match(/HARDWARE_PAGE_GZIP\[\] PROGMEM = \{([\s\S]*?)\};/)[1];
const packed=Buffer.from([...asset.matchAll(/0x([0-9a-f]{2})/g)].map(m=>parseInt(m[1],16)));
(async()=>{
 const server=http.createServer((req,res)=>{res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Content-Encoding':'gzip'});res.end(packed);});
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const origin='http://127.0.0.1:'+server.address().port;
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try {
  for(const width of [1100,390]) {
   const fixture=JSON.parse(fs.readFileSync(path.join(root,'build/pin-map-4.json'),'utf8'));
   const config={driveInterface:0,simulation:0,bluetoothEnabled:0,bluetoothMode:3,forceApSta:0,buttonGpio:0,buttonActiveLow:1,buttonPullup:1,pwmInverted:0,localInputMode:0,localAzInvert:0,localElInvert:0,localAzMin:0,localAzCenter:2048,localAzMax:4095,localElMin:0,localElCenter:2048,localElMax:4095,localDeadzone:200,localHysteresis:50,azSensor:0,azSensorAddress:6,azSensorZero:0,elSensor:0,elSensorAddress:6,elSensorZero:0,pins:{...fixture.pins}};
   const state={token:123,generation:2,recovery:false,trial:false,ready:true,reason:'none',remainingMs:120000,heapFree:100000,heapMaxBlock:40000,backend:'NONE',sppSupported:true,classicEsp32:true,hasConfirmed:true,configured:config};
   let diagnostic={hasEl:true,azSensor:'MT6701',elSensor:'ADC',azRaw:16383,azMv:null,azValue:16383,elRaw:2048,elMv:1550,elValue:1550};
   const errors=[],requests=[];const page=await browser.newPage({viewport:{width,height:950},locale:'ru-RU'});
   page.on('pageerror',e=>errors.push(e.message));
   await page.route(origin+'/**',route=>{
    const request=route.request(),url=new URL(request.url());
    if(url.pathname==='/api/adcdiag')return route.fulfill({contentType:'application/json',body:JSON.stringify(diagnostic)});
    if(url.pathname==='/adc')return route.fulfill({contentType:'text/html',body:fs.readFileSync(path.join(root,'G5500_Fork/diagnostic_pages.hpp'),'utf8').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1]});
    if(url.pathname==='/hardware')return route.continue();
    if(request.method()==='POST'){requests.push(Object.fromEntries(new URLSearchParams(request.postData())));return route.fulfill({contentType:'application/json',body:'{"ok":true}'});}
    const data=url.pathname==='/api/hardware/pins'?fixture.metadata:url.pathname==='/api/hardware/modbus'?{pc:0,bus:0,address:1,node:201,first:201,count:1}:state;
    return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   });
   await page.goto(origin+'/hardware');await page.waitForFunction(()=>!document.getElementById('save').disabled);
   assert(await page.locator('#az-sensor-address').isHidden());assert(await page.locator('#el-sensor-zero').isHidden());
   const choose=(name,value)=>page.locator(`[name="${name}"]`).selectOption(String(value));
   await choose('azSensor',1);assert(await page.locator('#save').isDisabled());
   await choose('pin.azSensorSda',21);await choose('pin.azSensorScl',22);await choose('pin.azAdc',-1);
   assert(!await page.locator('#save').isDisabled());
   await choose('elSensor',2);assert.equal(await page.locator('[name=elSensorAddress]').inputValue(),'54');
   await choose('pin.elSensorSda',21);await choose('pin.elSensorScl',22);await choose('pin.elAdc',-1);
   assert(!await page.locator('#save').isDisabled()); // shared bus accepted by both dropdowns
   await choose('elSensor',1);assert(await page.locator('#save').isDisabled());
   assert.match(await page.locator('#pin-validation').textContent(),/одинаковым адресом/);
   await choose('pin.elSensorSda',23);await choose('pin.elSensorScl',18);
   assert(!await page.locator('#save').isDisabled());
   await choose('azSensor',2);await choose('elSensor',2);
   assert(!await page.locator('#save').isDisabled()); // two fixed-address sensors, separate buses
   await page.locator('[name=azSensorZero]').fill('4096');assert(await page.locator('#save').isDisabled());
   await page.locator('[name=azSensorZero]').fill('4095');assert(!await page.locator('#save').isDisabled());
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
   const out=path.join(root,'local-notes/position-sensors-web');fs.mkdirSync(out,{recursive:true});
   await page.screenshot({path:path.join(out,`hardware-${width}.png`),fullPage:true});
   await page.locator('#position-sensors').locator('..').screenshot({path:path.join(out,`sensors-${width}.png`)});
   if(width===1100&&process.env.UPDATE_HARDWARE_IMAGE)await page.screenshot({path:path.join(root,'docs/images/web-hardware.png'),fullPage:true});
   await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('message').textContent.includes('Сохранено.'));
   assert.equal(requests.length,1);assert.equal(requests[0].azSensor,'2');assert.equal(requests[0]['pin.elSensorSda'],'23');assert.equal(requests[0].azSensorZero,'4095');
   assert.equal(Object.keys(requests[0]).length,57);
   await page.goto(origin+'/adc');await page.waitForFunction(()=>document.getElementById('azraw').textContent==='16383');
   assert.match(await page.locator('#azPin').textContent(),/MT6701/);assert(await page.locator('#azlin').locator('..').isHidden());
   assert(await page.locator('#ellin').isVisible());assert.match(await page.locator('#azmv').locator('..').textContent(),/С учётом нуля/);
   diagnostic={...diagnostic,elSensor:'AS5600',elRaw:-1,elValue:-1,elMv:null};await page.evaluate(()=>upd());
   assert.match(await page.locator('#elraw').textContent(),/Ошибка чтения/);assert(await page.locator('#chart').isHidden());
   const main=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8');
   await page.setContent('<div id="az2PointList"></div><div id="az2CalProgress"></div>');
   await page.addScriptTag({content:main.slice(main.indexOf('function renderCalibrationPoints('),main.indexOf('function renderAxisCalibration('))});
   await page.evaluate(()=>{window._forkState={azSensor:'MT6701'};renderCalibrationPoints('az','2',[{angle:0,mv:1000},{angle:450,mv:16383}],false,false);});
   assert.match(await page.locator('#az2PointList').textContent(),/16383 отсч\./);assert(!/мВ/.test(await page.locator('#az2PointList').textContent()));
   await page.evaluate(()=>{window._forkState={azSensor:'ADC'};renderCalibrationPoints('az','2',[{angle:0,mv:1000},{angle:450,mv:16383}],false,false);});
   assert.match(await page.locator('#az2PointList').textContent(),/16383 мВ/);
   assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: runtime sensor selection, shared/separate I2C, address/zero validation, POST and diagnostics at 1100/390 px');
 }finally{await browser.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
})().catch(e=>{console.error(e);process.exitCode=1;});
