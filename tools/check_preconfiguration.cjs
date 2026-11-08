// Browser acceptance for optional settings, wire framing, retries and consent.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {chromium} = require('playwright');
const root = path.resolve(__dirname, '..');
const directory = process.env.WEB_FLASHER_SITE || path.join(root, 'dist/web-flasher');
const output = path.join(root, 'artifacts/preconfiguration');
fs.mkdirSync(output, {recursive:true});

function fakeSerial() {
  const state = window.usbFixture = {name:'G5500', id:'aabbccddeeff', calls:[], writes:[], opens:0, dropped:false,
    network:{connected:false, ethConnected:false, staIp:'0.0.0.0', ethIp:'0.0.0.0', apIp:'192.168.4.1', ssid:'', token:42, choice:1, trial:false, trialBooted:false,
      wifi:{mode:'dhcp',address:'0.0.0.0',mask:'0.0.0.0',gateway:'0.0.0.0',dns1:'0.0.0.0',dns2:'0.0.0.0'},eth:{mode:'dhcp'}},
    mqtt:{enabled:false,host:'',port:1883,tls:false,tlsSupported:true,username:'',hasPassword:false,discovery:true,discoveryPrefix:'homeassistant',storageOk:true,connected:false}, password:''};
  const enc = new TextEncoder();
  class Port {
    constructor() {this.boot='0123456789abcdef'; this.sid=123; this.cache=new Map();}
    async open() {
      if (this.readable) throw new DOMException('Already open','InvalidStateError');
      state.opens++;
      if (state.reboot) {state.reboot=false; this.boot='1123456789abcdef'; this.cache.clear(); state.network.trialBooted=true;}
      this.readable = new ReadableStream({start:(controller)=>{this.controller=controller;},cancel:()=>{}});
      this.writable = new WritableStream({write:(chunk)=>this.command(new TextDecoder().decode(chunk))});
    }
    async setSignals(signals) {state.signals=signals;}
    async close() {this.readable=null; this.writable=null;}
    getInfo() {return {usbVendorId:state.bridge?0x1a86:0x303a,usbProductId:0x1001};}
    send(reply) {const raw=enc.encode('boot log\r\n'+JSON.stringify(reply)+'\r\n'); this.controller.enqueue(raw.subarray(0,31));this.controller.enqueue(raw.subarray(31));}
    command(raw) {
      if (!raw.startsWith('@') || !raw.endsWith('\r\n')) throw Error('Invalid command framing');
      const f=Object.fromEntries(new URLSearchParams(raw.slice(1).trim())); state.calls.push(f);
      const cid=Number(f.cid),key=this.boot+':'+cid;
      if (f.path==='/api/hello') {this.sid++;this.cache.clear();this.send({v:1,cid,status:200,boot:this.boot,sid:this.sid,selection:0,inputMax:1024,replyMax:1200,pageMax:640});return;}
      if (this.cache.has(key)) {this.send(this.cache.get(key));return;}
      let body={},status=200,type='application/json';
      const config=()=>({rotorName:state.name,model:'G5500',build:{displayKind:0,board:'ESP32-S3 N16R8',features:{commandApi:true,mqtt:true,mqttTls:true,ethernet:false}},padding:'x'.repeat(800)});
      if (f.method==='GET') {
        if (f.path==='/api/device') body={ok:true,deviceRole:'controller',deviceId:state.id,volatile:'x'.repeat(1200)};
        else if (f.path==='/api/config') body=config();
        else if (f.path==='/api/status') body={moving:false,recovery:false,model:'G5500'};
        else if (f.path==='/api/mqtt') body=state.mqtt;
        else if (f.path==='/api/network') body=state.network;
        else {status=404;body={error:'unknown_fixture_route'};}
      } else if (f.method==='POST') {
        const args=Object.fromEntries(Object.entries(f).filter(([k])=>!['v','cid','boot','sid','method','path','page','selection'].includes(k)));
        state.writes.push({path:f.path,args});
        if (state.failMqtt && f.path==='/api/mqtt') {status=503;body={error:'mqtt_storage_failed'};}
        else if (f.path==='/api/settings') {state.name=args.rotorName;body={ok:true};}
        else if (f.path==='/api/mqtt') {
          for (const [k,v] of Object.entries(args)) {
            if (k==='password') {state.password=v;state.mqtt.hasPassword=!!v;}
            else state.mqtt[k]=['enabled','tls','discovery'].includes(k)?v==='true':k==='port'?Number(v):v;
          }
          state.mqtt.connected=state.network.connected;body={ok:true};
        } else if (f.path==='/api/wifi') {state.network.ssid=args.ssid;state.wifiPassword=args.pass;state.network.connected=true;state.network.staIp='192.168.1.99';type='text/plain';body='Настройки Wi-Fi сохранены.';}
        else if (f.path==='/api/network') {
          for (const [k,v] of Object.entries(args)) {
            if (k==='choice') state.network.choice=Number(v);
            else {const match=/^(wifi|eth)(Mode|Address|Mask|Gateway|Dns1|Dns2)$/.exec(k);if(match) state.network[match[1]][match[2][0].toLowerCase()+match[2].slice(1)]=v;}
          }
          state.network.trial=true;state.network.staIp=state.network.wifi.mode==='static'?state.network.wifi.address:'192.168.1.99';state.reboot=true;body={ok:true};
        } else if (f.path==='/api/network/confirm') {assertToken(args.token);state.network.trial=false;body={ok:true};}
        else {status=404;body={error:'unknown_fixture_route'};}
      }
      function assertToken(value) {if(value!=='42')throw Error('Wrong network token');}
      const all=enc.encode(type==='application/json'?JSON.stringify(body):body),offset=Number(f.page),part=all.subarray(offset,offset+240);
      const revision=all.reduce((a,n)=>(Math.imul(a,31)+n)>>>0,7);
      const reply={v:1,cid,status,boot:this.boot,sid:this.sid,selection:0,offset,next:offset+part.length<all.length?offset+part.length:null,total:all.length,revision,type,base64:btoa(String.fromCharCode(...part))};
      this.cache.set(key,reply);
      if (state.wrongBoot && f.method==='POST') {reply.boot='ffffffffffffffff';this.send(reply);return;}
      if (state.dropPost && !state.dropped && f.method==='POST') {state.dropped=true;return;}
      this.send(reply);
    }
  }
  const port=window.fixturePort=new Port();
  Object.defineProperty(navigator,'serial',{configurable:true,value:{requestPort:async()=>port,getPorts:async()=>[port]}});
}

let serviceBase, base, pending, online=false, pollFailure=false;
const requests=[];
const id='12345678-1234-4234-9234-123456789abc',rotator='22345678-1234-4234-9234-123456789abc';
const service=http.createServer(async(req,res)=>{
  let body='';for await(const chunk of req)body+=chunk;
  const data=body?JSON.parse(body):{}; requests.push({path:req.url,data,origin:req.headers.origin,cookie:req.headers.cookie});
  res.setHeader('Access-Control-Allow-Origin',new URL(base).origin);res.setHeader('Access-Control-Allow-Headers','Content-Type');res.setHeader('Access-Control-Allow-Methods','POST, OPTIONS');res.setHeader('Cache-Control','no-store');
  if(req.method==='OPTIONS'){res.writeHead(204);res.end();return;}
  if(req.url.startsWith('/provisioning/')){
    res.setHeader('Content-Type','text/html; charset=utf-8');res.end('<html lang="ru"><title>Тест привязки</title><button onclick="fetch(\'/consent\',{method:\'POST\'}).then(()=>document.body.textContent=\'Разрешение выдано\')">Разрешить привязку</button>');return;
  }
  let result={};
  if(req.url==='/consent'){pending='issued';result={ok:true};}
  else if(req.url.endsWith('/sessions')){pending='pending';result={id,approval_url:serviceBase+'/provisioning/'+id};}
  else if(req.url.endsWith('/ack')){pending='complete';result={status:'complete'};}
  else if(req.url.endsWith('/poll')){
    if(pollFailure){pollFailure=false;res.writeHead(503,{'Content-Type':'application/json'});res.end(JSON.stringify({detail:'Временная ошибка. Повторите.'}));return;}
    result={status:pending};
    if(pending!=='pending'){
      result={...result,device_id:'aabbccddeeff',approved:true,enabled:true,blocked:false,online,rotator_id:rotator,device_url:serviceBase+'/?rotator='+rotator};
      if(pending==='issued') result.mqtt={host:'mqtt.fixture.test',port:8883,tls:true,username:'d_fixture',password:'a'.repeat(64)};
    }
  }else{res.writeHead(404);res.end();return;}
  res.setHeader('Content-Type','application/json');res.end(JSON.stringify(result));
});
const server=http.createServer((req,res)=>{
  const relative=decodeURIComponent(new URL(req.url,'http://localhost').pathname).replace(/^\/controller\//,'')||'index.html';
  const file=path.resolve(directory,relative);
  if(!file.startsWith(directory+path.sep)||!fs.existsSync(file)||fs.statSync(file).isDirectory()){res.writeHead(404);res.end();return;}
  let data=fs.readFileSync(file);
  if(relative==='index.html') data=Buffer.from(data.toString().replace('content="https://rotator.r3v.ru"','content="'+serviceBase+'"'));
  res.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css','.html':'text/html','.json':'application/json','.svg':'image/svg+xml'})[path.extname(file)]||'application/octet-stream');res.end(data);
});

(async()=>{
 await new Promise(r=>server.listen(Number(process.env.PROVISIONING_FLASHER_PORT)||0,'127.0.0.1',r));base=`http://127.0.0.1:${server.address().port}/controller/`;
 if(process.env.PROVISIONING_SERVICE_URL)serviceBase=process.env.PROVISIONING_SERVICE_URL;
 else {await new Promise(r=>service.listen(0,'127.0.0.1',r));serviceBase=`http://127.0.0.1:${service.address().port}`;}
 const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{}),args:['--no-sandbox']});
 const errors=[];
 try{
  const context=await browser.newContext({viewport:{width:1280,height:1000}});
  await context.addInitScript(fakeSerial);
  if(process.env.PROVISIONING_SERVICE_URL&&!process.env.PROVISIONING_FRESH_LOGIN)await context.addCookies([{name:'bridge_session',value:'provisioning-browser-fixture',url:serviceBase},{name:'bridge_csrf',value:'provisioning-browser-csrf',url:serviceBase}]);
  if(process.env.PROVISIONING_FRESH_LOGIN)await context.route(serviceBase+'/auth/google/login*',async route=>{
    const response=await route.fetch({maxRedirects:0});assert.equal(response.status(),303);
    const url=new URL(response.headers().location);assert.equal(url.origin,'https://accounts.google.com');assert.equal(url.searchParams.get('code_challenge_method'),'S256');
    await route.fulfill({response,headers:{...response.headers(),location:url.searchParams.get('redirect_uri')+'?state='+url.searchParams.get('state')+'&code=browser-fixture'}});
  });
  const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
  if(process.env.PROVISIONING_SERVICE_URL)context.on('request',r=>{if(r.url().startsWith(serviceBase+'/api/provisioning/public/')&&r.method()==='POST')requests.push({path:new URL(r.url()).pathname,data:r.postDataJSON(),cookie:r.headers().cookie});});
  await page.goto(base);await page.locator('#workspace').waitFor({state:'visible'});
  assert.equal(await page.locator('#presetEnabled').count(),0);assert(await page.locator('#presetForm').isVisible());await page.locator('#presetName').fill('RA3VLD_Test');
  await page.locator('#presetSsid').fill('Сеть дома');await page.locator('#presetWifiPassword').fill('fixture-wifi-password');
  await page.locator('#presetMqttMode').selectOption('manual');await page.locator('#presetMqttHost').fill('192.168.1.10');
  await page.locator('#presetMqttUser').fill('station');await page.locator('#presetMqttPassword').fill('fixture-mqtt-password');
  await page.locator('#presetDiscovery').check();await page.locator('#presetDiscoveryPrefix').fill('homeassistant');
  for(const width of [1280,390]){await page.setViewportSize({width,height:1000});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.locator('.preset-panel').screenshot({path:path.join(output,`manual-${width}.png`)});}
  await page.setViewportSize({width:1280,height:1000});
  await page.evaluate(()=>window.usbFixture.dropPost=true);
  await page.locator('#presetApply').click();await page.waitForFunction(()=>document.getElementById('presetStatus').textContent.includes('Устройство подключено к сети'));
  const saved=await page.evaluate(()=>({name:usbFixture.name,ssid:usbFixture.network.ssid,wifi:usbFixture.wifiPassword,host:usbFixture.mqtt.host,discovery:usbFixture.mqtt.discovery,password:usbFixture.password,settingsWrites:usbFixture.writes.filter(w=>w.path==='/api/settings').length,cids:usbFixture.calls.filter(c=>c.path==='/api/settings').map(c=>c.cid)}));
  assert.equal(saved.name,'RA3VLD_Test');assert.equal(saved.ssid,'Сеть дома');assert.equal(saved.wifi,'fixture-wifi-password');assert.equal(saved.host,'192.168.1.10');assert.equal(saved.discovery,true);assert.equal(saved.password,'fixture-mqtt-password');assert.equal(saved.settingsWrites,1);assert.equal(saved.cids.length,2);assert.equal(saved.cids[0],saved.cids[1]);
  assert.deepEqual(await page.evaluate(()=>usbFixture.signals),{dataTerminalReady:true,requestToSend:false});
  await page.evaluate(async()=>{const {SerialApi}=await import('./serial-api.js');usbFixture.bridge=true;const api=new SerialApi(fixturePort);await api.open();await api.identity();await api.close();usbFixture.bridge=false;});
  assert.deepEqual(await page.evaluate(()=>usbFixture.signals),{dataTerminalReady:false,requestToSend:false});
  assert.equal(requests.length,0,'Manual MQTT must not contact the cloud service');
  assert.match(await page.locator('.preset-panel').innerText(),/можно выполнить позже вручную/);
  // Firmware error leaves drafts recoverable and does not proceed to Wi-Fi.
  await page.evaluate(()=>{usbFixture.failMqtt=true;usbFixture.writes=[];});
  await page.locator('#presetApply').click();await page.waitForFunction(()=>document.getElementById('presetStatus').textContent.includes('mqtt_storage_failed'));
  assert.equal(await page.locator('#presetMqttPassword').inputValue(),'fixture-mqtt-password');
  assert.equal(await page.evaluate(()=>usbFixture.writes.some(w=>w.path==='/api/wifi')),false);
  await page.evaluate(()=>{usbFixture.failMqtt=false;});
  // Static IP follows reboot -> same ID -> readback -> network confirmation.
  await page.locator('#presetIpMode').selectOption('static');await page.locator('#presetAddress').fill('192.168.1.50');await page.locator('#presetGateway').fill('192.168.1.1');await page.locator('#presetDns').fill('192.168.1.1');
  await page.locator('#presetApply').click();await page.waitForFunction(()=>usbFixture.network.wifi.address==='192.168.1.50'&&!usbFixture.network.trial,{},{timeout:20000});
  await page.waitForFunction(()=>!document.getElementById('presetApply').disabled);assert.match(await page.locator('#presetLog').innerText(),/успешно запущена и подтверждена/);
  // A changed board/session never silently receives a retry in a new session.
  await page.evaluate(()=>{usbFixture.id='112233445566';usbFixture.writes=[];});
  await page.locator('#presetApply').click();await page.waitForFunction(()=>document.getElementById('presetStatus').textContent.includes('другая плата'));
  assert.equal(await page.evaluate(()=>usbFixture.writes.length),0);
  await page.locator('#presetNewDevice').click();await page.locator('#presetApply').click();
  await page.waitForFunction(()=>!document.getElementById('presetApply').disabled&&document.getElementById('presetLog').textContent.includes('112233445566'));
  await page.locator('#presetNewDevice').click();await page.evaluate(()=>usbFixture.id='aabbccddeeff');
  // Independent broker selection, followed by explicit account consent.
  await page.locator('#presetIpMode').selectOption('preserve');await page.locator('#presetMqttMode').selectOption('service');
  await page.route(serviceBase+'/api/provisioning/public/sessions',route=>route.abort('blockedbyclient'),{times:1});
  await page.locator('#presetApply').click();
  await page.waitForFunction(()=>document.getElementById('presetStatus').textContent.startsWith('Нет связи с rotator.r3v.ru.'));
  assert.equal(await page.locator('#presetServiceTroubleshooting').evaluate(node=>node.open),true);
  assert.match(await page.locator('#presetServiceTroubleshooting').innerText(),/ERR_BLOCKED_BY_CLIENT/);
  await page.waitForFunction(()=>!document.getElementById('presetApply').disabled);
assert(await page.locator('#presetServiceTroubleshooting').isVisible());assert.match(await page.locator('#presetServiceTroubleshooting').innerText(),/Нет связи/);
  await page.locator('#presetApply').click();await page.locator('#presetConsent').waitFor({state:'visible'});
  const popupPromise=context.waitForEvent('page');await page.locator('#presetConsent').click();const popup=await popupPromise;
  if(process.env.PROVISIONING_SERVICE_URL){
    if(process.env.PROVISIONING_FRESH_LOGIN)await popup.getByRole('link',{name:'Войти через Google',exact:true}).click();
    const consent=popup.getByRole('button',{name:'Разрешить привязку',exact:true});
    try{await consent.waitFor();}catch(error){const url=new URL(popup.url());console.error('Consent page:',url.origin+url.pathname,await popup.locator('body').innerText());await popup.screenshot({path:path.join(output,'consent-error.png'),fullPage:true});throw error;}
    for(const width of [1280,390]){await popup.setViewportSize({width,height:1000});assert.equal(await popup.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await popup.screenshot({path:path.join(output,`consent-${width}.png`),fullPage:true});}
    const rotate=popup.getByLabel('Заменить MQTT-пароль и записать новый в плату');if(await rotate.count())await rotate.check();
  }
  await popup.getByRole('button',{name:'Разрешить привязку',exact:true}).click();
  await page.waitForFunction(()=>!document.getElementById('presetDeviceLink').hidden,{},{timeout:20000});
  await page.waitForFunction(()=>!document.getElementById('presetApply').disabled);
  if(process.env.PROVISIONING_SERVICE_URL){const snapshot=await (await popup.request.get(serviceBase+'/api/snapshot')).json();assert.equal(snapshot.connections.length,1);assert.equal(snapshot.rotators.length,1);}
  assert.equal(await page.evaluate(()=>usbFixture.mqtt.tls),true);assert.equal(await page.evaluate(()=>usbFixture.mqtt.discovery),false);
  assert.match(await page.evaluate(()=>usbFixture.password),/^[a-f0-9]{64}$/);
  assert.equal(await page.evaluate(()=>localStorage.length+sessionStorage.length),0);
  for(const req of requests.filter(r=>r.path.startsWith('/api/'))){assert.equal(req.cookie,undefined);assert.equal(req.data.ssid,undefined);assert.equal(req.data.pass,undefined);assert.equal(req.data.password,undefined);}
  if(!process.env.PROVISIONING_SERVICE_URL)online=true;
  else await fetch(serviceBase+'/fixture/telemetry',{method:'POST'});
  await page.locator('#presetRefresh').click();await page.waitForFunction(()=>document.getElementById('presetLog').textContent.includes('свежую телеметрию'),{},{timeout:10000});
  for(const width of [1280,390]){await page.setViewportSize({width,height:1000});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.locator('.preset-panel').screenshot({path:path.join(output,`service-${width}.png`)});}
  // Unsupported profiles visibly keep their manual setup path.
  await page.locator('#boardSelect').selectOption('esp32');await page.locator('input[value="esp32_spp"]').check();assert(await page.locator('#presetForm').isVisible());assert.equal(await page.locator('#presetFields').evaluate(node=>node.disabled),true);assert(await page.locator('#presetName').isDisabled());assert.match(await page.locator('#presetUnavailable').innerText(),/основной прошивке/);
  // Run the actual patched vendor callback with its loader already disconnected.
  // Settings stay frozen, and the same granted port is handed off automatically.
  await page.locator('#boardSelect').selectOption('s3');await page.locator('#presetMqttMode').selectOption('preserve');await page.locator('#presetName').fill('Auto_Handoff');await page.locator('#confirmHardware').check();
  await page.locator('.install-button').click();await page.locator('ewt-install-dialog').waitFor({state:'attached'});
  await page.waitForFunction(()=>!!document.querySelector('ewt-install-dialog')._manifest);
  const dialogFile=fs.readdirSync(path.join(directory,'vendor')).find(n=>/^install-dialog-.*\.js$/.test(n));
  const script=fs.readFileSync(path.join(directory,'vendor',dialogFile),'utf8');
  const begin=script.indexOf('(e=>{this._installState=e;if')+1,end=script.indexOf('}),this.port,this.manifestPath',begin)+1;
  assert.ok(begin>0&&end>begin);
  await page.evaluate(async(source)=>{
    document.getElementById('presetName').value='Changed_After_Click';
    await fixturePort.close();
    const callback=Function('return ('+source+')').call(document.querySelector('ewt-install-dialog'));callback({state:'finished'});
  },script.slice(begin,end));
  assert.equal(await page.locator('#boardSelect').isDisabled(),true);
  await page.waitForFunction(()=>usbFixture.name==='Auto_Handoff'&&!document.getElementById('presetApply').disabled,{},{timeout:20000});
  assert.equal(await page.locator('ewt-install-dialog').count(),0);
  // Leaving the always-visible form unchanged keeps ordinary flashing free of
  // our automatic settings handoff and does not write any saved configuration.
  const writesBefore=await page.evaluate(()=>usbFixture.writes.length);
  await page.evaluate(()=>{document.getElementById('presetForm').reset();document.getElementById('presetMqttMode').dispatchEvent(new Event('change'));});
  await page.locator('#confirmHardware').check();await page.locator('.install-button').click();
  await page.locator('ewt-install-dialog').waitFor({state:'attached'});
  const ordinary=await page.evaluate(()=>document.querySelector('ewt-install-dialog').dispatchEvent(new CustomEvent('g5500-flash-state',{detail:{state:'finished'},bubbles:true,composed:true,cancelable:true})));
  assert.equal(ordinary,true,'An unchanged form must not take over the flasher USB port');
  await page.waitForTimeout(650);
  assert.equal(await page.evaluate(()=>usbFixture.writes.length),writesBefore);
  assert.deepEqual(errors,[]);
  console.log('PASS: manual MQTT/Discovery independent of service, UTF-8 Wi-Fi, identical POST retry, saved-state verification, storage failure/retry, static-IP reboot/confirm, wrong-device guard, account consent, MQTT TLS delivery, no secret storage, actual vendor callback/automatic USB handoff/frozen draft, desktop/mobile');
 }finally{await browser.close();await new Promise(r=>server.close(r));if(service.listening)await new Promise(r=>service.close(r));}
})().catch(e=>{console.error(e);process.exitCode=1;});
