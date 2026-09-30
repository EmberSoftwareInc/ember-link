import { build } from 'esbuild';
import { mkdir,copyFile,readFile,writeFile,readdir } from 'node:fs/promises';
await mkdir('dist',{recursive:true});
await build({entryPoints:['src/app.js'],bundle:true,format:'esm',outfile:'dist/app.js',minify:true,legalComments:'eof',target:'es2022'});
for(const name of ['index.html','style.css'])await copyFile(name,`dist/${name}`);

await mkdir('dist/assets',{recursive:true});
await copyFile('assets/dongle-button.png','dist/assets/dongle-button.png');
let notices='Ember Link browser installer: third-party licenses\n\n';
for(const name of ['esptool-js','@noble/hashes','pako','atob-lite']) {
 const directory=`node_modules/${name}`;
 const pkg=JSON.parse(await readFile(`${directory}/package.json`,'utf8'));
 notices+=`\n${name} ${pkg.version}\n${'='.repeat(60)}\n`;
 const files=(await readdir(directory)).filter(x=>/^(license|licence|copying|notice)(\.|$)/i.test(x));
 if(!files.length)throw Error(`Missing license text for ${name}`);
 for(const file of files)notices+=await readFile(`${directory}/${file}`,'utf8')+'\n';
}
notices+='\nLILYGO T-Dongle-S3 product photo\n'+await readFile('assets/LILYGO-LICENSE.txt','utf8');
await writeFile('dist/THIRD_PARTY_LICENSES.txt',notices);
