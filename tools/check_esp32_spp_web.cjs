const output=require('node:path').resolve(__dirname,'../build/esp32-spp-web');require('node:fs').mkdirSync(output,{recursive:true});
const {chromium}=require('playwright'),fs=require('node:fs'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../G5500_Fork/web_page.hpp'),'utf8');
const pages={};for(const m of source.matchAll(/static constexpr char (\w+)\[\] PROGMEM = R"HTML\((.*?)\)HTML";/gs))pages[m[1]]=m[2];
const fixture=()=>({model:'G5500',rotorName:'G5500',externalAzMode:'shortest',gs232ManualFollow:false,rotctldManualFollow:false,n1mmManualFollow:false,pstManualFollow:false,n1mmPort:12040,pstPort:1234,reportPort:13010,reportHost:'',controlHz:100,calMode:'2',axes:[{speedPercent:30,outputLimit:69,coastDegrees:1.7,feedbackTimeoutMs:500,stallMs:3000,zero:100,full:2800},{speedPercent:20,outputLimit:50,coastDegrees:2.5,feedbackTimeoutMs:500,stallMs:3000,zero:2800,full:100}]});
const table=()=>({profile:'custom',start:-50,end:190,span:240,customStart:-50,customSpan:240,storageFault:false,points:[{angle:-50,mv:100,endpoint:true},{angle:30,mv:1000,endpoint:false},{angle:190,mv:2800,endpoint:true}]});
(async()=>{const browser=await chromium.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});try{for(const width of [1100,390]){
 const page=await browser.newPage({viewport:{width,height:900}}),errors=[],posts=[];let config=fixture(),tables={az:table(),el:table()},failure=null,fault='az_feedback_stale';
 page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
 await page.route('http://essential.test/**',async r=>{const req=r.request(),u=new URL(req.url()),route=u.pathname,d=Object.fromEntries(new URLSearchParams(req.postData()||''));
  const reply=(v,code=200)=>r.fulfill({status:code,contentType:'application/json',body:JSON.stringify(v)});
  if(route==='/settings'||route==='/calibration')return r.fulfill({contentType:'text/html',body:pages[route==='/settings'?'ESSENTIAL_SETTINGS_PAGE':'ESSENTIAL_CALIBRATION_PAGE']});
  if(req.method()==='POST'){
   posts.push({route,d});if(failure&&failure.route===route)return reply({error:failure.error},failure.code);
   if(route==='/api/settings'){if(d.rotorName)Object.assign(config,{rotorName:d.rotorName,model:d.model,externalAzMode:d.externalAzMode,gs232ManualFollow:d.gs232ManualFollow==='1'});for(const key of ['n1mmPort','pstPort','reportPort'])if(key in d)config[key]=+d[key];if('reportHost' in d)config.reportHost=d.reportHost;for(const key of ['n1mmManualFollow','pstManualFollow','rotctldManualFollow'])if(key in d)config[key]=d[key]==='1';if(d.controlHz)config.controlHz=+d.controlHz;for(const [i,a] of ['az','el'].entries()){for(const [suffix,key] of [['SpeedPercent','speedPercent'],['CoastDegrees','coastDegrees'],['MotionOutputLimitPercent','outputLimit'],['MotionFeedbackTimeoutMs','feedbackTimeoutMs'],['MotionStallMs','stallMs']])if(d[a+suffix])config.axes[i][key]=+d[a+suffix]}}
   if(route==='/api/motion/reset')fault='';
   if(route==='/api/calmode')config.calMode=d.m;
   if(route==='/api/azrange'||route==='/api/elrange'){const a=route.includes('az')?'az':'el';Object.assign(tables[a],{profile:d.profile,start:d.profile==='custom'?+d.start:0,span:d.profile==='custom'?+d.span:a==='az'?360:180});tables[a].end=tables[a].start+tables[a].span;if(d.profile!=='custom'){const angles=a==='az'?[0,90,180,270,360]:[0,45,90,135,180];tables[a].points=angles.map((angle,i)=>({angle,mv:100+i*400,endpoint:i===0||i===angles.length-1}))}}
   if(route==='/api/azcal'||route==='/api/elcal'){const a=route.includes('az')?'az':'el',t=tables[a];if(d.op==='add_capture'){if(t.points.some(p=>p.angle===+d.angle))return reply({error:'invalid_point'},400);t.points.push({angle:+d.angle,mv:1200,endpoint:false});t.points.sort((a,b)=>a.angle-b.angle)}if(d.op==='delete')t.points=t.points.filter(p=>p.angle!==+d.angle)}
   return reply({ok:true,result:'ok'});
  }
  if(route==='/api/config')return reply(config);
  if(route==='/api/azcal'||route==='/api/elcal')return reply(tables[route.includes('az')?'az':'el']);
  if(route==='/api/status')return reply({profile:'direct',fault,axes:[]});
  if(route==='/api/adcdiag')return reply({ready:true,azValue:1200,azSensor:'ADC',elValue:1800,elSensor:'ADC'});
  return reply({error:'unknown'},404);
 });
 const ready=()=>page.waitForFunction(()=>!document.getElementById('content').disabled);
 const submit=async id=>{await page.locator('#'+id+' button').click();await page.waitForTimeout(80);await ready()};
 await page.goto('http://essential.test/settings');await ready();assert.equal(posts.length,0);assert.equal(await page.locator('#name').inputValue(),'G5500');assert.match(await page.locator('#fault').innerText(),/feedback_stale/);
 await page.locator('#resetAxis').selectOption('az');await submit('reset');assert.equal(posts.at(-1).route,'/api/motion/reset');assert.equal(posts.at(-1).d.axis,'az');assert.match(await page.locator('#fault').innerText(),/нет/);
 await page.locator('#name').fill('TEST Rotor');failure={route:'/api/settings',code:503,error:'motion_storage_failed'};await submit('general');assert.equal(await page.locator('#feedback').getAttribute('data-state'),'error');assert.equal(await page.locator('#name').inputValue(),'TEST Rotor');failure=null;await submit('general');assert.equal(config.rotorName,'TEST Rotor');
 assert.match(await page.locator('#protocols').innerText(),/rotctld/);assert.equal(await page.locator('#n1mmPort').inputValue(),'12040');assert.equal(await page.locator('#pstPort').inputValue(),'1234');
 await page.locator('#n1mmPort').fill('12041');await page.locator('#pstPort').fill('1236');await page.locator('#reportPort').fill('13011');await page.locator('#reportHost').fill('192.0.2.15');await page.locator('#n1mmFollow').check();await page.locator('#rotctldFollow').check();
 failure={route:'/api/settings',code:400,error:'port_conflict'};await submit('protocols');assert.equal(await page.locator('#feedback').getAttribute('data-state'),'error');assert.equal(await page.locator('#n1mmPort').inputValue(),'12041');assert.equal(config.n1mmPort,12040);failure=null;await submit('protocols');
 assert.equal(config.n1mmPort,12041);assert.equal(config.pstPort,1236);assert.equal(config.reportPort,13011);assert.equal(config.reportHost,'192.0.2.15');assert(config.n1mmManualFollow&&config.rotctldManualFollow);assert(!config.pstManualFollow&&!config.gs232ManualFollow);
 await page.reload();await ready();assert.equal(await page.locator('#reportHost').inputValue(),'192.0.2.15');assert(await page.locator('#n1mmFollow').isChecked());assert(!(await page.locator('#pstFollow').isChecked()));await page.locator('#reportHost').fill('');await submit('protocols');assert.equal(config.reportHost,'');
 await page.locator('#azlimit').fill('45');await page.locator('#azstall').fill('3500');await submit('drive');assert.equal(config.axes[0].outputLimit,45);assert.equal(config.axes[0].stallMs,3500);assert(!Object.keys(posts.at(-1).d).some(k=>/pid|kick|jerk|acceleration/i.test(k)));
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.screenshot({path:path.join(output,'settings-mock-'+width+'.png'),fullPage:true});
 await page.goto('http://essential.test/calibration');await ready();const count=posts.length;assert.equal(await page.locator('#azPoints tr').count(),2);assert.match(await page.locator('#elReady').innerText(),/готова/);
 await page.locator('#azPoints button').first().click();await page.waitForTimeout(80);await ready();assert(posts.slice(count).some(x=>x.route==='/api/cal2'&&x.d.p==='O'));assert.equal(posts[count].route,'/api/move');
 await page.locator('#elPoints button').last().click();await page.waitForTimeout(80);await ready();assert(posts.some(x=>x.route==='/api/cal2'&&x.d.p==='F2'));
 await page.locator('#calMode').selectOption('multi');await submit('calModeForm');assert.equal(await page.locator('#azPoints tr').count(),3);assert(await page.locator('#azAdd').isVisible());
 await page.locator('#azAngle').fill('15.5');await submit('azAdd');assert(tables.az.points.some(p=>p.angle===15.5));assert.equal(await page.locator('#azAngle').inputValue(),'');assert(posts.some(x=>x.route==='/api/azcal'&&x.d.op==='add_capture'&&x.d.angle==='15.5'));
 await page.locator('#azAngle').fill('15.5');await submit('azAdd');assert.equal(await page.locator('#feedback').getAttribute('data-state'),'error');assert.equal(await page.locator('#azAngle').inputValue(),'15.5');
 failure={route:'/api/elcal',code:503,error:'calibration_save_failed'};await page.locator('#elAngle').fill('20');await submit('elAdd');assert.equal(await page.locator('#elAngle').inputValue(),'20');assert.match(await page.locator('#feedback').innerText(),/сохранить/);failure=null;
 await page.locator('#azStart').fill('-60');await page.locator('#azSpan').fill('250');await submit('azRange');assert.equal(tables.az.start,-60);assert.equal(tables.az.end,190);
 await page.locator('#azProfile').selectOption('p36');await submit('azRange');assert(await page.locator('#azAdd').isHidden());await page.locator('#azPoints button').first().click();await page.waitForTimeout(80);await ready();assert(posts.some(x=>x.route==='/api/cal'&&x.d.p==='az0'));
 await page.locator('#readSensor').click();await page.waitForTimeout(80);assert.match(await page.locator('#sensor').innerText(),/1200/);
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));assert.deepEqual(errors,[]);await page.screenshot({path:path.join(output,'calibration-mock-'+width+'.png'),fullPage:true});
 await page.close();console.log('PASS essential UI '+width+': settings success/failure, reset, 2-point/multipoint AZ+EL, add/capture/duplicate/save failure, range and sensor read; no overflow or JS errors');
 }}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
