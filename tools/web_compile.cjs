// Developer-only asset compiler. Firmware builds/checks use web_compiled.json.
// Top-level names are public to inline handlers and other classic scripts.
const fs=require('node:fs'),acorn=require('acorn'),terser=require('terser');
(async()=>{
 const code=fs.readFileSync(0,'utf8'),ast=acorn.parse(code,{ecmaVersion:2022});
 const definitions=[];let init='',at=0;
 for(const node of ast.body)if(node.type==='FunctionDeclaration'){
  init+=code.slice(at,node.start);at=node.end;definitions.push(code.slice(node.start,node.end));
 }
 init+=code.slice(at);
 const strict=ast.body.some(n=>n.directive==='use strict');
 const groups=[];let group='';
 for(const definition of definitions){
  if(group.length+definition.length>72000&&group){groups.push(group);group='';}
  group+=definition+'\n';
 }
 if(group)groups.push(group);
 const minify=async source=>{
  const result=await terser.minify(source,{ecma:2020,compress:{passes:2,toplevel:false},mangle:{toplevel:false},format:{comments:false}});
  return result.code;
 };
 const libraries=[];
 for(const source of groups)libraries.push(await minify((strict?'"use strict";':'')+source));
 process.stdout.write(JSON.stringify({compiler:'terser '+require('terser/package.json').version,libraries,init:await minify(init)}));
})().catch(e=>{console.error(e);process.exitCode=1});
