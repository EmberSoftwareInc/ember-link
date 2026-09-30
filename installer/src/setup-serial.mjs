// A bounded, request-correlated JSON protocol. Never retry destructive commands.
export class SetupSerial {
  constructor(port, onClosed = () => {}) {
    this.port = port; this.onClosed = onClosed; this.nextId = 1;
    this.closed = false; this.pending = null; this.reader = null; this.writer = null;
  }
  async open() {
    try {
      await this.port.open({baudRate:115200});
      this.reader = this.port.readable.getReader();
      this.writer = this.port.writable.getWriter();
      await this.port.setSignals({dataTerminalReady:true});
      this.pump = this.readLoop();
    } catch (error) { await this.close(); throw error; }
  }
  async readLoop() {
    const decoder = new TextDecoder(); let buffer = '';
    try {
      while (!this.closed) {
        const {value,done} = await this.reader.read();
        if (done) break;
        buffer += decoder.decode(value,{stream:true});
        if (buffer.length > 16384) throw Error('USB setup response exceeded its size limit.');
        let end;
        while ((end = buffer.indexOf('\n')) >= 0) {
          const line = buffer.slice(0,end).trim(); buffer = buffer.slice(end+1);
          if (!line) continue;
          let message; try { message = JSON.parse(line); } catch { continue; }
          if (this.pending && message.id === this.pending.id) {
            const pending = this.pending; this.pending = null; clearTimeout(pending.timer);
            if (message.ok === true) pending.resolve(message);
            else pending.reject(Error(message.error?.message || 'USB command failed.'));
          }
        }
      }
    } catch (error) { this.failure = error; }
    finally {
      this.fail(this.failure || Error('USB disconnected. Reconnect and check the card before retrying.'));
      this.onClosed();
      if (!this.closed) void this.close();
    }
  }
  fail(error) {
    if (this.pending) { const p = this.pending; this.pending = null; clearTimeout(p.timer); p.reject(error); }
  }
  request(cmd, fields = {}, timeout = 10000) {
    if (this.closed || !this.writer || this.pending) return Promise.reject(Error('USB setup is unavailable or busy.'));
    const id = this.nextId++;
    return new Promise((resolve,reject) => {
      const timer = setTimeout(() => {
        this.fail(Error('USB response timed out. The operation may still be running; keep Link powered and reconnect to check its result.'));
        void this.close();
      }, timeout);
      this.pending = {id,resolve,reject,timer};
      this.writer.write(new TextEncoder().encode(JSON.stringify({...fields,cmd,id})+'\n')).catch(error => {
        this.fail(error); void this.close();
      });
    });
  }
  async close() {
    if (this.closed) return;
    this.closed = true; this.fail(Error('USB session closed.'));
    try { await this.reader?.cancel(); await this.pump; } catch {}
    try { this.reader?.releaseLock(); this.writer?.releaseLock(); } catch {}
    try { await this.port.close(); } catch {}
    this.onClosed();
  }
}
export function validateCardStatus(value) {
  if (!value || typeof value.serial !== 'string' || !/^[A-Fa-f0-9]{12}$/.test(value.serial) ||
      !['maintenance','present','readable','fat32','canFormat'].every(k=>typeof value[k]==='boolean') ||
      !Number.isSafeInteger(value.capacityBytes) || value.capacityBytes < 0 ||
      (value.label !== undefined && (typeof value.label !== 'string' || !/^[\x20-\x7e]{0,11}$/.test(value.label))) ||
      (value.canRename !== undefined && typeof value.canRename !== 'boolean') ||
      (value.maintenance && value.present && (value.canFormat || value.canRename) && !/^[a-f0-9]{32}$/.test(value.challenge)))
    throw Error('Unsupported card status response. Nothing was formatted.');
  return value;
}
