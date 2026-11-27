import runtime from './core/runtime.mjs';
import {openSdCard, sdGetEntryType, sdReadFile, sdOpenFile, sdFileSize, sdReadFileChunk}
    from './kaihen/sdcard.mjs';
import {sha256} from './kaihen/sha256.mjs';
import policy from './runtime-policy.cjs';

export function verifySdRuntime(context, manifest) {
    const sd = openSdCard(context), status = {};
    for (const [name, item] of Object.entries(manifest)) {
        const entry = sdGetEntryType(context, sd, item.path);
        if (!(entry.transport === 0 && entry.result === 0))
            throw Error(item.path + ' is missing or inaccessible; copy the IZ2S SD package');
        const handle = sdOpenFile(context, sd, item.path, 1);
        try {
            const size = sdFileSize(context, handle);
            if (Object.prototype.hasOwnProperty.call(policy.FIXED_FILES, name)) {
                if (size !== BigInt(item.size))
                    throw Error('boot core size mismatch; install its matching launch save');
                status[name] = 'pinned';
            } else {
                const length = policy.HEADER_BYTES[name];
                if (!length || size < BigInt(length) || size > BigInt(policy.LIMITS[name]))
                    throw Error('invalid file size');
                const header = sdReadFileChunk(context, handle, 0, length);
                policy.validateMutable(name, header, size);
                status[name] = 'format';
            }
        } catch (error) {
            throw Error(item.path + ': ' + error.message);
        } finally {
            runtime.closeHandle(handle);
        }
    }
    for (const name of Object.keys(policy.FIXED_FILES)) {
        const item = manifest[name];
        const file = sdReadFile(context, sd, item.path, 0x100000);
        if (sha256(file.bytes) !== item.sha256)
            throw Error(item.path + ' differs from the boot core SHA-256; install its matching launch save');
        status[name] = 'sha256';
    }
    return status;
}
