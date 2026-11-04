// Track the one acquisition run. Stop at a checkpoint, then finish restoration.
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {EventEmitter} = require('node:events');

class StopRequested extends Error {
  constructor() {super('Run stopped at a checkpoint'); this.name = 'StopRequested';}
}

class RunControl {
  constructor({id, save}) {
    this.save = save;
    this.events = new EventEmitter();
    this.state = {id, pid: process.pid, mode: 'running', active: false, checkpoint: 'starting'};
    this.stopSignal = () => {
      if (this.state.mode !== 'running') return;
      this.state.mode = 'stopping';
      this.events.emit('stop');
    };
  }
  async start() {
    this.state.directory = await fs.mkdtemp(path.join(os.tmpdir(), 'ns2-'));
    await fs.chmod(this.state.directory, 0o700).catch(() => {});
    this.started = true;
    process.on('SIGINT', this.stopSignal);
    process.on('SIGTERM', this.stopSignal);
    await this.save();
  }
  async checkpoint(name) {
    this.state.active = false;
    this.state.checkpoint = name;
    await this.save();
    if (['stopping', 'closed'].includes(this.state.mode)) throw new StopRequested();
  }
  async begin(name) {
    await this.checkpoint('before ' + name);
    this.state.active = true;
    this.state.checkpoint = name;
    await this.save();
  }
  async end(name) {
    this.state.active = false;
    this.state.checkpoint = 'after ' + name;
    await this.save();
  }
  retire() {
    this.state.active = false;
    this.state.mode = 'retiring';
    this.events.emit('retire');
  }
  // AFTER in run-control.cjs
  async close() {
    process.off('SIGINT', this.stopSignal);
    process.off('SIGTERM', this.stopSignal);
    this.state.active = false;
    this.state.mode = 'closed';
    // Safe recursive removal handles lingering socket/tmp entries on Windows
    if (this.started) await fs.rm(this.state.directory, {recursive: true, force: true});
    this.started = false;
  }
}

function reportWriter(file, snapshot) {
  let queue = Promise.resolve();
  return () => {
    const bytes = JSON.stringify(snapshot(), (_, v) => typeof v === 'bigint' ? v.toString() : v, 2) + '\n';
    queue = queue.catch(() => {}).then(async () => {
      // 1. Unique temporary filename prevents NTFS asynchronous delete collisions
      const temporary = `${file}.${process.pid}.${Date.now()}.${Math.random().toString(36).slice(2)}.tmp`;
      await fs.writeFile(temporary, bytes, {mode: 0o600});
      // 2. Retry loop with linear backoff for transient Windows file locks
      for (let attempt = 0; attempt < 20; attempt++) {
        try {
          await fs.rename(temporary, file);
          return;
        } catch (err) {
          if (['EPERM', 'EBUSY', 'EACCES', 'EEXIST'].includes(err.code) && attempt < 19) {
            await new Promise(resolve => setTimeout(resolve, 20 + attempt * 10));
          } else if (attempt === 19) {
            // 3. Fallback: copyFile overwrites existing content even if delete lock is active
            try {
              await fs.copyFile(temporary, file);
              await fs.unlink(temporary).catch(() => {});
              return;
            } catch {
              await fs.unlink(temporary).catch(() => {});
              throw err;
            }
          } else {
            await fs.unlink(temporary).catch(() => {});
            throw err;
          }
        }
      }
    });
    return queue;
  };
}
module.exports = {RunControl, StopRequested, reportWriter};
