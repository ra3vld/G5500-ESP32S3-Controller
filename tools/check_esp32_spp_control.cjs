const output=require('node:path').resolve(__dirname,'../build/esp32-spp-web');require('node:fs').mkdirSync(output,{recursive:true});
const {chromium}=require('playwright'),fs=require('node:fs'),assert=require('node:assert/strict');
const html=fs.readFileSync(__dirname+'/../G5500_Fork/web_page.hpp','utf8').split('R"HTML(')[1].split(')HTML"')[0];
const state={profile:'direct',ip:'192.168.3.235',rangeRevision:3,controlMode:'auto',pendingValid:false,configurationLocked:false,recovery:false,fault:'',heapFree:50000,heapMinFree:40000,allocFailures:0,rssi:-60,btConnected:true,axes:[{current:52,calibrated:true,output:0,direction:0,min:-50,max:190,speed:20,limit:69},{current:164,calibrated:true,output:0,direction:0,min:-45,max:190,speed:20,limit:50}]};
(async()=>{const b=await chromium.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});try{for(const width of [1100,390]){
 const p=await b.newPage({viewport:{width,height:850}}),posts=[],errors=[];p.on('pageerror',e=>errors.push(e.message));
 await p.route('http://minimal.test/**',async route=>{const q=route.request(),u=new URL(q.url());if(u.pathname==='/')return route.fulfill({contentType:'text/html',body:html});if(q.method()==='POST'){posts.push({path:u.pathname,body:Object.fromEntries(new URLSearchParams(q.postData()))});return route.fulfill({contentType:'application/json',body:'{"ok":true,"result":"ok"}'})}return route.fulfill({contentType:'application/json',body:JSON.stringify(state)})});
 await p.goto('http://minimal.test/');await p.waitForFunction(()=>window._forkState?.profile==='direct');
 assert.equal(await p.locator('#az').innerText(),'52.0°');assert.equal(await p.locator('#tel').inputValue(),'164.0');
 await p.locator('#taz').fill('55');await p.locator('#tel').fill('160');await p.locator('#go').click();await p.waitForTimeout(100);
 assert(posts.some(q=>q.path==='/api/target'&&q.body.az==='55'&&q.body.rangeRevision==='3'));
 const hold=p.locator('[data-axis="0"][data-dir="1"]'),box=await hold.boundingBox();await p.mouse.move(box.x+box.width/2,box.y+box.height/2);await p.mouse.down();await p.waitForTimeout(600);await p.mouse.up();await p.waitForTimeout(100);
 const moves=posts.filter(q=>q.path==='/api/move');assert(moves.some(q=>q.body.d==='H'&&q.body.az==='1'));assert(moves.some(q=>q.body.d==='S'));
 const serials=moves.map(q=>+q.body.serial).filter(Boolean);assert(serials.every((x,i)=>!i||x>serials[i-1]));
 await p.locator('#stop').click();await p.waitForTimeout(100);assert(posts.some(q=>q.path==='/api/move'&&q.body.d==='S'&&!q.body.client));
 await p.locator('#resetFault').click();await p.waitForTimeout(100);assert(posts.some(q=>q.path==='/api/motion/reset'&&q.body.axis==='all'));assert.equal(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);assert.deepEqual(errors,[]);
 await p.screenshot({path:output+'/mock-'+width+'.png'});await p.close();console.log('PASS minimal web '+width+': target, lease renew/release, STOP and bounds');
 }}finally{await b.close()}})().catch(e=>{console.error(e);process.exitCode=1});
