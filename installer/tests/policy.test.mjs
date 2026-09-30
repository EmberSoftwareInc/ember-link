import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash,webcrypto} from 'node:crypto';
import {PARTS,validateManifest,checkHardware,checkedDownload} from '../src/policy.mjs';
if(!globalThis.crypto)globalThis.crypto=webcrypto;
const m=()=>({schema:1,channel:'stable',version:'0.3.6',boardId:'lilygo-t-dongle-s3',layoutId:'link-v1',chip:'ESP32-S3',flashSize:16777216,sourceCommit:'a'.repeat(40),applicationSha256:'b'.repeat(64),parts:PARTS.map(([offset,path])=>({offset,path,size:4096,sha256:'b'.repeat(64)}))});
test('stable and numbered development packages stay separate',()=>{
 assert.equal(validateManifest(m(),'stable').version,'0.3.6');
 assert.throws(()=>validateManifest(m(),'dev'));
 assert.equal(validateManifest({...m(),version:'0.3.7-dev.1',channel:'dev'},'dev').version,'0.3.7-dev.1');
 for(const version of ['0.3.7-dev.0','0.3.7-dev','0.3.7-dev.01','0.3.7-rc1','01.3.7'])assert.throws(()=>validateManifest({...m(),version,channel:'dev'},'dev'));
});
test('reject unexpected offsets, device dumps, paths and missing parts',()=>{
 for(const mutation of [x=>x.parts[0].offset=0x9000,x=>x.parts[0].path='../nvs.bin',x=>x.parts[3].size=16777216,x=>x.parts.pop(),x=>x.flashSize=8388608,x=>x.applicationSha256='c'.repeat(64)]){
 const x=m();mutation(x);assert.throws(()=>validateManifest(x,'stable'));
 }
});
test('flash requires correct chip and detected capacity',()=>{
 checkHardware('ESP32-S3','16MB');
 for(const pair of [['ESP32','16MB'],['ESP32-S3','8MB'],['ESP32-S3',undefined]])assert.throws(()=>checkHardware(...pair));
});
test('all file bytes must match the expected length and SHA-256',async()=>{
 const data=new Uint8Array([1,2,3,4]);const part={size:4,sha256:createHash('sha256').update(data).digest('hex')};
 assert.deepEqual(await checkedDownload(new Response(data),part),data);
 await assert.rejects(checkedDownload(new Response(data.slice(0,2)),part));
 await assert.rejects(checkedDownload(new Response(new Uint8Array(8)),part));
 await assert.rejects(checkedDownload(new Response(new Uint8Array(4)),part));
 await assert.rejects(checkedDownload(new Response('',{status:404}),part));
});
