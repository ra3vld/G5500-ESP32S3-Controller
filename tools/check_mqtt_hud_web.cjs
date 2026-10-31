// MQTT HUD catalogue controls and compact/wide layouts against embedded HTML.
const {chromium}=require('playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/mqtt-hud-web');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const state=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
state.deviceRole='hud';state.build.features={...state.build.features,mqtt:true,webLive:false};state.livePort=0;
const peer={id:'c0e5525c3314',name:'MQTT test',background:0,transport:'wifi',selected:true,online:true,ageMs:0,az:20,el:30};
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage(),posts=[],errors=[];let slowMode=false,ackAt=0;
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('**/*',route=>{
   const request=route.request(),url=new URL(request.url());
   if(request.method()==='POST'){
    const form=Object.fromEntries(new URLSearchParams(request.postData()));posts.push({path:url.pathname,form});
    if(slowMode&&url.pathname==='/api/mode'){ackAt=Date.now()+3500;return route.fulfill({status:202,json:{ok:false,result:'deferred',requestId:77}});}
    if(url.pathname==='/api/hud/peers'&&form.transport)peer.transport=form.transport;
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(slowMode&&url.pathname==='/api/hud/command')return route.fulfill({json:{requestId:77,ok:Date.now()>=ackAt,result:Date.now()>=ackAt?'ok':'deferred'}});
   if(url.pathname==='/api/hud/peers')return route.fulfill({json:{total:1,offset:0,items:[peer]}});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,items:[],values:[],samples:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://mqtt-hud.test/#settings');await page.waitForFunction(()=>window._forkState);
  await page.locator('#peersSettings > details').evaluate(e=>e.open=true);
  assert.match(await page.locator('#hudTransportHelp').textContent(),/Основное подключение HUD — UDP/);
  await page.getByRole('button',{name:'Найти по UDP',exact:true}).click();
  assert.equal(posts.at(-1).path,'/api/hud/discover');assert.equal(posts.at(-1).form.transport,'wifi');
  await page.getByRole('button',{name:'Перейти на MQTT',exact:true}).click();
  assert.deepEqual(posts.at(-1),{path:'/api/hud/peers',form:{id:peer.id,transport:'mqtt'}});
  await page.getByRole('button',{name:'Перейти на UDP',exact:true}).waitFor();
  assert.match(await page.locator('#hudPeerSettings').textContent(),/MQTT test · MQTT/);
  await page.getByRole('button',{name:'Найти по MQTT',exact:true}).click();
  assert.deepEqual(posts.at(-1),{path:'/api/hud/discover',form:{transport:'mqtt'}});
  await page.locator('#hudMqttId').fill('001122334455');
  await page.getByRole('button',{name:'Добавить по MQTT',exact:true}).click();
  await page.waitForFunction(()=>document.getElementById('hudMqttId').value==='001122334455');
  assert.deepEqual(posts.at(-1),{path:'/api/hud/peers',form:{id:'001122334455',transport:'mqtt'}});
  fs.mkdirSync(out,{recursive:true});
  for(const width of [390,1100]){
   await page.setViewportSize({width,height:1000});
   await page.locator('#hudMqttId').scrollIntoViewIfNeeded();
   await page.getByRole('button',{name:'Закрыть уведомление'}).click().catch(()=>{});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'overflow '+width);
   await page.locator('form.device-address[data-feature=mqtt]').screenshot({path:path.join(out,`mqtt-hud-${width}.png`)});
  }
  state.hudTransport='mqtt';await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>window._forkState?.hudTransport==='mqtt');slowMode=true;
  const delayed=await page.evaluate(async()=>({ok:await forkPost('/api/mode',{mode:'auto'}),transport:window._forkState?.hudTransport,message:document.getElementById('forkMessage').textContent}));
  assert.equal(delayed.ok,true,'3.5s TCP ACK must remain pending: '+JSON.stringify(delayed));
  state.build.features.mqtt=false;await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>document.getElementById('hudMqttSearch').hidden);
  assert.deepEqual(errors,[]);
  console.log('PASS: MQTT discovery/add/switch, persisted request fields, 3.5s MQTT ACK, disabled feature, 390/1100px layouts and no browser errors');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
