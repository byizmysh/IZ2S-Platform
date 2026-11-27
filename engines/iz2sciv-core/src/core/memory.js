// Memory helpers — allocation tracking and IPC buffer access.

var activeAllocationTracker = null;

function beginAllocationTracking() {
    if (activeAllocationTracker !== null) return null;
    activeAllocationTracker = [];
    return activeAllocationTracker;
}

function endAllocationTracking(tracker) {
    if (activeAllocationTracker !== tracker) return false;
    activeAllocationTracker = null;
    return true;
}

// Track raw game-heap allocations for a payload that owns a bounded
// allocation scope. The tracked pointer is the original malloc result, not
// the page-aligned address returned by allocPage.
function allocRawTracked(bytes) {
    const raw = malloc(bytes);
    if (raw && activeAllocationTracker !== null)
        activeAllocationTracker.push(raw);
    return raw;
}

function freeTrackedAllocations(allocations) {
    var errors = [];
    var callsCompleted = 0;
    for (var i = allocations.length - 1; i >= 0; i--) {
        try {
            free(allocations[i]);
            callsCompleted++;
        } catch (error) {
            errors.push("allocation-" + i + ":" + String(error));
        }
    }
    // The target's C free() is void; this reports normal ROP-call completion,
    // not an allocator-side proof that the block was reclaimed.
    return {ok: errors.length === 0, attempted: allocations.length,
        callsCompleted: callsCompleted, errors: errors};
}

// Page-aligned allocation (required by kernel for IPC buffers)
function allocPage(bytes) {
    const raw = allocRawTracked(bytes + 0x2000);
    return (raw + 0xFFFn) / 0x1000n * 0x1000n;
}

// Write a null-terminated string into a buffer
function writeStr(buf, str) {
    for (let i = 0; i < str.length; i++)
        write8(buf + BigInt(i), BigInt(str.charCodeAt(i)));
    write8(buf + BigInt(str.length), 0n);
}

// Used by the kaihen chain payload (src/kaihen/state.mjs destructures it
// from the core API; SD staging reads sink chunks through it). Upstream
// removed it as dead code because nothing in their tree references it —
// keep it for the chain build.
function read_bytes(addr, len) {
    const out = [];
    for (let i = 0; i < len; i++) {
        out.push(Number(read8(addr + BigInt(i))));
    }
    return out;
}

function write_bytes(addr, bytes) {
    for (let i = 0; i < bytes.length; i++) {
        write8(addr + BigInt(i), BigInt(bytes[i]));
    }
}
