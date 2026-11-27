'use strict';
// Shared by the save builder, console preflight and PC session. Bump the policy
// when the boot ABI changes, not when rebuilding a compatible launcher.
const BOOT_POLICY = 'iz2s.sd-flex.v1';
const FIXED_FILES = {bootstrap: 'bootstrap.bin', ro: 'ro/main', core: 'core/main'};
const HEADER_BYTES = {game: 256, launcher: 512, resources: 80};
const LIMITS = {game: 512 * 1024 * 1024, launcher: 32 * 1024 * 1024, resources: 1024 * 1024 * 1024};

function compatibilityContract(records) {
    const anchors = {};
    for (const [name, path] of Object.entries(FIXED_FILES)) {
        const found = records.filter(record => record.path === path);
        const record = found[0];
        if (found.length !== 1 || !Number.isSafeInteger(record.bytes) || record.bytes <= 0 ||
            !/^[a-f0-9]{64}$/.test(record.sha256)) throw Error('Invalid boot anchor: ' + path);
        anchors[name] = {path, bytes: record.bytes, sha256: record.sha256};
    }
    return {policy: BOOT_POLICY, anchors};
}

// Header checks are bounded structural checks, not substitutes for package SHA256.
// The PC verifies complete files. Native IZA loading retains its section CRCs.
function validateMutable(name, header, fileSize) {
    const fail = message => { throw Error(message); };
    const size = Number(fileSize), minimum = HEADER_BYTES[name];
    if (!minimum || !Number.isSafeInteger(size) || size < minimum || size > LIMITS[name])
        fail('invalid file size');
    if (header.length !== minimum) fail('truncated header');
    const u32 = at => (header[at] | header[at + 1] << 8 | header[at + 2] << 16 | header[at + 3] << 24) >>> 0;
    const u64 = at => {
        const n = u32(at) + u32(at + 4) * 4294967296;
        if (!Number.isSafeInteger(n)) fail('header offset exceeds safe range');
        return n;
    };
    const text = (at, length) => String.fromCharCode(...header.slice(at, at + length));
    if (name === 'game') {
        if (text(0, 4) !== 'NSO0' || u32(4) !== 0 || u32(8) !== 0) fail('unsupported NSO header');
        const flags = u32(12);
        if ((flags & ~0x3f) || (flags & 0x38) !== 0x38) fail('NSO segment hashes must be enabled');
        let fileEnd = 256, memoryEnd = 0;
        for (let i = 0; i < 3; i++) {
            const at = 16 + 16 * i, offset = u32(at), address = u32(at + 4), unpacked = u32(at + 8);
            const stored = (flags & (1 << i)) ? u32(96 + 4 * i) : unpacked;
            if (!stored || !unpacked || offset < fileEnd || stored > size - offset ||
                address < memoryEnd || address % 4096 || unpacked > LIMITS.game - address)
                fail('invalid NSO segment bounds');
            if (!header.slice(160 + 32 * i, 192 + 32 * i).some(byte => byte !== 0))
                fail('missing NSO segment hash');
            fileEnd = offset + stored;
            memoryEnd = address + unpacked;
        }
        if (fileEnd !== size || u32(60) > LIMITS.game - memoryEnd) fail('invalid NSO image extent');
    } else if (name === 'launcher') {
        if (text(0, 8) !== 'IZSDAPP1' || ![1, 2].includes(u32(8)) || u32(12) !== 512 ||
            u32(16) !== size || u32(20) !== 0 || text(24, 48) !== 'izlauncher' + '\0'.repeat(38))
            fail('unsupported IZ2S menu header');
        let end = 512;
        for (let i = 0; i < 12; i++) {
            const at = 176 + 16 * i, offset = u32(at), bytes = u32(at + 4);
            if (u32(at + 12) || (!bytes && (offset || u32(at + 8)))) fail('invalid IZA section');
            if (!bytes) continue;
            if (offset < end || offset % 4 || bytes > size - offset) fail('invalid IZA section bounds');
            end = offset + bytes;
        }
        if (end !== size || u32(180) !== 1024) fail('invalid IZA image extent');
    } else {
        if (u64(0) !== 80) fail('unsupported ROMFS header');
        const ranges = [];
        for (let at = 8; at < 72; at += 16) {
            const offset = u64(at), bytes = u64(at + 8);
            if (offset < 80 || !bytes || bytes > size - offset) fail('invalid ROMFS table bounds');
            ranges.push([offset, offset + bytes]);
        }
        ranges.sort((a, b) => a[0] - b[0]);
        for (let i = 1; i < ranges.length; i++)
            if (ranges[i][0] < ranges[i - 1][1]) fail('overlapping ROMFS tables');
        const data = u64(72);
        if (data < 80 || data > size || ranges.some(([start, end]) => data >= start && data < end))
            fail('invalid ROMFS data offset');
    }
    return true;
}

module.exports = {BOOT_POLICY, FIXED_FILES, HEADER_BYTES, LIMITS, compatibilityContract, validateMutable};
