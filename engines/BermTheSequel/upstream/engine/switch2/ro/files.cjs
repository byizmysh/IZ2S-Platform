// Native FS client operations on retained handles; all buffers belong to s.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {packet} = require("../../shared/ipc.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function pathRequest(command, input, address, length = 0x301) {
  const bytes = packet(command, input), pointer = BigInt(address);
  bytes.writeUInt32LE(4 | 1 << 16);
  bytes.writeUInt32LE(Number(BigInt(length) << 16n | (pointer >> 36n & 7n) << 6n |
    (pointer >> 32n & 15n) << 12n), 8);
  bytes.writeUInt32LE(Number(pointer & 0xffffffffn), 12);
  return bytes;
}

async function dispatch(context, request, state, domain) {
  const {s, live, save} = context;
  const command = request.command.slice('root-native-'.length);
  if (command === 'code-close') {
    const handle = Number(request.handle);
    const code = state.codeFileSystems?.find(item => item.handle === handle);
    check(!s.threads.size && code && s.handles.has(handle) &&
      code !== state.codeFileSystems[0] && !code.domainRoot,
    'Close requires an idle analysis CodeFS; the retained binding is preserved');
    await s.close(handle);
    code.closedAt = new Date().toISOString();
    (state.closedCodeFileSystems ||= []).push(code);
    state.codeFileSystems = state.codeFileSystems.filter(item => item !== code);
    await save();
    return {handle, title: code.title, closed: true, at: code.closedAt};
  }
  if (command === 'code-domain') {
    const handle = Number(request.handle);
    const code = state.codeFileSystems?.find(item => item.handle === handle);
    check(code && s.handles.has(handle), 'Code-domain conversion requires the owned CodeFS');
    if (!code.domainRoot) {
      const reply = await domain.send(s, 'RO CodeFS ConvertToDomain', handle, domain.wire.control(0));
      check(!reply.result && reply.data.readUInt32LE() > 0, 'CodeFS domain conversion failed');
      code.domainRoot = reply.data.readUInt32LE();
      check(code.domainRoot <= 0x4000, 'CodeFS object ID exceeds the native domain capacity');
      await save();
    }
    return {handle, root: code.domainRoot};
  }
  if (command === 'donor-cleanup') {
    const donor = state.donor;
    check(donor?.ready && !donor.pendingRemoval && !s.threads.size && !s.pendingIpc && state.worker?.joined,
      'Donor cleanup requires the completed launch and an idle worker');
    const filesystem = require('../app/filesystem.cjs');
    const donorMount = donor.mount || 'save';
    const handle = await require('../app/mounted-save.cjs').cloneMountedFilesystem(s, donorMount);
    try {
      for (const item of donor.files.filter(item => item.created && !item.removed)) {
        check(['main', 'main.npdm'].includes(item.name) && item.verified, 'Unknown donor file');
        const name = '/' + item.name;
        const opened = await filesystem.openFile(s, handle, name);
        check(!opened.result, 'Owned donor file is missing');
        try {
          check(await filesystem.fileSize(s, opened.handle) === BigInt(item.size) &&
            hash(await filesystem.readFile(s, opened.handle, 0, item.size)) === item.sha256,
          'Owned donor file changed');
        } finally {await s.close(opened.handle);}
        donor.pendingRemoval = item.name; await save();
        const filename = Buffer.alloc(0x301); filename.write(name);
        await s.write(s.base + 0x2000, filename);
        const removed = await domain.send(s, 'Remove owned RO donor ' + name, handle,
          pathRequest(1, Buffer.alloc(0), s.base + 0x2000));
        check(!removed.result, 'Donor file removal failed');
        check(!(await s.ipc('Commit donor cleanup', handle, 10)).result, 'Donor cleanup commit failed');
        check((await filesystem.pathMethod(s, handle, 7, name)).canonicalResult === 0x202,
          'Owned donor file still exists');
        item.removed = true; delete donor.pendingRemoval; await save();
      }
    } finally {await s.close(handle);}
    donor.cleanedUp = true; await save();
    return {cleanedUp: true, files: donor.files.filter(item => item.removed).map(item => item.name)};
  }
  if (command === 'donor-prepare') {
    const manifest = require('./payload/manifest.json');
    const main = await fs.readFile(path.join(__dirname, 'payload/main'));
    const policy = await fs.readFile(path.join(__dirname, live.session + '-code-0400000000000037/main.npdm'));
    check(manifest.platform === live.platform && manifest.programId === '0400000000000037' &&
      main.length === manifest.payloadBytes && hash(main) === manifest.payloadSha256 &&
      hash(policy) === manifest.policySha256, 'The payload or this console\'s RO policy is unsupported');
    check(!s.threads.size, 'Prepare the donor before launching a worker');
    if (state.donor?.ready) return state.donor;
    const directory = path.join(__dirname, '../app');
    if (!state.donor) {
      const captured = require(path.join(directory, 'mounts.json'));
      const donorMount = captured.mounts?.some(m => m.name === 'tmp') ? 'tmp' : 'save';
      const handle = await require(path.join(directory, 'mounted-save.cjs')).cloneMountedFilesystem(s, donorMount);
      state.donor = {handle, mount: donorMount, files: [], checkedAbsent: [], created: new Date().toISOString()};
      await save();
    }
    const donor = state.donor;
    check(s.handles.has(donor.handle) && !donor.root, 'Donor session is unavailable or already converted');
    const pathCall = async (name, id, input = Buffer.alloc(0)) => {
      const filename = Buffer.alloc(0x301); filename.write('/' + name);
      await s.write(s.base + 0x2000, filename);
      return domain.send(s, 'RO donor /' + name + ' command ' + id, donor.handle,
        pathRequest(id, input, s.base + 0x2000));
    };
    if (!donor.preflightComplete) {
      const names = ['main', 'main.npdm', 'rtld', 'sdk', ...Array.from({length: 10}, (_, i) => 'subsdk' + i)];
      for (const name of names) {
        const reply = await pathCall(name, 7);
        if (!reply.result && ['main', 'main.npdm'].includes(name)) {
          check(reply.data.readUInt32LE() === 1, 'Existing donor path is not a file');
          const file = name === 'main' ? path.join(__dirname, 'payload/main') :
            path.join(__dirname, live.session + '-code-0400000000000037/main.npdm');
          const expected = await fs.readFile(file), mode = Buffer.alloc(4); mode.writeUInt32LE(1);
          check(expected.length > 0 && expected.length <= 0x10000, 'Existing donor comparison exceeds its buffer');
          const child = domain.oneHandle(await pathCall(name, 8, mode));
          let stale;
          try {
            const size = await s.ipc('Existing donor file size', child, 4);
            check(!size.result, 'Existing donor size query failed');
            const length = Number(size.data.readBigUInt64LE());
            check(length > 0 && length <= 0x10000, 'Existing donor exceeds its comparison buffer');
            const args = Buffer.alloc(24); args.writeBigUInt64LE(BigInt(length), 16);
            const read = await s.ipc('Existing donor verification', child, 0, args,
              {receive: [{address: s.base + 0x4000, size: length, mode: 1}]});
            check(!read.result && read.data.readBigUInt64LE() === BigInt(length), 'Existing donor read was incomplete');
            const current = await s.readOwned(s.base + 0x4000, length);
            if (current.equals(expected)) {
              if (!donor.files.some(f => f.name === name)) donor.files.push({name, file, size: expected.length,
                sha256: hash(expected), verified: true, reused: true});
            } else stale = {name, size: length, sha256: hash(current)};
          } finally { await s.close(child); }
          if (stale) {
            donor.pendingStaleRemoval = stale; await save();
            const removed = await pathCall(name, 1);
            check(!removed.result, 'Stale donor /' + name + ' removal failed');
            check(!(await s.ipc('Commit stale donor removal', donor.handle, 10)).result,
              'Stale donor removal commit failed');
            check((await pathCall(name, 7)).canonicalResult === 0x202,
              'Stale donor /' + name + ' still exists');
            (donor.removedStale ||= []).push(stale);
            delete donor.pendingStaleRemoval;
            if (!donor.checkedAbsent.includes(name)) donor.checkedAbsent.push(name);
            await save();
          }
        } else {
          check(reply.canonicalResult === 0x202, 'The donor root already contains /' + name + ' or lookup failed');
          if (!donor.checkedAbsent.includes(name)) donor.checkedAbsent.push(name);
        }
      }
      donor.preflightComplete = true;
      await save();
    }
    for (const name of ['main.npdm', 'main']) {
      const file = name === 'main' ? path.join(__dirname, 'payload/main') :
        path.join(__dirname, live.session + '-code-0400000000000037/main.npdm');
      const bytes = await fs.readFile(file), digest = hash(bytes);
      check(bytes.length > 0 && bytes.length <= 0x100000, 'Unexpected donor file size');
      const previous = donor.files.find(f => f.name === name);
      if (previous?.verified) { check(previous.sha256 === digest, 'Prepared donor content changed'); continue; }
      check(!previous, 'An incomplete donor write requires inspection');
      const input = Buffer.alloc(16); input.writeBigUInt64LE(BigInt(bytes.length), 8);
      const created = await pathCall(name, 0, input);
      check(!created.result, 'Donor file creation failed: ' + created.canonicalResult);
      const entry = {name, file, size: bytes.length, sha256: digest, created: true};
      donor.files.push(entry); await save();
      const mode = Buffer.alloc(4); mode.writeUInt32LE(3);
      const opened = await pathCall(name, 8, mode);
      const child = domain.oneHandle(opened);
      try {
        for (let offset = 0; offset < bytes.length; offset += 0x10000) {
          const part = bytes.subarray(offset, offset + 0x10000), args = Buffer.alloc(24);
          args.writeUInt32LE(1); args.writeBigUInt64LE(BigInt(offset), 8);
          args.writeBigUInt64LE(BigInt(part.length), 16);
          await s.write(s.base + 0x4000, part);
          const written = await s.ipc('RO donor write', child, 1, args,
            {send: [{address: s.base + 0x4000, size: part.length, mode: 1}]});
          check(!written.result, 'Donor write failed');
        }
        const chunks = [];
        for (let offset = 0; offset < bytes.length; offset += 0x10000) {
          const size = Math.min(0x10000, bytes.length - offset), args = Buffer.alloc(24);
          args.writeBigUInt64LE(BigInt(offset), 8); args.writeBigUInt64LE(BigInt(size), 16);
          const read = await s.ipc('RO donor readback', child, 0, args,
            {receive: [{address: s.base + 0x4000, size, mode: 1}]});
          check(!read.result && read.data.readBigUInt64LE() === BigInt(size), 'Donor readback failed');
          chunks.push(await s.readOwned(s.base + 0x4000, size));
        }
        check(hash(Buffer.concat(chunks)) === digest, 'Donor file hash differs');
        entry.verified = true;
        await save();
      } finally { await s.close(child); }
    }
    check(!(await s.ipc('RO donor commit', donor.handle, 10)).result, 'Donor commit failed');
    const converted = await domain.send(s, 'RO donor ConvertToDomain', donor.handle, domain.wire.control(0));
    check(!converted.result && converted.data.readUInt32LE() > 0, 'Donor domain conversion failed');
    donor.root = converted.data.readUInt32LE();
    check(donor.root <= 0x4000, 'Donor object ID exceeds the native domain capacity');
    donor.ready = true; await save();
    return donor;
  }
  if (command === 'code-open') {
    const handle = Number(request.handle), title = BigInt(request.title);
    const dumpDirectory = process.env.NS2_FIRMWARE_DUMP_DIR;
    check(!request.dumpOutput || dumpDirectory && path.isAbsolute(dumpDirectory),
      'Firmware capture requires a dedicated output directory');
    check(state.domainCopies?.some(item => item.handle === handle) && s.handles.has(handle),
      'Code open requires a retained copied root');
    const existing = !request.fresh && state.codeFileSystems?.find(item => item.title === title.toString() && s.handles.has(item.handle));
    if (existing) return existing;
    const attributes = Number(request.attributes ?? 1), storage = Number(request.storage ?? 3);
    check(Number.isInteger(attributes) && attributes >= 0 && attributes <= 15, 'Invalid content attributes');
    check([3, 4, 5].includes(storage), 'Unsupported code storage');
    const input = Buffer.alloc(16); input[0] = attributes; input[1] = storage; input.writeBigUInt64LE(title, 8);
    const infoSize = Number(request.infoSize ?? 0x1a4);
    check(Number.isInteger(infoSize) && infoSize >= 0x100 && infoSize <= 0x2000, 'Invalid code-info capacity');
    const output = s.base + 0x4000, guard = Buffer.alloc(32, 0x5c);
    await s.write(output - 32, Buffer.concat([guard, Buffer.alloc(infoSize), guard]));
    const reply = await s.ipc('RO pivot: OpenCodeFileSystem', handle, 0, input,
      {receive: [{address: output, size: infoSize}]});
    const entry = {at: new Date().toISOString(), operation: command, sourceRoot: handle,
      title: title.toString(), storage, attributes, infoSize, result: reply.result, canonicalResult: reply.canonicalResult};
    state.calls.push(entry); await save();
    if (reply.result) return entry;
    entry.handle = domain.oneHandle(reply);
    (state.codeFileSystems ||= []).push(entry); await save();
    check((await s.readOwned(output - 32, 32)).equals(guard) &&
      (await s.readOwned(output + infoSize, 32)).equals(guard), 'Code-info output exceeded its buffer');
    const info = await s.readOwned(output, infoSize);
    const directory = request.dumpOutput ?
      path.join(dumpDirectory, title.toString(16).padStart(16, '0')) :
      path.join(__dirname, live.session + '-code-' + title.toString(16).padStart(16, '0'));
    await fs.mkdir(directory, {recursive: true, mode: 0o700});
    entry.directory = directory;
    entry.codeInfo = {file: path.join(directory, 'code-info.bin'), sha256: hash(info),
      isSigned: info[infoSize - 4], targetHash: info.subarray(infoSize - 0x24, infoSize - 4).toString('hex')};
    await fs.writeFile(entry.codeInfo.file, info, {mode: 0o600});
    await save();
    return entry;
  }
  if (command === 'file-capture') {
    const handle = Number(request.handle), name = request.name;
    const code = state.codeFileSystems?.find(item => item.handle === handle);
    check(code && s.handles.has(handle) && (['main', 'main.npdm', 'rtld', 'sdk'].includes(name) ||
      code.listing?.entries.some(entry => entry.type === 1 && entry.name === name)),
      'Capture requires an owned CodeFS and a code filename');
    const filename = Buffer.alloc(0x301); filename.write('/' + name);
    await s.write(s.base + 0x2000, filename);
    const input = Buffer.alloc(4); input.writeUInt32LE(1);
    const opened = await domain.send(s, 'RO pivot: open /' + name, handle,
      pathRequest(8, input, s.base + 0x2000));
    check(!opened.result, 'Code file did not open: ' + opened.canonicalResult);
    const child = domain.oneHandle(opened);
    let safeToClose = false;
    try {
      const sizeReply = await s.ipc('RO pivot: code file size', child, 4);
      safeToClose = true;
      check(!sizeReply.result, 'Code file size failed');
      const size = Number(sizeReply.data.readBigUInt64LE());
      check(Number.isSafeInteger(size) && size > 0 && size <= 256 * 1024 * 1024, 'Unexpected code-file size');
      const length = request.limit === undefined ? size : Math.min(size, Number(request.limit));
      check(Number.isInteger(length) && length > 0, 'Invalid capture limit');
      const output = s.base + 0x4000;
      const capacity = Math.min(0x10000, s.bytes - 0x4000);
      const file = path.join(code.directory, name + (length === size ? '' : '.sample'));
      const temporary = file + '.partial';
      const out = await fs.open(temporary, 'wx', 0o600);
      const digest = crypto.createHash('sha256');
      let header = Buffer.alloc(0), captured = 0;
      try {
        for (let offset = 0; offset < length;) {
          const n = Math.min(capacity, length - offset), data = Buffer.alloc(24);
          data.writeBigUInt64LE(BigInt(offset), 8); data.writeBigUInt64LE(BigInt(n), 16);
          safeToClose = false;
          const reply = await s.ipc('RO pivot: read code file', child, 0, data,
            {receive: [{address: output, size: n, mode: 1}]});
          if (reply.result) safeToClose = true;
          check(!reply.result && reply.data.readBigUInt64LE() === BigInt(n), 'Code file read was short or failed');
          const bytes = await s.readOwned(output, n);
          safeToClose = true;
          const written = await out.write(bytes, 0, bytes.length, offset);
          check(written.bytesWritten === bytes.length, 'Local code-file write was short');
          digest.update(bytes);
          if (header.length < 0x100) header = Buffer.concat([header, bytes]).subarray(0, 0x100);
          offset += n; captured = offset;
        }
        await out.sync();
      } finally {await out.close();}
      check(captured === length, 'Code-file capture stopped early');
      await fs.link(temporary, file); // Never replace a pre-existing capture.
      await fs.unlink(temporary);
      const entry = {at: new Date().toISOString(), name, file, size, captured: length,
        sha256: digest.digest('hex'),
        header: header.toString('hex')};
      (code.files ||= []).push(entry); await save();
      return entry;
    } finally { if (safeToClose) await s.close(child); }
  }
  throw Error('Unknown native file operation');
}

module.exports = {dispatch, pathRequest};
