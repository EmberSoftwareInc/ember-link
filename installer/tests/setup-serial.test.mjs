import test from 'node:test';
import assert from 'node:assert/strict';
import {SetupSerial,validateCardStatus} from '../src/setup-serial.mjs';
function fakePort(reply) {
  let input; const sent=[];
  return {sent,readable:new ReadableStream({start(c){input=c;}}),
    writable:new WritableStream({write(bytes){const req=JSON.parse(new TextDecoder().decode(bytes));sent.push(req);reply?.(req,message=>input.enqueue(new TextEncoder().encode(message)));}}),
    async open(){},async close(){},async setSignals(){}};
}
test('USB responses are correlated and split JSON lines are reassembled',async()=>{
  const p=fakePort((req,emit)=>{emit(JSON.stringify({id:req.id+1,ok:true})+'\n');emit(`{"id":${req.id},"ok":`);emit('true,"present":true}\n');});
  const c=new SetupSerial(p);await c.open();assert.equal((await c.request('card_status')).present,true);await c.close();
});
test('device rejection is surfaced without retrying a format',async()=>{
  const p=fakePort((req,emit)=>emit(JSON.stringify({id:req.id,ok:false,error:{message:'confirmation required'}})+'\n'));
  const c=new SetupSerial(p);await c.open();await assert.rejects(c.request('card_format'),/confirmation required/);assert.equal(p.sent.length,1);await c.close();
});
test('timed out format is never retried and a fresh session is required',async()=>{
  const p=fakePort();const c=new SetupSerial(p);await c.open();
  await assert.rejects(c.request('card_format',{},15),/may still be running/);
  assert.equal(p.sent.length,1);await assert.rejects(c.request('card_format'),/unavailable/);await c.close();
});
test('oversized USB response aborts the operation',async()=>{
  const p=fakePort((req,emit)=>emit('x'.repeat(16385)));const c=new SetupSerial(p);await c.open();
  await assert.rejects(c.request('card_status'),/size limit/);await c.close();
});
test('format capability requires typed card state and a device-session challenge',()=>{
  const valid={serial:'E072A1C285F4',maintenance:true,present:true,readable:false,fat32:false,canFormat:true,capacityBytes:8*1024**3,challenge:'a'.repeat(32)};
  assert.equal(validateCardStatus(valid),valid);
  for(const invalid of [{...valid,challenge:''},{...valid,maintenance:'true'},{...valid,serial:'wrong'},{...valid,capacityBytes:-1}])assert.throws(()=>validateCardStatus(invalid));
});
test('naming status accepts older firmware but validates new fields and rename-only challenges',()=>{
  const old={serial:'E072A1C285F4',maintenance:true,present:true,readable:true,fat32:true,canFormat:false,capacityBytes:64*1024**3};
  assert.equal(validateCardStatus(old),old);
  const named={...old,canRename:true,label:'EMBER LINK',challenge:'a'.repeat(32)};
  assert.equal(validateCardStatus(named),named);
  for(const change of [{challenge:''},{label:'a'.repeat(12)},{label:'bad\nname'},{label:12},{canRename:'true'}])assert.throws(()=>validateCardStatus({...named,...change}));
});
test('rename timeout never retries or turns into a format',async()=>{
  const p=fakePort();const c=new SetupSerial(p);await c.open();
  await assert.rejects(c.request('card_rename',{confirm:'RENAME_MICROSD'},15),/may still be running/);
  assert.deepEqual(p.sent.map(r=>r.cmd),['card_rename']);
  await assert.rejects(c.request('card_rename'),/unavailable/);await c.close();
});
