// Build a separate, local-only qualification page. Never deployed by Pages.
// Usage: node tools/make_installer_interruption_test.mjs /private/tmp/new-test-dir
import {readFile,writeFile,cp,mkdir,stat} from 'node:fs/promises';
import {resolve,dirname,relative,isAbsolute,sep} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const output=process.argv[2] && resolve(process.argv[2]);
if(!output)throw Error('Provide a fresh directory outside the repository.');
// A test site must never overwrite the normal installer or any repo source.
const relativeOutput=relative(root,output);
if(output===root || (!relativeOutput.startsWith('..'+sep) && relativeOutput!=='..' && !isAbsolute(relativeOutput)))
  throw Error('Test output must be outside the repository.');
try{await stat(output);throw Error('Output already exists; choose a fresh directory.');}
catch(error){if(error.code!=='ENOENT')throw error;}
const {build}=createRequire(resolve(root,'installer/package.json'))('esbuild');
const source=await readFile(resolve(root,'installer/src/app.js'),'utf8');
const anchor='    const total=files.reduce((sum,f)=>sum+f.data.length,0);';
if(source.split(anchor).length!==2)throw Error('Installer changed; review the pause insertion point.');
const hook=`
    // Only this separate test bundle pauses. Keep ordinary flash parameters,
    // package verification and original firmware bytes unchanged.
    const begin=loader.flashDeflBegin.bind(loader);
    const block=loader.flashDeflBlock.bind(loader);
    let application=false,paused=false;
    loader.flashDeflBegin=async(size,compressed,offset)=>{
      const result=await begin(size,compressed,offset);
      application=offset===0x20000;
      return result;
    };
    loader.flashDeflBlock=async(data,seq,timeout)=>{
      const result=await block(data,seq,timeout);
      if(application && seq===1 && !paused){
        paused=true;
        // The stub acknowledges a block before writing it, then completes that
        // write while receiving the next block. Two acknowledged blocks put
        // the cut after the first application block, before the full image.
        status('POWER-CUT TEST PAUSED: two application blocks acknowledged. Unplug ONLY the spare Link now, wait five seconds, then reconnect while holding BOOT. Do not move it to a machine. Report that you have reconnected; recovery uses the normal installer.');
        await new Promise((resolve,reject)=>{
          const disconnected=event=>{
            if(event.target!==port && event.port!==port)return;
            navigator.serial.removeEventListener('disconnect',disconnected);
            reject(Error('Expected test disconnect detected. Use the normal installer to recover the spare.'));
          };
          navigator.serial.addEventListener('disconnect',disconnected);
        });
      }
      return result;
    };
`;
await mkdir(output,{recursive:true});
await cp(resolve(root,'installer/dist'),output,{recursive:true});
await build({stdin:{contents:source.replace(anchor,hook+anchor),resolveDir:resolve(root,'installer/src'),sourcefile:'power-cut-test.js'},bundle:true,format:'esm',outfile:resolve(output,'app.js'),minify:true,legalComments:'eof',target:'es2022'});
let html=await readFile(resolve(output,'index.html'),'utf8');
html=html.replace('<title>Install Ember Link</title>','<title>Ember Link · Power-cut test</title>').replace('<main>','<main><section class="card"><h1>Spare-board recovery test</h1><p>This LOCAL TEST intentionally pauses after a partial firmware write. Use only the spare dongle. After the pause, unplug as instructed. Recover at <a href="http://127.0.0.1:8794/">the normal installer</a>; this test page always pauses.</p></section>');
html=html.replace('<option value="stable">','<option value="stable" disabled>').replace('<option value="dev">','<option value="dev" selected>');
await writeFile(resolve(output,'index.html'),html);
const catalog=JSON.parse(await readFile(resolve(output,'catalog.json'),'utf8'));
if(!catalog.preview||!catalog.dev)throw Error('Requires a local Development preview package.');
catalog.stable=null;
await writeFile(resolve(output,'catalog.json'),JSON.stringify(catalog,null,2)+'\n');
console.log('Local interruption test prepared at '+output+'; normal installer unchanged.');
