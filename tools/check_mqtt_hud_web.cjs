// MQTT HUD catalogue controls and compact/wide layouts against embedded HTML.
const {chromium}=require('playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),out=process.env.ARTIFACT_DIR||path.join(root,'local-notes/mqtt-hud-web');
const html=fs.readFileSync(process.env.WEB_PAGE_SOURCE||path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const state=JSON.parse(execFileSync(process.env.SIM_CORE||path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
state.deviceRole='hud';state.build.features={...state.build.features,mqtt:true,webLive:false};state.livePort=0;
const peer={id:'c0e5525c3314',name:'MQTT test',background:0,transport:'wifi',selected:true,online:true,ageMs:0,az:20,el:30};
const catalogue={saved:[peer],discovered:[]};
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
   if(url.pathname==='/api/hud/peers'){
    const items=url.searchParams.get('discovered')==='1'?catalogue.discovered:catalogue.saved;
    const offset=Number(url.searchParams.get('offset')||0);
    return route.fulfill({json:{total:items.length,offset,items:items.slice(offset,offset+8)}});
   }
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{ok:true,items:[],values:[],samples:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://mqtt-hud.test/#settings');await page.waitForFunction(()=>window._forkState);
  await page.locator('#peersSettings > details').evaluate(e=>e.open=true);
  await page.evaluate(()=>loadHudDiscovery());
  assert.equal(await page.locator('#hudDiscovery .device-pagination button:not([hidden])').count(),0,'empty discovery has no page buttons');
  assert.equal(await page.locator('#hudSettingsPrev').evaluate(e=>e.hidden),true,'one saved controller has no page buttons');
  assert(await page.getByRole('button',{name:'Найти по UDP',exact:true}).isVisible());
  assert(await page.locator('#hudAddress').isHidden());
  assert(await page.locator('#hudBusFirst').isHidden());
  assert(await page.locator('#hudMqttId').isHidden());
  assert.match(await page.locator('#hudTransportHelp').textContent(),/Основное подключение HUD — UDP/);
  await page.getByRole('button',{name:'Найти по UDP',exact:true}).click();
  assert.equal(posts.at(-1).path,'/api/hud/discover');assert.equal(posts.at(-1).form.transport,'wifi');
  await page.getByRole('button',{name:'Перейти на MQTT',exact:true}).click();
  assert.deepEqual(posts.at(-1),{path:'/api/hud/peers',form:{id:peer.id,transport:'mqtt'}});
  await page.getByRole('button',{name:'Перейти на UDP',exact:true}).waitFor();
  assert.match(await page.locator('#hudPeerSettings').textContent(),/MQTT test · MQTT/);
  await page.locator('#hudMqttDiscovery > summary').click();
  assert(await page.getByRole('button',{name:'Найти по UDP',exact:true}).isHidden());
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
   const notice=page.getByRole('button',{name:'Закрыть уведомление'});if(await notice.isVisible())await notice.click();
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'overflow '+width);
   await page.locator('form.device-address[data-feature=mqtt]').screenshot({path:path.join(out,`mqtt-hud-${width}.png`)});
   const row=page.locator('#hudPeerSettings .device-peer');
   const rename=await row.getByRole('button',{name:'Имя',exact:true}).boundingBox();
   const remove=await row.getByRole('button',{name:'Удалить',exact:true}).boundingBox();
   assert(Math.abs(rename.y-remove.y)<1,'name and delete must share a row at '+width);
   const transport=await row.locator('.peer-transport').boundingBox();
   assert(Math.abs(transport.y-rename.y)<1,'all actions must share a row at '+width);
   if(width>600){
    const title=await row.locator(':scope > button').boundingBox();
    assert(Math.abs(title.y-transport.y)<1,'controller and actions must share a row');
    assert(Math.abs(transport.x-title.x-title.width-6)<1,'six-pixel gap beside controller');
   }
   const colour=await row.getByRole('combobox').boundingBox();
   const swatch=await row.locator('.hud-background-swatch').boundingBox();
   assert(Math.abs(colour.y+colour.height/2-swatch.y-swatch.height/2)<1,'colour control must stay beside swatch at '+width);
   await row.screenshot({path:path.join(out,`hud-controller-${width}.png`)});
   await page.locator('#hudUdpDiscovery > summary').click();
   await page.locator('#hudDiscovery').screenshot({path:path.join(out,`hud-discovery-${width}.png`)});
   await page.locator('#hudMqttDiscovery > summary').click();
  }
  const makePeers=count=>Array.from({length:count},(_,i)=>({...peer,id:(i+1).toString(16).padStart(12,'0'),name:'Поворотка '+(i+1),selected:i===0}));
  const reloadCatalogues=async count=>{
   catalogue.saved=makePeers(count);catalogue.discovered=makePeers(count);
   await page.evaluate(async()=>{await loadHudPeers();await loadHudDiscovery()});
   await page.waitForFunction(count=>hudTotal===count&&hudDiscoveryTotal===count,count);
  };
  for(const count of [0,1,8]){
   await reloadCatalogues(count);
   assert.equal(await page.locator('#hudControls .device-pagination button:not([hidden])').count(),0,'no paging at '+count);
   assert.equal(await page.locator('#hudPrev').evaluate(e=>e.hidden),true);
  }
  await reloadCatalogues(17);
  for(const prefix of ['hudSettings','hudDiscovery']){
   assert(await page.locator('#'+prefix+'Prev').isDisabled());
   await page.locator('#'+prefix+'Next').click();
   await page.waitForFunction(prefix=>document.getElementById(prefix+(prefix==='hudDiscovery'?'Info':'PageInfo')).textContent.includes('9–16'),prefix);
   assert.equal(await page.locator('#'+prefix+'Prev').isDisabled(),false);
   await page.locator('#'+prefix+'Next').click();
   await page.waitForFunction(prefix=>document.getElementById(prefix+(prefix==='hudDiscovery'?'Info':'PageInfo')).textContent.includes('17–17'),prefix);
   assert(await page.locator('#'+prefix+'Next').isDisabled());
  }
  // Deleting the last page must return to the remaining page before hiding navigation.
  await reloadCatalogues(8);
  assert.equal(await page.locator('#hudPeerSettings .device-peer').count(),8);
  assert.equal(await page.locator('#hudDiscovered .device-peer').count(),8);
  assert.equal(await page.evaluate(()=>hudOffset===0&&hudDiscoveredOffset===0),true);
  assert.equal(await page.locator('#hudControls .device-pagination button:not([hidden])').count(),0);
  catalogue.saved=[peer];catalogue.discovered=[];
  await page.evaluate(async()=>{await loadHudPeers();await loadHudDiscovery()});
  for(const width of [390,1100]){
   await page.setViewportSize({width,height:1000});
   await page.locator('#hudDiscovery .device-pagination').screenshot({path:path.join(out,`empty-discovery-${width}.png`)});
  }
  state.hudTransport='mqtt';await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>window._forkState?.hudTransport==='mqtt');slowMode=true;
  const delayed=await page.evaluate(async()=>({ok:await forkPost('/api/mode',{mode:'auto'}),transport:window._forkState?.hudTransport,message:document.getElementById('forkMessage').textContent}));
  assert.equal(delayed.ok,true,'3.5s TCP ACK must remain pending: '+JSON.stringify(delayed));
  state.build.features.mqtt=false;await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>document.getElementById('hudMqttSearch').hidden);
  assert(await page.locator('#hudMqttDiscovery').isHidden());
  state.build.features.hudUdp=false;state.build.features.rs485=false;
  await page.evaluate(()=>upd(true));
  await page.waitForFunction(()=>document.getElementById('hudUdpDiscovery').hidden);
  await page.locator('#hudWiredDiscovery > summary').click();
  assert(await page.locator('#hudBusFirst').isHidden());
  assert(await page.locator('#hudUartSearch').isVisible());
  assert.deepEqual(errors,[]);
  console.log('PASS: inline controller actions, 0/1/8/17-item pagination and last-page removal, discovery/MQTT/ACK/features, 390/1100px; no browser errors');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
