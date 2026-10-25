// UART bridge form: persistence fields, disabled controls and narrow layouts.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {spawn}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
async function sampleState(){
 return new Promise((resolve,reject)=>{
  const child=spawn(process.env.SIMULATOR_PATH||path.join(root,'build/simulator'),[],{stdio:['pipe','pipe','ignore']});
  let output='';const timer=setTimeout(()=>{child.kill();reject(new Error('simulator did not produce a state'));},5000);
  child.once('error',error=>{clearTimeout(timer);reject(error);});
  child.stdout.on('data',data=>{
   output+=data;
   if(output.includes('\n')){clearTimeout(timer);child.kill();resolve(JSON.parse(output.split('\n')[0]).state);}
  });
  child.stdin.write('step 0\n');
 });
}
(async()=>{
 const sample=await sampleState();
 const browser=await chromium.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[],posts=[];
  page.on('pageerror',e=>errors.push(e.message));
  const state={...sample,livePort:81,liveControlChannel:false,deviceRole:'controller',hudUart:true,hudBaud:9600,hudIntervalMs:2000,
   hudReadyHigh:0,configurationLocked:false};
  await page.route('http://hud-link.test/**',route=>{
   const req=route.request(),url=new URL(req.url());
   if(req.method()==='POST'){
    const form=Object.fromEntries(new URLSearchParams(req.postData()));posts.push({path:url.pathname,form});
    return route.fulfill({json:{ok:true,result:'ok'}});
   }
   if(url.pathname==='/api/status')return route.fulfill({json:state});
   if(url.pathname.startsWith('/api/'))return route.fulfill({json:{items:[],values:[],points:[],peers:[]}});
   return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  });
  await page.goto('http://hud-link.test/#settings');await page.waitForFunction(()=>window._forkState);
  await page.locator('#hudPanel summary').click();
  assert.equal(await page.locator('#hudBaud').count(),0);
  assert.equal(await page.locator('#hudUartSettings a').getAttribute('href'),'/hardware#uart-settings');
  assert.equal(await page.locator('#hudRefreshHz').inputValue(),'0.5');
  assert.equal(await page.locator('#hudReadyHigh').inputValue(),'0');
  await page.locator('#hudRefreshHz').fill('4');
  await page.evaluate(()=>upd(true));
  assert.equal(await page.locator('#hudRefreshHz').inputValue(),'4'); // Polling preserves unsaved values.
  await page.locator('#hudConfig button[type=submit]').click();
  await page.waitForFunction(()=>!document.getElementById('hudConfig').dataset.dirty);
  assert.deepEqual(posts.at(-1),{path:'/api/device',form:{role:'controller',wifi:'existing',uart:'1',port:'4534',
   intervalMs:'250',readyHigh:'0'}});
  await page.getByRole('button',{name:'Закрыть уведомление'}).click();
  for(const width of [320,390,1100]){
   await page.setViewportSize({width,height:900});
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,`overflow ${width}`);
  }
  fs.mkdirSync(path.join(root,'artifacts/screenshots'),{recursive:true});
  await page.setViewportSize({width:390,height:1500});
  await page.locator('#hudUartSettings').scrollIntoViewIfNeeded();
  await page.locator('#hudUartSettings').screenshot({path:path.join(root,'artifacts/screenshots/hud-link-390.png')});
  await page.locator('#deviceUart').selectOption('0');
  assert(await page.locator('#hudUartSettings').isHidden());assert(await page.locator('#hudRefreshHz').isDisabled());
  await page.locator('#hudConfig button[type=submit]').click();
  await page.waitForFunction(()=>!document.getElementById('hudConfig').dataset.dirty);
  assert.deepEqual(posts.at(-1),{path:'/api/device',form:{role:'controller',wifi:'existing',uart:'0',port:'4534'}});
  assert.deepEqual(errors,[]);
  console.log('PASS: UART hardware link/frequency/AUX polarity, dirty form, disabled UART and 320/390/1100px layouts');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
