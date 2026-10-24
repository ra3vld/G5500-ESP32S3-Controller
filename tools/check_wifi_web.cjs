// Wi-Fi form regression: saved SSID, encoding, and visible save failures.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8');
const html=source.split('R"HTML(')[1].split(')HTML";')[0];
const status=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
status.ssid='Saved network';
status.wifiMode='Wi-Fi Client';status.ip=status.staIp='192.168.1.42';status.apIp='-';status.connectedSsid=status.ssid;status.wifiConnecting=false;
status.rssi=-62;status.heapFree=65536;status.uptimeSeconds=90061;

(async()=>{
 const browser=await chromium.launch({...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{}),headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[],requests=[];
  page.setDefaultTimeout(15000);
  page.on('pageerror',error=>{errors.push(error.message);console.error('PAGE ERROR:',error.message);});
  page.on('dialog',dialog=>dialog.accept());
  let saveSucceeds=false,hangSave=false,hangStatus=false,statusRequests=0;
  await page.route('**/*',async route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/api/status'){
    ++statusRequests;
    if(hangStatus)return;
    return route.fulfill({json:status});
   }
   if(url.pathname==='/api/rssi')return route.fulfill({json:{values:[],min:null,max:null,avg:null,count:0}});
   if(url.pathname==='/api/wifi'){
    requests.push({method:route.request().method(),url,form:new URLSearchParams(route.request().postData())});
    if(hangSave)return;
    return route.fulfill({status:saveSucceeds?200:500,contentType:'text/plain; charset=utf-8',body:saveSucceeds?'Настройки Wi-Fi сохранены. Подключаемся.':'Не удалось сохранить настройки Wi-Fi. Подключение не изменено.'});
   }
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://controller.test/');
  await page.waitForFunction(()=>document.getElementById('ssid').value==='Saved network');
  assert.equal(await page.locator('#statusWifi').textContent(),'Подключён\nSTA: 192.168.1.42');
  assert.equal(await page.locator('#statusSignal').textContent(),'-62 dBm');
  assert.equal(await page.locator('#statusMemory').textContent(),'64.0 КиБ');
  assert.equal(await page.locator('#statusUptime').textContent(),'1 д 01:01:01');
  assert.equal(await page.locator('#settingsPanel').isVisible(),false);
  assert.equal(await page.locator('#controlPanel form').count(),0,'editors belong to Settings');
  for(const [name,href] of [['ADC TEST','/adc'],['RESET DIAG','/resetdiag'],['DIAG CONSOLE','/diaglog']]){
   const link=page.getByRole('link',{name,exact:true});
   assert.equal(await link.isVisible(),true);
   assert.equal(await link.getAttribute('href'),href);
  }
  assert.equal(await page.locator('#controlPanel').evaluate(e=>e.lastElementChild.id),'currentStatus');
  fs.mkdirSync(path.join(root,'artifacts/screenshots'),{recursive:true});
  assert.equal(await page.locator('#networkSettings > details').getAttribute('open'),'');
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:844});
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,`control overflow at ${width}`);
   await page.screenshot({path:path.join(root,`artifacts/screenshots/control-tab-${width}.png`),fullPage:true});
  }
  await page.getByRole('tab',{name:'Управление',exact:true}).focus();
  await page.keyboard.press('ArrowRight');
  assert.equal(await page.getByRole('tab',{name:'Настройки',exact:true}).getAttribute('aria-selected'),'true');
  assert.equal(await page.locator('#controlPanel').isVisible(),false);
  assert.equal(await page.locator('.nav a:visible').count(),4,'diagnostics and setup remain available in Settings');
  await page.locator('#programSettings details').evaluate(e=>e.open=true);
  await page.locator('#forkSettings-rotorName').fill('Unsaved name');
  await page.getByRole('tab',{name:'Управление',exact:true}).click();
  await page.waitForTimeout(600);
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  assert.equal(await page.locator('#forkSettings-rotorName').inputValue(),'Unsaved name');
  await page.reload();
  await page.waitForFunction(()=>!!window._forkState);
  assert.equal(await page.locator('#settingsPanel').isVisible(),true,'reload keeps the Settings tab');
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:844});
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,`settings overflow at ${width}`);
   await page.screenshot({path:path.join(root,`artifacts/screenshots/settings-tab-${width}.png`),fullPage:true});
  }
  assert.equal(await page.locator('#apip').textContent(),'-');
  assert.equal(await page.locator('#mode').textContent(),'Wi-Fi Client');
  status.wifiMode='Access Point';status.ip=status.apIp='192.168.4.1';status.staIp='-';status.connectedSsid='';status.rssi=null;status.uptimeSeconds=0;status.heapFree=0;
  status.btLinkConnected=true;status.btConnected=false;status.build.bluetooth=true;status.build.bluetoothBackend='ESP32_SPP';
  await page.waitForFunction(()=>document.getElementById('apip').textContent==='192.168.4.1');
  assert.equal(await page.locator('#mode').textContent(),'Access Point');
  assert.equal(await page.locator('#statusWifi').textContent(),'Точка доступа\nAP: 192.168.4.1');
  assert.equal(await page.locator('#ip').textContent(),'-','AP address must not be labelled as a client address');
  assert.match(await page.locator('#rotatorConnection').textContent(),/AP: 192\.168\.4\.1/);
  assert.equal(await page.locator('#statusSignal').textContent(),'—');
  assert.equal(await page.locator('#statusBt').textContent(),'Связь без SPP');
  assert.equal(await page.locator('#statusMemory').textContent(),'0.0 КиБ');
  assert.equal(await page.locator('#statusUptime').textContent(),'00:00:00');
  const ssid='Тестовая сеть & +',password='dummy=&+?% password';
  await page.locator('#networkSettings > details').evaluate(e=>e.open=true);
  await page.locator('#ssid').fill(ssid);
  await page.locator('#pass').fill(password);
  await page.waitForTimeout(600);
  assert.equal(await page.locator('#ssid').inputValue(),ssid,'polling must preserve typed SSID');
  const save=page.locator('#wifiSaveButton');
  await save.click();
  await page.waitForFunction(()=>document.getElementById('wifiMessage').textContent.includes('Не удалось сохранить Wi-Fi'));
  assert.equal(requests[0].method,'POST');
  assert.equal(requests[0].url.search,'');
  assert.equal(requests[0].form.get('ssid'),ssid);
  assert.equal(requests[0].form.get('pass'),password);
  assert.equal(await page.locator('#pass').inputValue(),password,'failed save must allow retry');
  saveSucceeds=true;
  await save.click();
  await page.waitForFunction(()=>document.getElementById('wifiMessage').textContent.includes('сохранены'));
  assert.equal(await page.locator('#pass').inputValue(),'');
  status.ssid=ssid;status.wifiConnecting=true;
  await page.waitForFunction(()=>window._forkState.ssid==='Тестовая сеть & +');
  assert.match(await page.locator('#wifiMessage').textContent(),/Полученный IP появится/);
  // Wrong credentials preserve the setup page and explain the automatic retry.
  status.wifiConnecting=false;
  await page.waitForFunction(()=>wifiMessage.textContent.includes('пока не удалось'));
  assert(await page.locator('#ssid').isVisible());
  status.wifiConnecting=true;
  status.wifiMode='AP + Wi-Fi Client';status.ip=status.staIp='10.20.30.40';status.apIp='192.168.4.1';status.rssi=-55;
  status.connectedSsid=ssid;status.wifiConnecting=false;
  await page.waitForFunction(()=>wifiMessage.textContent.includes('Полученный IP: 10.20.30.40'));
  assert.match(await page.locator('#wifiMessage').textContent(),/Точка доступа: 192\.168\.4\.1/);
  assert.equal(await page.locator('#wifiClientLink').getAttribute('href'),'http://10.20.30.40/');
  assert(await page.locator('#wifiClientLink').isVisible());
  assert.equal(await page.locator('#ip').textContent(),'10.20.30.40');
  for(const width of [1100,390,320]){
   await page.setViewportSize({width,height:844});
   for(const tab of ['control','settings']){
    await page.evaluate(tab=>selectTab(tab),tab);
    assert.match(await page.locator('#rotatorConnection').innerText(),/STA: 10\.20\.30\.40 · AP: 192\.168\.4\.1/);
    assert.equal(await page.locator('#statusWifi').textContent(),'Подключён + AP\nSTA: 10.20.30.40\nAP: 192.168.4.1');
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth&&document.querySelector('.wrap').scrollWidth<=document.querySelector('.wrap').clientWidth),`AP+STA overflow at ${width}`);
    await page.locator(tab==='control'?'#currentStatus':'#wifiMessage').scrollIntoViewIfNeeded();
    await page.screenshot({path:path.join(root,`local-notes/network-ip-20261019/${tab}-connected-${width}.png`)});
   }
  }
  await page.reload();await page.waitForFunction(()=>window._forkState?.staIp==='10.20.30.40');
  assert(await page.locator('#networkSettings > details').evaluate(e=>e.open));
  assert.equal(await page.locator('#wifiClientLink').getAttribute('href'),'http://10.20.30.40/');
  await page.locator('#ssid').fill('');
  await save.click();
  assert.match(await page.locator('#wifiMessage').textContent(),/Укажите SSID/);
  assert.equal(requests.length,2,'empty SSID must not request a reboot');
  await page.clock.install();
  await page.locator('#ssid').fill(ssid);
  hangSave=true;
  await save.click();
  await page.waitForFunction(()=>document.getElementById('wifiSaveButton').disabled);
  await page.waitForTimeout(50);
  const beforeDuplicate=requests.length;
  await page.evaluate(()=>wifi());
  assert.equal(requests.length,beforeDuplicate,'pending save must not be submitted twice');
  await page.clock.runFor(12500);
  await page.waitForFunction(()=>document.getElementById('wifiMessage').textContent.includes('Подтверждение не получено'));
  assert.equal(await save.isDisabled(),false);
  assert.equal(requests.length,3,'timeout must not retry the save automatically');
  hangStatus=true;
  await page.evaluate(()=>{void upd();});
  // Leave the pending promise running while several polling intervals elapse.
  const initialStatusRequests=statusRequests;
  await page.clock.runFor(3000);
  assert.ok(statusRequests<=initialStatusRequests+1,'status polling must not overlap requests');
  await page.clock.runFor(4000);
  assert.match(await page.locator('#rotatorConnection').innerText(),/Последние адреса: STA: 10\.20\.30\.40 · AP: 192\.168\.4\.1/);
  assert.match(await page.locator('#statusWifi').innerText(),/Последние адреса:/);
  // LIVE heartbeats do not carry network state. Setup must still poll HTTP
  // promptly and publish the DHCP address while the WebSocket stays healthy.
  const livePage=await browser.newPage({viewport:{width:320,height:844}});
  livePage.setDefaultTimeout(10000);livePage.on('pageerror',e=>errors.push(e.message));livePage.on('dialog',d=>d.accept());
  let setupState={...status,ssid:'',connectedSsid:'',wifiConnecting:false,staIp:'-',ip:'192.168.4.1',apIp:'192.168.4.1',wifiMode:'Access Point',rssi:null,livePort:81};
  let socket,revision=0,livePolls=0;
  await livePage.routeWebSocket('**/api/live',ws=>socket=ws);
  await livePage.route('**/*',route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/api/status'){livePolls++;return route.fulfill({json:setupState});}
   if(url.pathname==='/api/rssi')return route.fulfill({json:{values:[]}});
   if(url.pathname==='/api/wifi'){setupState={...setupState,ssid:'New home',wifiConnecting:true};return route.fulfill({body:'SAVED'});}
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  const heartbeat=setInterval(()=>socket?.send(JSON.stringify({v:1,boot:1,type:'state',revision:++revision,state:{}})),100);
  try{
   await livePage.goto('http://controller.test/#settings');await livePage.waitForFunction(()=>live.ready);
   await livePage.locator('#ssid').fill('New home');await livePage.locator('#pass').fill('test password');
   await livePage.locator('#wifiSaveButton').click();
   await livePage.waitForFunction(()=>wifiMessage.textContent.includes('Полученный IP появится'));
   const before=livePolls;
   setupState={...setupState,wifiConnecting:false,connectedSsid:'New home',staIp:'172.16.2.99',ip:'172.16.2.99',wifiMode:'AP + Wi-Fi Client',rssi:-60};
   await livePage.waitForFunction(()=>wifiMessage.textContent.includes('Полученный IP: 172.16.2.99'));
   assert(livePolls>before);assert(await livePage.evaluate(()=>live.ready));
   assert.match(await livePage.locator('#rotatorConnection').innerText(),/STA: 172\.16\.2\.99 · AP: 192\.168\.4\.1/);
  }finally{clearInterval(heartbeat);await livePage.close();}
  await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  status.ethernetSupported=true;status.ethStarted=false;status.ethLink=false;status.ethConnected=false;
  status.staIp='-';status.connectedSsid='';status.rssi=null;status.wifiMode='Access Point';status.apIp='192.168.4.1';
  await page.evaluate(s=>renderStatus(s),status);
  assert.equal(await page.locator('#ethernetStatus').isVisible(),true);
  assert.equal(await page.locator('#ethstate').innerText(),'Ошибка запуска');
  status.ethStarted=true;status.ethLink=true;await page.evaluate(s=>renderStatus(s),status);
  assert.equal(await page.locator('#ethstate').innerText(),'Ожидание DHCP');
  status.ethConnected=true;status.ethIp='10.0.1.50';status.ip=status.ethIp;status.wifiMode='Ethernet + AP';
  await page.evaluate(s=>renderStatus(s),status);
  assert.equal(await page.locator('#ethip').innerText(),'10.0.1.50');
  assert.equal(await page.locator('#ethstate').innerText(),'Подключён');
  assert.match(await page.locator('#statusWifi').innerText(),/ETH: 10\.0\.1\.50/);
  assert.equal(await page.locator('#statusWifi').getAttribute('class'),'online');
  status.ethLink=false;status.ethConnected=false;status.ethIp='-';status.wifiMode='Access Point';
  await page.evaluate(s=>renderStatus(s),status);
  assert.equal(await page.locator('#ethstate').innerText(),'Нет кабеля');
  assert.doesNotMatch(await page.locator('#statusWifi').innerText(),/ETH:/);
  assert.deepEqual(errors,[]);
  console.log('PASS: first setup IP, simultaneous STA/AP addresses, default open network settings, mobile layouts, STA/AP/Ethernet status and DHCP/cable transitions, Wi-Fi POST, encoded credentials, failure/success, stalled save timeout, duplicate-save guard and single status request');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
