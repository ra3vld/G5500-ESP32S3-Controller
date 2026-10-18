// Offline presentation checks: no server, firmware or device access.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=fs.readFileSync(path.resolve(__dirname,'../G5500_Fork/web_page.hpp'),'utf8');
const script=source.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script); // Check syntax of the complete page script without running its network code.
const elements=Object.fromEntries(['resetDriveError','motion','forkMessage'].map(id=>[id,{textContent:'',title:'',classes:new Set(),classList:{toggle(name,on){on?elements[id].classes.add(name):elements[id].classes.delete(name)}}}]));
const context=vm.createContext({document:{getElementById:id=>elements[id]},setText:(node,text)=>node.textContent=text});
vm.runInContext(script.slice(script.indexOf('function motionFaultText('),script.indexOf('initMotionForms();',script.indexOf('function motionFaultText('))),context);
const axis=(motionFault='',extra={})=>({motionFault,motionReady:true,motionPhase:motionFault?'fault':'idle',...extra});
const state=(axes,fault='',extra={})=>({axes,fault,model:'G5500',deviceRole:'controller',motion:'STOP',...extra});
const render=s=>{context.state=s;vm.runInContext('renderDriveStatus(state)',context)};
for(const code of ['kick_start_timeout','feedback_stale','feedback_invalid','stalled','planner_error']){
 render(state([axis(code),axis()],`az_${code}`));
 assert.match(elements.motion.textContent,/^Ошибка: AZ:/);assert(elements.motion.classes.has('motion-fault'));
 assert(!elements.resetDriveError.classes.has('hide'));assert.match(elements.motion.textContent,/AZ:/);
 assert.equal(elements.motion.textContent.split('AZ:').length,2,'common fault must not duplicate the axis');
}
render(state([axis('feedback_invalid'),axis('stalled')],'el_stalled'));
assert.match(elements.motion.textContent,/AZ: неверные показания датчика\nEL: движение прекратилось/);
elements.forkMessage.textContent='Готово';assert.match(elements.motion.textContent,/неверные показания/,'command notice must not replace the fault');
render(state([axis(),axis()]));
assert(elements.resetDriveError.classes.has('hide'));assert.equal(elements.motion.textContent,'STOP');
assert.equal(elements.motion.textContent,'STOP');assert(!elements.motion.classes.has('motion-fault'));assert.equal(elements.motion.title,'');
render(state([axis(),axis()],'pwm_attach_failed'));assert.match(elements.motion.textContent,/регулировку скорости/);
render(state([axis(),axis()],'el_feedback_stale'));assert.match(elements.motion.textContent,/EL: нет свежих/);
render(state([axis(),axis()],'future_fault'));assert.match(elements.motion.textContent,/future_fault/);
render(state([axis('',{motionReady:false}),axis()]));assert.match(elements.motion.textContent,/AZ: неверные сохранённые параметры/);
render(state([axis('stalled'),axis('feedback_invalid')],'el_feedback_invalid',{deviceRole:'hud',hudOnline:true}));assert.match(elements.motion.textContent,/AZ:/);assert.match(elements.motion.textContent,/EL:/);
render(state([axis('stalled'),axis()],'az_stalled',{deviceRole:'hud',hudOnline:false}));assert(elements.resetDriveError.classes.has('hide'),'offline HUD must not present cached faults as current');
render(state([axis(),axis('stalled')],'',{model:'G800DXA'}));assert(elements.resetDriveError.classes.has('hide'),'single-axis model ignores nonexistent EL');
assert.match(source,/renderDriveStatus\(j\);/);
assert(!source.includes("setText(motion,j.motion)"),'normal motion must not overwrite the fault');
assert(!source.includes('id="driveErrors"'),'standalone fault block removed');
assert.match(source,/<h2>Наведение<\/h2>[\s\S]*?id="motion"[\s\S]*?id="resetDriveError"/);
assert.match(source,/id="resetDriveError"[^>]*onclick="resetMotion\('all'\)"/);
assert.match(source,/'\/api\/motion\/reset'\]\.includes\(path\)\)body=\{\.\.\.body,selection\}/);
(async()=>{
 const calls=[];context.forkPost=async(path,body)=>{calls.push({path,body});assert(elements.resetDriveError.disabled)};
 const start=script.indexOf('async function resetMotion('),end=script.indexOf('\n}',start)+2;
 vm.runInContext(script.slice(start,end),context);
 await vm.runInContext("resetMotion('all')",context);
 assert.equal(calls.length,1);assert.equal(calls[0].path,'/api/motion/reset');assert.equal(calls[0].body.axis,'all');
 assert.equal(elements.resetDriveError.disabled,false);
 let finish;context.forkPost=()=>new Promise(resolve=>{finish=resolve});
 const first=vm.runInContext("resetMotion('all')",context);
 await vm.runInContext("resetMotion('all')",context);assert(elements.resetDriveError.disabled);
 finish(true);await first;assert.equal(elements.resetDriveError.disabled,false);
 context.forkPost=async()=>{throw Error('offline')};
 await assert.rejects(vm.runInContext("resetMotion('all')",context));assert.equal(elements.resetDriveError.disabled,false);
 console.log('PASS: guidance fault status and reset button, HUD selection, duplicate suppression and failed-request recovery');
})().catch(error=>{console.error(error);process.exitCode=1});
console.log('PASS: guidance fault status, both axes, reset/clear, common and legacy faults, command notices, offline HUD, JS syntax');
