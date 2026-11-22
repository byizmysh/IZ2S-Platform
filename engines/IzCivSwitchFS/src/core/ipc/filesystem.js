// Filesystem IPC helpers - commands for IFileSystem and GetEntryType packets.

// -- IPC commands (IFileSystem) ------------------------------------

// FreeSpace (cmd 11) - returns bytes free, or -1 on error.
function ipcFreeSpace(handle, path) {
    path = path || "/";
    const pathBuf = allocPage(0x1000);
    writeStr(pathBuf, path);
    const buf = allocPage(0x2000);
    prepPacket(buf, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(buf, 8, pathBuf, path.length + 1);
    write32(buf + 0x10n, 0x49434653n);
    write32(buf + 0x18n, 11n);
    sendIPC(handle, buf);
    const reply = cmifReply(buf);
    if (reply.result !== 0) return { error: reply.result, bytes: -1 };
    const lo = Number(read32(buf + BigInt(reply.data))) >>> 0;
    const hi = Number(read32(buf + BigInt(reply.data + 4))) >>> 0;
    return { error: 0, bytes: lo + hi * 0x100000000 };
}

// GetEntryType (cmd 7) - returns entry type (0=dir, 1=file), or -1 on error.
function ipcGetEntryType(handle, path) {
    const pathBuf = allocPage(0x1000);
    writeStr(pathBuf, path);
    const buf = allocPage(0x2000);
    prepPacket(buf, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(buf, 8, pathBuf, path.length + 1);
    write32(buf + 0x10n, 0x49434653n);
    write32(buf + 0x18n, 7n);
    sendIPC(handle, buf);
    const reply = cmifReply(buf);
    if (reply.result !== 0) return { error: reply.result, type: -1 };
    return { error: 0, type: Number(read32(buf + BigInt(reply.data))) >>> 0 };
}

function ipcGetEntryTypeChecked(handle, path) {
    const pathBuf = allocPage(0x1000);
    const buf = allocPage(0x2000);
    writeStr(pathBuf, path);
    prepPacket(buf, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(buf, 8, pathBuf, path.length + 1);
    write32(buf + 0x10n, 0x49434653n);
    write32(buf + 0x18n, 7n);
    const transport = Number(sendIPC(handle, buf)) >>> 0;
    if (transport !== 0) return {transport, result: null, type: -1};
    const reply = cmifReply(buf);
    return {transport, result: reply.result,
        type: reply.result === 0
            ? Number(read32(buf + BigInt(reply.data))) >>> 0
            : -1};
}

// OpenDirectory (cmd 9) + ReadDirectory (IDirectory cmd 0).
// Lists contents without opening files and returns {name, type, size} entries.
// mode: 1=dirs only, 2=files only, 3=both
// CloseHandle is attempted for the IDirectory here, before this function returns.
// onDirectoryClose, when supplied, receives (result, error) from that close.
function ipcListDirectory(handle, path, mode, onDirectoryClose) {
    const IFS_CMD_OPEN_DIRECTORY = 9;
    const IDIRECTORY_CMD_READ = 0;
    mode = mode || 3;

    // -- OpenDirectory: needs X descriptor (path) + inline u32 mode --
    // Response returns a moved handle for the IDirectory object.
    const pathBuf = allocPage(0x1000);
    writeStr(pathBuf, path);
    const buf = allocPage(0x2000);
    prepPacket(buf, (4 | (1 << 16)) >>> 0, 0x0C, []);
    writeXDesc(buf, 8, pathBuf, 0x301);
    write32(buf + 0x10n, 0x49434653n);  // SFCI magic
    write32(buf + 0x18n, BigInt(IFS_CMD_OPEN_DIRECTORY));
    // SFCI layout: magic(+0) version(+4) cmd(+8) token(+C) data(+10)
    // mode goes at SFCI+0x10 = buf+0x20
    write32(buf + 0x20n, BigInt(mode)); // mode (3 = dirs+files)
    sendIPC(handle, buf);
    const openReply = cmifReply(buf);
    if (openReply.result !== 0 || openReply.moves.length === 0)
        return { error: openReply.result || 0xFFFF,
            openResult: openReply.result, openOk: false,
            readResult: null, entries: [] };
    const dirHandle = openReply.moves[0];

    var result = null;
    try {
        // -- ReadDirectory (IDirectory cmd 0) --
        // Needs a B (receive) descriptor for the output entries.
        // Each entry is 0x310 bytes: name[0x301] + pad[3] + type(u8) + pad[3] + size(u64)
        var ENTRY_SIZE = 0x310;
        var MAX_ENTRIES = 64;
        var outSize = ENTRY_SIZE * MAX_ENTRIES;
        var outBuf = allocPage(outSize + 0x1000);

        // Zero the output buffer
        for (var i = 0n; i < BigInt(outSize); i += 8n) write64(outBuf + i, 0n);

        // Build the request with 1 B descriptor (bits 27:24 of word0)
        //   word0: type=4, 0 X, 0 A, 1 B = 4 | (1 << 24)
        //   After HIPC header: B descriptor (12 bytes) at +0x08
        //   Then raw data with SFCI
        var readBuf = allocPage(0x2000);
        for (var j = 0n; j < 0x200n; j += 8n) write64(readBuf + j, 0n);

        // HIPC header
        write32(readBuf, BigInt((4 | (1 << 24)) >>> 0));  // type=4, 1 B desc
        write32(readBuf + 4n, 8n);                         // raw_words = 8

        // B descriptor at +0x08 (12 bytes), matching the map-alias
        // descriptor layout used by the reference IPC encoder. IDirectory::Read
        // uses mode 0; the address high fields occupy bits 2..4 and 28..31.
        var addrLow = Number(outBuf & 0xFFFFFFFFn);
        var bDescriptorMode = 0;
        var bWord2 = (bDescriptorMode |
            (Number((outBuf >> 36n) & 0x7n) << 2) |
            (Number((BigInt(outSize) >> 32n) & 0xFn) << 24) |
            (Number((outBuf >> 32n) & 0xFn) << 28)) >>> 0;
        write32(readBuf + 0x08n, BigInt(outSize));
        write32(readBuf + 0x0Cn, BigInt(addrLow >>> 0));
        write32(readBuf + 0x10n, BigInt(bWord2));

        // Raw data: SFCI at 16-byte aligned position after B descriptor
        // B ends at +0x14. Raw data starts at +0x14.
        // Next 16-byte aligned offset: +0x20
        write32(readBuf + 0x20n, 0x49434653n);  // SFCI magic
        // cmd 0 is Read because this packet targets IDirectory. On
        // IFileSystem, cmd 0 would be CreateFile; that handle is not used here.
        write32(readBuf + 0x28n, BigInt(IDIRECTORY_CMD_READ));

        var readTransport = Number(sendIPC(dirHandle, readBuf)) >>> 0;
        var readReply = null;
        var readProtocolError = null;
        if (readTransport === 0) {
            readReply = cmifReply(readBuf);
            var sfcoMagic = Number(read32(readBuf + BigInt(readReply.at))) >>> 0;
            if (sfcoMagic !== 0x4F434653)
                readProtocolError = "response did not contain SFCO";
        }
        var entries = [];
        var entriesRead = null;
        if (readTransport === 0 && !readProtocolError && readReply.result === 0) {
            // IDirectory::Read returns a u64 number of entries in reply.data.
            var entriesReadRaw = read64(readBuf + BigInt(readReply.data));
            entriesRead = entriesReadRaw.toString();
            var count = entriesReadRaw > BigInt(MAX_ENTRIES)
                ? MAX_ENTRIES : Number(entriesReadRaw);
            for (var e = 0; e < count; e++) {
                var base = outBuf + BigInt(e * ENTRY_SIZE);
                var name = "";
                for (var c = 0; c < 0x301; c++) {
                    var ch = Number(read8(base + BigInt(c)));
                    if (ch === 0) break;
                    name += String.fromCharCode(ch);
                }
                var entryType = Number(read8(base + 0x304n));
                var sizeLo = Number(read32(base + 0x308n)) >>> 0;
                var sizeHi = Number(read32(base + 0x30Cn)) >>> 0;
                entries.push({ name: name, type: entryType, size: sizeLo + sizeHi * 0x100000000 });
            }
        }

        var readError;
        if (readTransport !== 0) {
            readError = readTransport;
        } else if (readProtocolError) {
            readError = 0xFFFFFFFF;
        } else {
            readError = readReply.result;
        }

        result = { error: readError, openResult: openReply.result,
            openOk: true, readTransport: readTransport,
            readProtocolError: readProtocolError,
            readResult: readReply && !readProtocolError ? readReply.result : null,
            entriesRead, truncated: entriesRead !== null &&
                BigInt(entriesRead) >= BigInt(MAX_ENTRIES), entries: entries };
        return result;
    } finally {
        // Cleanup: close the IDirectory handle (SVC close; no FS mutation).
        var closeResult = null;
        var closeError = null;
        try {
            closeResult = Number(closeHandle(dirHandle)) >>> 0;
            if (result) result.directoryCloseResult = closeResult;
        } catch (error) {
            closeError = error;
            if (result) result.directoryCloseError = error.message;
        }
        if (onDirectoryClose)
            onDirectoryClose(closeResult, closeError);
        if (closeError) throw closeError;
    }
}

// -- IFileSystem commands used by scratch tree and wave setup -----------

// CreateDirectory (cmd 2).
function ipcCreateDirectory(handle, path) {
    var pathBuf = allocPage(0x1000);
    var pkt = allocPage(0x2000);
    writeStr(pathBuf, path);
    prepPacket(pkt, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(pkt, 8, pathBuf, path.length + 1);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 2n);
    sendIPC(handle, pkt);
    return cmifReply(pkt).result;
}

// CreateFile (cmd 0). size in bytes; creates file and pre-allocates space.
function ipcCreateFile(handle, path, size) {
    var pathBuf = allocPage(0x1000);
    var pkt = allocPage(0x2000);
    writeStr(pathBuf, path);
    prepPacket(pkt, (4 | (1 << 16)) >>> 0, 0x0C, []);
    writeXDesc(pkt, 8, pathBuf, path.length + 1);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 0n);
    write32(pkt + 0x20n, 0n);
    write32(pkt + 0x24n, 0n);
    write64(pkt + 0x28n, BigInt(size || 0));
    sendIPC(handle, pkt);
    return cmifReply(pkt).result;
}

// OpenFile (cmd 8). mode: 1=read, 2=write, 3=read+write.
// Returns the moved IFile handle, or 0 on failure.
function ipcOpenFile(handle, path, mode) {
    var pathBuf = allocPage(0x1000);
    var pkt = allocPage(0x2000);
    writeStr(pathBuf, path);
    prepPacket(pkt, (4 | (1 << 16)) >>> 0, 9, []);
    writeXDesc(pkt, 8, pathBuf, path.length + 1);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 8n);
    write32(pkt + 0x20n, BigInt(mode));
    sendIPC(handle, pkt);
    var reply = cmifReply(pkt);
    return (reply.result === 0 && reply.moves.length) ? reply.moves[0] : 0;
}

// IFile::OperateRange (cmd 5) with operation=2 (invalidate cache).
// Opens the file, invalidates [0, size), closes it. Returns the SFCO result,
// or -1 if the file could not be opened.
function ipcFileInvalidate(fsHandle, path, size) {
    var fileHandle = ipcOpenFile(fsHandle, path, 1);
    if (!fileHandle) return -1;
    var pkt = allocPage(0x2000);
    prepPacket(pkt, 4, 0x0C, []);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 5n);                    // cmd 5 = OperateRange
    write32(pkt + 0x20n, 2n);                    // operation = 2 (invalidate)
    write64(pkt + 0x28n, 0n);                    // offset = 0
    write64(pkt + 0x30n, BigInt(size));           // size
    sendIPC(fileHandle, pkt);
    var result = cmifReply(pkt).result;
    closeHandle(fileHandle);
    return result;
}

function ipcCreateDirectoryChecked(handle, path) {
    var pathBuf = allocPage(0x1000);
    var pkt = allocPage(0x2000);
    writeStr(pathBuf, path);
    prepPacket(pkt, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(pkt, 8, pathBuf, path.length + 1);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 2n);
    var transport = Number(sendIPC(handle, pkt)) >>> 0;
    return {transport, result: transport === 0 ? cmifReply(pkt).result : null};
}

function ipcCreateFileChecked(handle, path, size) {
    var pathBuf = allocPage(0x1000);
    var pkt = allocPage(0x2000);
    writeStr(pathBuf, path);
    prepPacket(pkt, (4 | (1 << 16)) >>> 0, 0x0C, []);
    writeXDesc(pkt, 8, pathBuf, path.length + 1);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 0n);
    write32(pkt + 0x20n, 0n);
    write32(pkt + 0x24n, 0n);
    write64(pkt + 0x28n, BigInt(size || 0));
    var transport = Number(sendIPC(handle, pkt)) >>> 0;
    return {transport, result: transport === 0 ? cmifReply(pkt).result : null};
}

function ipcDeleteFileChecked(handle, path) {
    var pathBuf = allocPage(0x1000);
    var pkt = allocPage(0x2000);
    writeStr(pathBuf, path);
    prepPacket(pkt, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(pkt, 8, pathBuf, path.length + 1);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 1n);
    var transport = Number(sendIPC(handle, pkt)) >>> 0;
    return {transport, result: transport === 0 ? cmifReply(pkt).result : null};
}

function ipcRenameFileChecked(handle, oldPath, newPath) {
    var oldPathBuf = allocPage(0x1000);
    var newPathBuf = allocPage(0x1000);
    var pkt = allocPage(0x2000);
    writeStr(oldPathBuf, oldPath);
    writeStr(newPathBuf, newPath);
    prepPacket(pkt, (4 | (2 << 16)) >>> 0, 8, []);
    writeXDesc(pkt, 8, oldPathBuf, oldPath.length + 1, 0);
    writeXDesc(pkt, 16, newPathBuf, newPath.length + 1, 1);
    write32(pkt + 0x20n, 0x49434653n);
    write32(pkt + 0x28n, 5n);
    var transport = Number(sendIPC(handle, pkt)) >>> 0;
    return {transport, result: transport === 0 ? cmifReply(pkt).result : null};
}

function ipcGetFileSizeChecked(fileHandle) {
    var pkt = allocPage(0x2000);
    prepPacket(pkt, 4, 8, [[0x10, 0x49434653], [0x18, 4]]);
    var transport = Number(sendIPC(fileHandle, pkt)) >>> 0;
    if (transport !== 0) return {transport, result: null, size: null};
    var reply = cmifReply(pkt);
    return {transport, result: reply.result,
        size: reply.result === 0 ? read64(pkt + BigInt(reply.data)) : null};
}

// Read one small file through IFile::Read. The caller supplies a strict size
// limit so malformed save data cannot trigger a large allocation.
function ipcReadFileChecked(handle, path, maxBytes) {
    var fileHandle = ipcOpenFile(handle, path, 1);
    if (!fileHandle)
        return {ok: false, stage: "open", transport: null, result: null};

    try {
        var sizeReply = ipcGetFileSizeChecked(fileHandle);
        if (sizeReply.transport !== 0 || sizeReply.result !== 0)
            return {ok: false, stage: "size", transport: sizeReply.transport,
                result: sizeReply.result};
        if (sizeReply.size < 1n || sizeReply.size > BigInt(maxBytes) ||
            sizeReply.size > BigInt(Number.MAX_SAFE_INTEGER)) {
            return {ok: false, stage: "size-range", transport: 0,
                result: null};
        }

        var size = Number(sizeReply.size);
        var outBuf = allocPage(size);
        var pkt = allocPage(0x2000);
        prepPacket(pkt, (4 | (1 << 24)) >>> 0, 0x0E, []);
        var addrLow = Number(outBuf & 0xFFFFFFFFn);
        // IFile::Read uses a map-alias output buffer that allows non-secure
        // transfer; bit 0 is that descriptor mode (as in ipcStorageRead).
        var bWord2 = 1 |
            (Number((outBuf >> 36n) & 0x7n) << 2) |
            (Number((BigInt(size) >> 32n) & 0xFn) << 24) |
            (Number((outBuf >> 32n) & 0xFn) << 28);
        write32(pkt + 0x08n, BigInt(size));
        write32(pkt + 0x0Cn, BigInt(addrLow >>> 0));
        write32(pkt + 0x10n, BigInt(bWord2 >>> 0));
        write32(pkt + 0x20n, 0x49434653n);
        write32(pkt + 0x28n, 0n);
        write32(pkt + 0x30n, 0n); // ReadOption::None.
        write64(pkt + 0x38n, 0n);
        write64(pkt + 0x40n, BigInt(size));

        var transport = Number(sendIPC(fileHandle, pkt)) >>> 0;
        if (transport !== 0)
            return {ok: false, stage: "read", transport, result: null};
        var reply = cmifReply(pkt);
        if (reply.result !== 0)
            return {ok: false, stage: "read", transport, result: reply.result};
        var bytesRead = read64(pkt + BigInt(reply.data));
        if (bytesRead !== BigInt(size))
            return {ok: false, stage: "read-size", transport, result: null};

        var bytes = new Array(size);
        for (var i = 0; i < size; i++)
            bytes[i] = Number(read8(outBuf + BigInt(i))) & 0xFF;
        return {ok: true, bytes};
    } finally {
        closeHandle(fileHandle);
    }
}

// Create and write a new file. The caller removes a stale path before calling.
function ipcWriteFileChecked(handle, path, bytes) {
    var createReply = ipcCreateFileChecked(handle, path, bytes.length);
    if (createReply.transport !== 0 || createReply.result !== 0)
        return {ok: false, stage: "create", ...createReply};

    var fileHandle = ipcOpenFile(handle, path, 2);
    if (!fileHandle)
        return {ok: false, stage: "open", transport: null, result: null};

    try {
        var inBuf = allocPage(bytes.length);
        write_bytes(inBuf, bytes);
        var pkt = allocPage(0x2000);
        prepPacket(pkt, (4 | (1 << 20)) >>> 0, 0x0E, []);
        var addrLow = Number(inBuf & 0xFFFFFFFFn);
        var bWord2 = 1 |
            (Number((inBuf >> 36n) & 0x7n) << 2) |
            (Number((BigInt(bytes.length) >> 32n) & 0xFn) << 24) |
            (Number((inBuf >> 32n) & 0xFn) << 28);
        write32(pkt + 0x08n, BigInt(bytes.length));
        write32(pkt + 0x0Cn, BigInt(addrLow >>> 0));
        write32(pkt + 0x10n, BigInt(bWord2 >>> 0));
        write32(pkt + 0x20n, 0x49434653n);
        write32(pkt + 0x28n, 1n);
        write32(pkt + 0x30n, 1n); // WriteOption::Flush.
        write64(pkt + 0x38n, 0n);
        write64(pkt + 0x40n, BigInt(bytes.length));
        var transport = Number(sendIPC(fileHandle, pkt)) >>> 0;
        if (transport !== 0)
            return {ok: false, stage: "write", transport, result: null};
        var reply = cmifReply(pkt);
        return {ok: reply.result === 0, stage: "write", transport,
            result: reply.result};
    } finally {
        closeHandle(fileHandle);
    }
}

// DeleteDirectoryRecursively (cmd 4).
function ipcDeleteDirectoryRecursivelyChecked(handle, path) {
    var pathBuf = allocPage(0x1000);
    var pkt = allocPage(0x2000);
    writeStr(pathBuf, path);
    prepPacket(pkt, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(pkt, 8, pathBuf, path.length + 1);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 4n);
    var transport = Number(sendIPC(handle, pkt)) >>> 0;
    if (transport !== 0) return {transport: transport, result: null};
    return {transport: transport, result: cmifReply(pkt).result};
}

function ipcDeleteDirectoryRecursively(handle, path) {
    var checked = ipcDeleteDirectoryRecursivelyChecked(handle, path);
    return checked.transport === 0 ? checked.result : 0xFFFFFFFF;
}

// Commit (cmd 10). Flushes pending writes.
function ipcCommitChecked(handle) {
    var pkt = allocPage(0x2000);
    prepPacket(pkt, 4, 8, [[0x10, 0x49434653], [0x18, 10]]);
    var transport = Number(sendIPC(handle, pkt)) >>> 0;
    if (transport !== 0) return {transport: transport, result: null};
    return {transport: transport, result: cmifReply(pkt).result};
}

function ipcCommit(handle) {
    var checked = ipcCommitChecked(handle);
    return checked.transport === 0 ? checked.result : 0xFFFFFFFF;
}
