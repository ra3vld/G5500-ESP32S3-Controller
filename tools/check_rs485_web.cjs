// Addressed discovery, catalogue selection and mobile/desktop layout.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {spawn}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
async function sampleState(){return new Promise((resolve,reject)=>{
 const child=spawn(process.env.SIMULATOR_PATH||path.join(root,'build/simulator'),[],{stdio:['pipe','pipe','ignore']});
 let output='';const timer=setTimeout(()=>{child.kill();reject(new Error('simulator timeout'));},5000);
 child.once('error',reject);child.stdout.on('data',data=>{output+=data;if(output.includes('\n')){clearTimeout(timer);child.kill();resolve(JSON.parse(output.split('\n')[0]).state);}});child.stdin.write('step 0\n');
});}
(async()=>{
 const sample=await sampleState(),posts=[],errors=[];
 const peers=[{id:'b',name:'Поворотка 1',transport:'uart',busAddress:3,online:true,ageMs:100,az:10,el:20},
  {id:'c',name:'Поворотка 2',transport:'uart',busAddress:7,online:true,ageMs:100,az:30,el:40}];
 const saved=new Set();let searching=false,selected='';
 const browser=await chromium.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:390,height:900}});
  page.on('pageerror',error=>errors.push(error.message));
  await page.addInitScript(()=>{window.G5500_HTTP_ONLY=true});
  await page.route('http://rs485.test/**',route=>{
   const req=route.request(),url=new URL(req.url());
   if(req.method()==='POST'){
    const form=Object.fromEntries(new URLSearchParams(req.postData()));posts.push({path:url.pathname,form});
    if(url.pathname==='/api/hud/discover')searching=true;
    if(url.pathname==='/api/hud/peers')saved.add(form.id);
    if(url.pathname==='/api/hud/select')selected=form.id;
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   if(url.pathname==='/api/status')return route.fulfill({json:{...sample,deviceRole:'hud',hudUart:true,hudPort:4534,configurationLocked:false}});
   if(url.pathname==='/api/hud/peers'){
    const discovered=url.searchParams.get('discovered')==='1';
    const items=peers.filter(p=>discovered?searching&&!saved.has(p.id):saved.has(p.id)).map(p=>({...p,selected:p.id===selected}));
    return route.fulfill({json:{items,total:items.length,scanActive:searching,scanAddress:5,scanLast:7}});
   }
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{items:[],values:[],points:[],peers:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://rs485.test/#settings');await page.waitForFunction(()=>window._forkState?.deviceRole==='hud');
  await page.locator('#peersSettings > details > summary').click();
  await page.locator('#hudBusFirst').fill('3');await page.locator('#hudBusLast').fill('7');
  await page.locator('#hudBusSearch').click();
  await page.waitForFunction(()=>document.getElementById('hudBusProgress').textContent.includes('адрес 5 / 7'));
  assert.deepEqual(posts[0],{path:'/api/hud/discover',form:{transport:'rs485',first:'3',last:'7'}});
  assert.match(await page.locator('#hudDiscovered').textContent(),/RS‑485 №3/);
  await page.locator('#hudDiscovered button').first().click();
  await page.waitForFunction(()=>document.querySelectorAll('#hudPeerSettings .device-peer').length===1);
  await page.locator('#hudDiscovered button').first().click();
  await page.waitForFunction(()=>document.querySelectorAll('#hudPeerSettings .device-peer').length===2);
  await page.locator('#hudPeerSettings .device-peer').nth(1).locator('button').first().click();
  await page.waitForFunction(()=>document.querySelectorAll('#hudPeerSettings button[aria-pressed=true]').length===1);
  assert.equal(selected,'c');
  await page.getByRole('button',{name:'Закрыть уведомление'}).click();
  fs.mkdirSync(path.join(root,'artifacts/screenshots'),{recursive:true});
  for(const width of [320,1100]){
   await page.setViewportSize({width,height:1000});
   const height=Math.ceil((await page.locator('#peersSettings').boundingBox()).height)+100;
   await page.setViewportSize({width,height});
   await page.locator('#peersSettings').scrollIntoViewIfNeeded();
   await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,`overflow ${width}`);
   await page.locator('#peersSettings').screenshot({path:path.join(root,`artifacts/screenshots/rs485-${width}.png`)});
  }
  assert.deepEqual(errors,[]);
  console.log('PASS: RS485 range/progress, add two controllers, select second, 320/1100px layout');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
