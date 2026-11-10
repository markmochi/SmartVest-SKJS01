const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
function suite(file){
 const name=path.basename(file).replace(/\.(?:cjs|mjs)$/,''),base=path.resolve(process.env.SJKS_TEST_OUTPUT||path.join(root,'testing/results'),name);
 const testOutput=(...parts)=>{const file=path.join(base,...parts);fs.mkdirSync(path.dirname(file),{recursive:true});return file;};
 const testOutputDir=(...parts)=>{const dir=path.join(base,...parts);fs.mkdirSync(dir,{recursive:true});return dir;};
 return {testOutput,testOutputDir};
}
module.exports={suite};
