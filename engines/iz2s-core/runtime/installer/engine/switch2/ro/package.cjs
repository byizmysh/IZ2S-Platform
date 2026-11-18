// Open a PFS0 application package through the retained FS root. Loader sees
// its entries as root-relative CodeFS files; no files are extracted to /tmp.
const fs = require('node:fs/promises');
const crypto = require('node:crypto');
const {pathRequest} = require('./files.cjs');
const filesystem = require('../app/filesystem.cjs');

const check = (condition, message) => { if (!condition) throw Error(message); };
const packagePath = kind => `@Sdcard:/iz2s/core/bin/${kind}/exefs.nsp`;
const filePath = kind => `/iz2s/core/bin/${kind}/exefs.nsp`;

function packageInput() {
  const input = Buffer.alloc(16);
  input.writeUInt32LE(7, 4); // FsFileSystemType_ApplicationPackage
  // Atmosphère's ncm::InvalidProgramId is a zero-initialized ProgramId.
  // Captured FS rejects all-ones here before it reaches the package path.
  input.writeBigUInt64LE(0n, 8);
  return input;
}

async function pathCall(context, domain, handle, command, name, input = Buffer.alloc(0)) {
  const {s} = context;
  const path = Buffer.alloc(0x301);
  path.write(name);
  await s.write(s.base + 0x2000, path);
  return domain.send(s, `Package ${command} ${name}`, handle,
    pathRequest(command, input, s.base + 0x2000));
}

async function openPackage(context, domain, root, kind) {
  const reply = await pathCall(context, domain, root, 10, packagePath(kind), packageInput());
  if (reply.result) return {result: reply.canonicalResult};
  const handle = domain.oneHandle(reply);
  const entries = [];
  try {
    for (const name of ['main', 'main.npdm']) {
      const opened = await filesystem.openFile(context.s, handle, `/${name}`);
      check(!opened.result, `Package /${name} did not open: ${opened.result}`);
      try {
        const size = Number(await filesystem.fileSize(context.s, opened.handle));
        check(size > 0 && size <= 0x40000, `Unexpected package /${name} size`);
        const bytes = await filesystem.readFile(context.s, opened.handle, 0, size);
        check(bytes.length === size, `Short package /${name} read`);
        entries.push({name, size, sha256: crypto.createHash('sha256').update(bytes).digest('hex')});
      } finally {
        await context.s.close(opened.handle);
      }
    }
    const converted = await domain.send(context.s, 'Package ConvertToDomain', handle,
      domain.wire.control(0));
    check(!converted.result, `Package domain conversion failed: ${converted.canonicalResult}`);
    return {result: 0, handle, root: converted.data.readUInt32LE(), entries};
  } catch (error) {
    await context.s.close(handle);
    throw error;
  }
}

async function stagePackage(context, domain, root, kind, source) {
  const {s} = context;
  const bytes = await fs.readFile(source);
  check(bytes.subarray(0, 4).toString() === 'PFS0', 'Source is not PFS0');
  const opened = await s.ipc('Open SD filesystem', root, 18);
  check(!opened.result, `SD filesystem open failed: ${opened.canonicalResult}`);
  const sd = domain.oneHandle(opened);
  try {
    for (const directory of ['/iz2s', '/iz2s/core', '/iz2s/core/bin', `/iz2s/core/bin/${kind}`]) {
      const exists = await filesystem.pathMethod(s, sd, 7, directory);
      if (exists.result) {
        const created = await pathCall(context, domain, sd, 2, directory);
        check(!created.result, `Create directory ${directory} failed: ${created.canonicalResult}`);
      }
    }
    const target = filePath(kind);
    const exists = await filesystem.pathMethod(s, sd, 7, target);
    if (!exists.result) {
      const removed = await pathCall(context, domain, sd, 1, target);
      check(!removed.result, `Remove ${target} failed: ${removed.canonicalResult}`);
    }
    const createInput = Buffer.alloc(16);
    createInput.writeBigUInt64LE(BigInt(bytes.length), 8);
    const created = await pathCall(context, domain, sd, 0, target, createInput);
    check(!created.result, `Create ${target} failed: ${created.canonicalResult}`);
    const mode = Buffer.alloc(4);
    mode.writeUInt32LE(3);
    const file = domain.oneHandle(await pathCall(context, domain, sd, 8, target, mode));
    try {
      for (let offset = 0; offset < bytes.length; offset += 0x10000) {
        const part = bytes.subarray(offset, offset + 0x10000);
        const input = Buffer.alloc(24);
        input.writeUInt32LE(1);
        input.writeBigUInt64LE(BigInt(offset), 8);
        input.writeBigUInt64LE(BigInt(part.length), 16);
        await s.write(s.base + 0x4000, part);
        const wrote = await s.ipc('Write application package', file, 1, input,
          {send: [{address: s.base + 0x4000, size: part.length, mode: 1}]});
        check(!wrote.result, `Package write failed: ${wrote.canonicalResult}`);
      }
    } finally {
      await s.close(file);
    }
    const committed = await s.ipc('Commit SD package', sd, 10);
    check(!committed.result, `Package commit failed: ${committed.canonicalResult}`);
    return {path: packagePath(kind), size: bytes.length,
      sha256: crypto.createHash('sha256').update(bytes).digest('hex')};
  } finally {
    await s.close(sd);
  }
}

async function stageFile(context, domain, root, target, source, skipIfPresent) {
  const {s} = context;
  check(target.startsWith('/') && !target.includes('..'), 'Target must be an SD-root path');
  if (skipIfPresent) {
    const foldrift = '/iz2s/modules/foldrift/launcher.iza';
    check(skipIfPresent === foldrift && [foldrift, foldrift + '.sig'].includes(target),
      'Only the bundled Foldrift files may use the install-if-missing guard');
  }
  const bytes = await fs.readFile(source);
  const opened = await s.ipc('Open SD filesystem', root, 18);
  check(!opened.result, `SD filesystem open failed: ${opened.canonicalResult}`);
  const sd = domain.oneHandle(opened);
  let createdFile = false;
  try {
    if (skipIfPresent) {
      const installed = await filesystem.pathMethod(s, sd, 7, skipIfPresent);
      if (!installed.result) return {path: `@Sdcard:${target}`, skipped: true};
      check(installed.canonicalResult === 0x202,
        `Foldrift installation status is unavailable: ${installed.canonicalResult}`);
    }
    const segments = target.split('/').slice(1, -1);
    let directory = '';
    for (const segment of segments) {
      directory += '/' + segment;
      const present = await filesystem.pathMethod(s, sd, 7, directory);
      if (present.result) {
        const created = await pathCall(context, domain, sd, 2, directory);
        check(!created.result, `Create ${directory} failed: ${created.canonicalResult}`);
      }
    }
    const exists = await filesystem.pathMethod(s, sd, 7, target);
    if (skipIfPresent) {
      if (!exists.result && target === skipIfPresent) return {path: `@Sdcard:${target}`, skipped: true};
      check(!exists.result || exists.canonicalResult === 0x202,
        `Cannot check ${target}: ${exists.canonicalResult}`);
    }
    if (!exists.result) {
      const removed = await pathCall(context, domain, sd, 1, target);
      check(!removed.result, `Delete ${target} failed: ${removed.canonicalResult}`);
    }
    const createInput = Buffer.alloc(16);
    createInput.writeBigUInt64LE(BigInt(bytes.length), 8);
    const created = await pathCall(context, domain, sd, 0, target, createInput);
    check(!created.result, `Create ${target} failed: ${created.canonicalResult}`);
    createdFile = true;
    const mode = Buffer.alloc(4);
    mode.writeUInt32LE(3);
    const file = domain.oneHandle(await pathCall(context, domain, sd, 8, target, mode));
    try {
      for (let offset = 0; offset < bytes.length; offset += 0x10000) {
        const part = bytes.subarray(offset, offset + 0x10000);
        const input = Buffer.alloc(24);
        input.writeUInt32LE(1);
        input.writeBigUInt64LE(BigInt(offset), 8);
        input.writeBigUInt64LE(BigInt(part.length), 16);
        await s.write(s.base + 0x4000, part);
        const wrote = await s.ipc(`Write ${target}`, file, 1, input,
          {send: [{address: s.base + 0x4000, size: part.length, mode: 1}]});
        check(!wrote.result, `Write ${target} failed: ${wrote.canonicalResult}`);
      }
    } finally {
      await s.close(file);
    }
    const committed = await s.ipc(`Commit ${target}`, sd, 10);
    check(!committed.result, `Commit ${target} failed: ${committed.canonicalResult}`);
    return {path: `@Sdcard:${target}`, size: bytes.length,
      sha256: crypto.createHash('sha256').update(bytes).digest('hex')};
  } catch (error) {
    if (skipIfPresent && createdFile) {
      try {
        const removed = await pathCall(context, domain, sd, 1, target);
        check(!removed.result, `Remove incomplete ${target} failed: ${removed.canonicalResult}`);
        const committed = await s.ipc(`Commit removal of ${target}`, sd, 10);
        check(!committed.result, `Commit removal of ${target} failed: ${committed.canonicalResult}`);
      } catch (cleanupError) {
        throw new AggregateError([error, cleanupError], error.message + '; ' + cleanupError.message);
      }
    }
    throw error;
  } finally {
    await s.close(sd);
  }
}

async function deleteFile(context, domain, root, target) {
  const {s} = context;
  check(target.startsWith('/') && !target.includes('..'), 'Target must be an SD-root path');
  const opened = await s.ipc('Open SD filesystem', root, 18);
  check(!opened.result, `SD filesystem open failed: ${opened.canonicalResult}`);
  const sd = domain.oneHandle(opened);
  try {
    const exists = await filesystem.pathMethod(s, sd, 7, target);
    if (exists.result) return {path: `@Sdcard:${target}`, removed: false};
    const removed = await pathCall(context, domain, sd, 1, target);
    check(!removed.result, `Delete ${target} failed: ${removed.canonicalResult}`);
    const committed = await s.ipc(`Commit ${target}`, sd, 10);
    check(!committed.result, `Commit ${target} failed: ${committed.canonicalResult}`);
    return {path: `@Sdcard:${target}`, removed: true};
  } finally {
    await s.close(sd);
  }
}

async function dispatch(context, request, state, domain) {
  const kind = request.kind;
  const root = Number(request.root);
  check(context.s.handles.has(root), 'FS root session is unavailable');
  if (request.command === 'root-native-file-stage') {
    return stageFile(context, domain, root, request.target, request.source, request.skipIfPresent);
  }
  if (request.command === 'root-native-file-delete') {
    return deleteFile(context, domain, root, request.target);
  }
  check(['ro', 'core', 'game'].includes(kind), 'Unknown application package');
  if (request.command === 'root-native-package-stage') {
    return stagePackage(context, domain, root, kind, request.source);
  }
  if (request.command === 'root-native-package-open') {
    const result = await openPackage(context, domain, root, kind);
    if (!result.result) {
      (state.packages ||= []).push({kind, ...result});
      if (request.selectDonor) {
        const previous = state.donor;
        state.donor = {handle: result.handle, root: result.root, mount: 'package',
          kind, ready: true, files: result.entries};
        if (previous?.handle && context.s.handles.has(previous.handle))
          await context.s.close(previous.handle);
      }
    }
    await context.save();
    return result;
  }
  throw Error('Unknown package operation');
}

module.exports = {dispatch, packagePath};
