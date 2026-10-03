/* Shared by the browser and the dependency-free protocol tests. */
(function (root) {
  'use strict';
  class JsonLines {
    constructor(onMessage, onNoise = () => {}, limit = 16384) { this.message = onMessage; this.noise = onNoise; this.limit = limit; this.buffer = ''; this.discard = false; }
    push(chunk) {
      for (const char of chunk) {
        if (char === '\n') {
          if (!this.discard && this.buffer.trim()) {
            let value;
            try { value = JSON.parse(this.buffer); } catch { this.noise('Ignored non-JSON serial output'); }
            if (value && typeof value === 'object' && !Array.isArray(value) && typeof value.type === 'string') this.message(value);
          }
          this.buffer = ''; this.discard = false;
        } else if (!this.discard) {
          this.buffer += char;
          if (this.buffer.length > this.limit) { this.buffer = ''; this.discard = true; this.noise('Serial line exceeded the size limit'); }
        }
      }
    }
  }
  class SerialLink {
    constructor(onMessage, onClose, onNoise) { this.onMessage = onMessage; this.onClose = onClose; this.onNoise = onNoise; this.port = null; this.reader = null; this.writer = null; this.readTask = null; this.writeTask = Promise.resolve(); this.closing = null; }
    async connect() {
      if (!globalThis.isSecureContext || !navigator.serial) throw new Error('Web Serial needs desktop Chrome or Edge in a secure context. Open this file directly, or serve it from localhost.');
      if (this.port) return;
      const port = await navigator.serial.requestPort();
      await port.open({ baudRate: 115200, bufferSize: 32768 });
      this.port = port;
      this.writer = port.writable.getWriter();
      this.readTask = this.readLoop(port);
    }
    async readLoop(port) {
      const decoder = new TextDecoder();
      const parser = new JsonLines(this.onMessage, this.onNoise);
      this.reader = port.readable.getReader();
      let reason = 'Serial disconnected';
      try {
        while (this.port === port) {
          const { value, done } = await this.reader.read();
          if (done) break;
          parser.push(decoder.decode(value, { stream: true }));
        }
      } catch (error) { reason = error.message; }
      finally { this.reader.releaseLock(); this.reader = null; }
      // Run cleanup outside this read task to avoid awaiting itself.
      if (!this.closing) queueMicrotask(() => this.disconnect(reason));
    }
    send(message) {
      const writer = this.writer;
      if (!writer || this.closing) return Promise.reject(new Error('Master is disconnected'));
      const data = new TextEncoder().encode(JSON.stringify(message) + '\n');
      const task = this.writeTask.catch(() => {}).then(() => writer.write(data));
      this.writeTask = task;
      return task;
    }
    disconnect(reason = 'Disconnected by user') {
      if (this.closing) return this.closing;
      if (!this.port) return Promise.resolve();
      this.closing = this.close(reason);
      return this.closing;
    }
    async close(reason) {
      const port = this.port;
      try {
        if (this.reader) await this.reader.cancel().catch(() => {});
        if (this.readTask) await this.readTask.catch(() => {});
        if (this.writer) {
          await this.writer.abort().catch(() => {});
          await this.writeTask.catch(() => {});
          this.writer.releaseLock(); this.writer = null;
        }
        await port.close();
      } catch (error) { reason += ': ' + error.message; }
      finally { this.port = null; this.readTask = null; this.closing = null; this.onClose(reason); }
    }
  }
  root.RelaySerial = { JsonLines, SerialLink };
  if (typeof module !== 'undefined') module.exports = root.RelaySerial;
})(globalThis);
