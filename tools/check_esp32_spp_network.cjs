const {chromium}=require('playwright'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),zlib=require('node:zlib');
const root=path.resolve(__dirname,'..'),out=path.join(root,'build/esp32-spp-network');fs.mkdirSync(out,{recursive:true});
const header=fs.readFileSync(path.join(root,'G5500_Fork/web_assets.hpp'),'utf8'),section=header.split('// BEGIN ASSETS: web_page_spp.hpp\n')[1].split('// END ASSETS: web_page_spp.hpp')[0];
const bytes=section.match(/ESSENTIAL_NETWORK_PAGE_GZIP\[\] PROGMEM = \{([\s\S]*?)\};/);
assert(bytes,'compact network page must be embedded');
const html=zlib.gunzipSync(Buffer.from([...bytes[1].matchAll(/0x([0-9a-f]{2})/g)].map(m=>parseInt(m[1],16)))).toString();
const original=()=>({choice:0,wifiMode:3,connected:true,wifiConnecting:false,ssid:'Home',staIp:'192.0.2.20',apIp:'192.168.4.1',rssi:-60,ethernetSupported:false,ethConnected:false,ethIp:'0.0.0.0',wifi:{mode:'dhcp',address:'192.0.2.20',mask:'255.255.255.0',gateway:'192.0.2.1',dns1:'192.0.2.1',dns2:'0.0.0.0'},eth:{mode:'dhcp',address:'0.0.0.0'},trial:false,trialBooted:false,trialRemainingMs:180000,token:11});
(async()=>{const browser=await chromium.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
try{for(const width of [1100,390]){
 const page=await browser.newPage({viewport:{width,height:900}});let state=original(),failure=null,hold=false,reads=0;const posts=[],errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 await page.route('http://*/**',async route=>{
  const q=route.request(),u=new URL(q.url()),p=u.pathname,d=Object.fromEntries(new URLSearchParams(q.postData()||''));
  if(p==='/network')return route.fulfill({contentType:'text/html',body:html});
  if(q.method()==='POST'){
   posts.push({path:p,data:d});if(failure?.path===p)return route.fulfill({status:failure.code,contentType:'application/json',body:JSON.stringify({error:failure.error})});
   if(p==='/api/network'){state.choice=+d.choice;state.wifi.mode=d.wifiMode||state.wifi.mode;for(const k of ['Address','Mask','Gateway','Dns1','Dns2'])if('wifi'+k in d)state.wifi[k[0].toLowerCase()+k.slice(1)]=d['wifi'+k];state.trial=true;state.trialBooted=false}
   if(p==='/api/network/confirm'){assert.equal(d.address,u.hostname);assert.equal(+d.token,state.token);state.trial=false}
   if(p==='/api/network/rollback')state.trial=false;
   if(p==='/api/wifi')state.ssid=d.ssid;
   if(hold)await new Promise(r=>setTimeout(r,350));return route.fulfill({contentType:'application/json',body:'{"ok":true}'});
  }
  if(p==='/api/network'){++reads;if(hold)await new Promise(r=>setTimeout(r,350));return route.fulfill({contentType:'application/json',body:JSON.stringify(state)})}
  return route.fulfill({status:404,body:'not found'});
 });
 const ready=()=>page.waitForFunction(()=>!document.getElementById('credentialsFields').disabled);
 await page.goto('http://192.0.2.20/network');await ready();assert.equal(posts.length,0);
 assert.match(await page.locator('#status').innerText(),/192.168.4.1/);assert.equal(await page.locator('#ssid').inputValue(),'Home');
 assert(await page.locator('#choice option[value="4"]').isDisabled());
 await page.locator('#wifiMode').selectOption('static');await page.locator('#wifiAddress').fill('192.0.2.25');
 await page.locator('#wifiDns1').fill('9.9.9.9');const beforeRead=reads;await page.locator('#refresh').click();await page.waitForFunction(()=>!document.getElementById('refresh').disabled);assert(reads>beforeRead);assert.equal(await page.locator('#wifiAddress').inputValue(),'192.0.2.25','poll must preserve IP draft');
 await page.screenshot({path:path.join(out,'network-static-'+width+'.png'),fullPage:true});
 failure={path:'/api/network',code:400,error:'invalid_netmask'};await page.locator('#network button').click();await ready();
 assert.match(await page.locator('#message').innerText(),/маска/);assert.equal(await page.locator('#wifiAddress').inputValue(),'192.0.2.25');assert(!posts.some(p=>p.path==='/api/network/confirm'));
 failure=null;await page.locator('#network button').click();await ready();assert(state.trial);await page.waitForFunction(()=>!document.getElementById('trial').hidden);assert(await page.locator('#network button').isDisabled());assert(await page.locator('#confirm').isDisabled());assert.match(await page.locator('#newAddress').getAttribute('href'),/192\.0\.2\.25\/network/);
 assert.deepEqual(posts.filter(p=>p.path==='/api/network').at(-1).data,{choice:'0',wifiMode:'static',wifiAddress:'192.0.2.25',wifiMask:'255.255.255.0',wifiGateway:'192.0.2.1',wifiDns1:'9.9.9.9',wifiDns2:'0.0.0.0'});
 state.trialBooted=true;state.token=22;state.staIp='192.0.2.25';await page.goto('http://192.0.2.25/network');await ready();assert(!(await page.locator('#confirm').isDisabled()));
 await page.screenshot({path:path.join(out,'network-trial-'+width+'.png'),fullPage:true});
 assert(!posts.some(p=>p.path==='/api/network/confirm'),'opening new address never auto-confirms');
 failure={path:'/api/network/confirm',code:503,error:'network_storage_failed'};await page.locator('#confirm').click();await ready();assert(state.trial&&await page.locator('#trial').isVisible());failure=null;
 await page.locator('#confirm').click();await ready();assert(!state.trial);assert(await page.locator('#trial').isHidden());
 // DHCP leaves the previous static settings intact; rollback uses the fresh token.
 await page.locator('#wifiMode').selectOption('dhcp');await page.locator('#network button').click();await ready();assert.deepEqual(posts.filter(p=>p.path==='/api/network').at(-1).data,{choice:'0',wifiMode:'dhcp'});
 state.trialBooted=true;state.token=33;await page.locator('#refresh').click();await page.waitForFunction(()=>!document.getElementById('rollback').disabled);await page.locator('#rollback').click();await ready();assert.equal(posts.at(-1).data.token,'33');
 await page.locator('#ssid').fill('New Home');await page.locator('#wifiPass').fill('new-password');
 failure={path:'/api/wifi',code:500,error:'write_failed'};await page.locator('#credentials button').click();await ready();assert.equal(await page.locator('#ssid').inputValue(),'New Home');assert.equal(await page.locator('#wifiPass').inputValue(),'new-password');
 failure=null;await page.locator('#credentials button').click();await ready();assert.equal(state.ssid,'New Home');assert.equal(await page.locator('#wifiPass').inputValue(),'');assert(!('wifiMode' in posts.at(-1).data));
 await page.locator('#apPass').fill('ap-password');failure={path:'/api/ap',code:503,error:'network_storage_failed'};await page.locator('#accessPoint button').click();await ready();assert.equal(await page.locator('#apPass').inputValue(),'ap-password');failure=null;
 await page.locator('#accessPoint button').click();await ready();assert.deepEqual(posts.at(-1).data,{pass:'ap-password'});
 await page.locator('#openAp').check();await page.locator('#accessPoint button').click();await ready();assert.deepEqual(posts.at(-1).data,{pass:''});
 // STOP stays outside locked forms during a slow write.
 hold=true;await page.locator('#network button').click();await page.locator('#stop').click();await page.waitForTimeout(80);assert(posts.some(p=>p.path==='/api/move'&&p.data.d==='S'));hold=false;await ready();
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));assert.deepEqual(errors,[]);
 await page.screenshot({path:path.join(out,'network-'+width+'.png'),fullPage:true});await page.close();
 console.log('PASS compact network '+width+': static/DHCP, dirty draft, storage errors, manual confirmation/rollback, Wi-Fi/AP passwords and independent STOP');
 }}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
