// Exercise the shipped ADC page's response handling without a browser/server.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'../G5500_Fork/diagnostic_pages.hpp'),'utf8');
const freshness=source.match(/function adcFreshness\(\)\{[\s\S]*?\n\}/)[0];
const update=source.match(/async function upd\(\)\{[\s\S]*?\n\}/)[0];
const elements=new Map(),readings=new Map();let reply={ready:false},draws=0,points=0,now=1000;
function element(id) {
  if(!elements.has(id))elements.set(id,{textContent:'',dataset:{},style:{},classList:{toggle(name,value){this[name]=value;}}});
  return elements.get(id);
}
class Clock extends Date {static now(){return now;}}
const context=vm.createContext({
  Date:Clock,navigator:{onLine:true},
  document:{getElementById:element,querySelector:element},
  background:{async read(){return reply;}},
  chart:{closest(){return element('chart-card');}},
  addPoint(){++points;},draw(){++draws;},
  updateAxis(axis,raw,value){readings.set(axis,{raw,value});}
});
vm.runInContext(`let lastAdcAt=0,adcFailed=false,hasEl=true;const axisSources={},azPts=[],elPts=[];${freshness}\n${update}`,context);
const poll=()=>vm.runInContext('upd()',context);
(async()=>{
  await poll();assert.equal(draws,0);assert.equal(points,0);assert.equal(readings.size,0);
  assert.match(element('adcStatus').textContent,/Ожидаем измерения/);
  reply={ready:true,hasEl:true,azRaw:1000,azMv:900,elRaw:2000,elMv:1800};
  await poll();assert.equal(element('adcStatus').dataset.stale,'false');
  assert.deepEqual(readings.get('az'),{raw:1000,value:900});assert.equal(draws,1);assert.equal(points,2);
  now+=500;reply={ready:false};await poll();
  assert.equal(element('adcStatus').dataset.stale,'true');
  assert.match(element('adcStatus').textContent,/Нет свежих данных/);
  assert.deepEqual(readings.get('az'),{raw:1000,value:900});assert.equal(draws,1);assert.equal(points,2);
  reply={ready:true,hasEl:false,azRaw:1100,azMv:990};await poll();
  assert.equal(element('adcStatus').dataset.stale,'false');assert.equal(element('adcElCard').style.display,'none');
  assert.deepEqual(readings.get('az'),{raw:1100,value:990});assert.equal(draws,2);
  console.log('PASS: ADC pending before/after measurement, retained readings, fresh recovery and single-axis response');
})().catch(error=>{console.error(error);process.exit(1);});
