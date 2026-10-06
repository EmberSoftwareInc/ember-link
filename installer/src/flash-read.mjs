import {md5} from '@noble/hashes/legacy';
import {bytesToHex} from '@noble/hashes/utils';
// esptool-js 0.7.0 readFlash returns after the final data ACK but does not consume
// the stub's trailing 16-byte digest frame. Consume it before another command can
// flush or misinterpret it. Keep tracing disabled: data can contain credentials.
export async function readFlashVerified(loader,address,size){
  try{
    const bytes=await loader.readFlash(address,size);
    const digest=await loader.transport.read(loader.FLASH_READ_TIMEOUT);
    if(!(bytes instanceof Uint8Array)||bytes.length!==size||!(digest instanceof Uint8Array)||digest.length!==16||
      bytesToHex(digest)!==bytesToHex(md5(bytes)))throw Error('Flash read checksum verification failed.');
    return bytes;
  }catch(error){throw Error(`Could not read flash at 0x${address.toString(16)}. ${error.message}`);}
}
