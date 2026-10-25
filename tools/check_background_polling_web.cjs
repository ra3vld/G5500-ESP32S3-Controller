// Browser traffic contract: closed editors must not keep allocating HTTP sessions.
const {chromium}=require(process.env.PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'G5500_Fork/web_page.hpp'),'utf8').split('R"HTML(')[1].split(')HTML";')[0];
const sample=JSON.parse(execFileSync(path.join(root,'build/simulator'),{input:'step 0\n',encoding:'utf8'}).trim()).state;
const status={...sample,livePort:81,liveControlChannel:false,deviceRole:'controller',setupRequired:false,configurationLocked:false,moving:false};
const setup={required:false,paused:false,step:0,role:'controller',ready:true,storageOk:true,equipment:true,token:1,generation:1};
const network={choice:0,connected:true,staIp:'192.168.1.42',apIp:'0.0.0.0',wifi:{mode:'dhcp'},eth:{mode:'dhcp'}};
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox','--disable-background-timer-throttling']});
 try{
  const page=await browser.newPage(),errors=[],counts={};let slow=false,pending=0,maxPending=0;
  page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(state=>{
   window.WebSocket=class {
    static OPEN=1;readyState=1;bufferedAmount=0;revision=0;
    constructor(){this.timer=setInterval(()=>this.onmessage?.({data:JSON.stringify({v:1,type:'state',boot:'abc',revision:++this.revision,state})}),200);}
    close(){this.readyState=3;clearInterval(this.timer);}send(){}
   };
  },status);
  await page.route('http://background.test/**',async route=>{
   const url=new URL(route.request().url()),name=url.pathname;counts[name]=(counts[name]||0)+1;
   if(name==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
   if(name==='/api/status')return route.fulfill({json:status});
   if(name==='/api/setup')return route.fulfill({json:setup});
   if(name==='/api/network'){
    ++pending;maxPending=Math.max(maxPending,pending);
    try{if(slow)await new Promise(resolve=>setTimeout(resolve,3200));await route.fulfill({json:network});}
    finally{--pending;}
    return;
   }
   return route.fulfill({json:{items:[],values:[],points:[],peers:[]}});
  });
  await page.goto('http://background.test/');
  await page.waitForFunction(()=>live.ready&&wizard.loaded&&wizard.network);
  assert.equal(await page.evaluate(()=>wizard.state?.required),false,'initial wizard state must be retained before periodic polling stops');
  const initial={...counts};await page.waitForTimeout(16000);
  const background=Object.fromEntries(['/api/setup','/api/network','/api/status'].map(p=>[p,(counts[p]||0)-(initial[p]||0)]));
  console.log('16 s on Control with healthy telemetry:',JSON.stringify(background));
  if(process.env.POLLING_BASELINE==='1')return;
  assert.equal(background['/api/setup'],0,'an inactive wizard must not poll HTTP');
  assert.equal(background['/api/network'],0,'a closed network editor must not poll HTTP');
  assert(background['/api/status']<=1,'retain only the existing occasional full status refresh');

  await page.getByRole('tab',{name:'Настройки',exact:true}).click();
  await page.locator('#networkSettings > details').evaluate(e=>e.open=true);
  const opened=counts['/api/network'];
  await page.waitForFunction(()=>!!wizard.network);await page.waitForTimeout(2700);
  assert(counts['/api/network']>opened,'the visible network editor keeps refreshing');
  await page.getByRole('tab',{name:'Управление',exact:true}).click();
  const closed=counts['/api/network'];await page.waitForTimeout(2700);
  assert.equal(counts['/api/network'],closed,'an open details element in a hidden tab must stop polling');

  slow=true;const beforeSlow=counts['/api/network'];
  await page.evaluate(()=>Promise.all([loadNetworkConfig(),loadNetworkConfig(),loadNetworkConfig()]));
  assert.equal(counts['/api/network']-beforeSlow,1,'slow simultaneous refreshes must share one request');
  assert.equal(maxPending,1,'HTTP network requests must not overlap');slow=false;

  const beforeWizard=counts['/api/setup'];
  await page.evaluate(s=>{wizard.active=true;wizard.state=s;renderSetup();},setup);
  await page.waitForTimeout(2700);
  assert(counts['/api/setup']>beforeWizard,'an active wizard keeps its periodic state refresh');
  await page.evaluate(()=>Object.defineProperty(document,'hidden',{configurable:true,value:true}));
  const hidden={...counts};await page.waitForTimeout(2700);
  assert.equal(counts['/api/setup'],hidden['/api/setup'],'a hidden page must not poll the wizard');
  assert.equal(counts['/api/network'],hidden['/api/network'],'a hidden page must not poll network configuration');
  assert.deepEqual(errors,[]);
  console.log('PASS: inactive wizard/closed network traffic, visible editor, slow request coalescing, active wizard and hidden page');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exit(1);});
