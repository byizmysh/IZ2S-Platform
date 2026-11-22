// Byte-array analysis helpers for the payload bundle.
// This file has no memory, IPC, or logging side effects.
// API: AnalysisBytes

var AnalysisBytes = (function() {
    function checkRange(bytes, offset, count) {
        if (!bytes || typeof bytes.length !== "number" ||
            !Number.isSafeInteger(offset) || !Number.isSafeInteger(count) ||
            offset < 0 || count < 0 || offset + count > bytes.length) {
            throw new RangeError("byte range is outside the supplied buffer");
        }
    }

    function readU32LE(bytes, offset) {
        checkRange(bytes, offset, 4);
        return (Number(bytes[offset]) | (Number(bytes[offset + 1]) << 8) |
            (Number(bytes[offset + 2]) << 16) |
            (Number(bytes[offset + 3]) << 24)) >>> 0;
    }

    function writeU32LE(bytes, offset, value) {
        checkRange(bytes, offset, 4);
        var word = Number(value) >>> 0;
        bytes[offset] = word & 0xFF;
        bytes[offset + 1] = (word >>> 8) & 0xFF;
        bytes[offset + 2] = (word >>> 16) & 0xFF;
        bytes[offset + 3] = (word >>> 24) & 0xFF;
    }

    function readU64LE(bytes, offset) {
        checkRange(bytes, offset, 8);
        var word = 0n;
        for (var i = 7; i >= 0; i--)
            word = (word << 8n) | BigInt(bytes[offset + i]);
        return word;
    }

    function writeU64LE(bytes, offset, value) {
        checkRange(bytes, offset, 8);
        var word = BigInt.asUintN(64, BigInt(value));
        for (var i = 0; i < 8; i++) {
            bytes[offset + i] = Number(word & 0xFFn);
            word >>= 8n;
        }
    }

    function equal(left, right) {
        if (!left || !right || left.length !== right.length) return false;
        for (var i = 0; i < left.length; i++)
            if (Number(left[i]) !== Number(right[i])) return false;
        return true;
    }

    function toHex(bytes, start, end) {
        start = start === undefined ? 0 : start;
        end = end === undefined ? bytes.length : end;
        checkRange(bytes, start, end - start);
        var out = "";
        for (var i = start; i < end; i++)
            out += ("0" + Number(bytes[i]).toString(16)).slice(-2);
        return out;
    }

    // Advance a small UTF-8 validator state: [continuationCount, low, high].
    function utf8Step(state, byte) {
        var remaining = state[0], low = state[1], high = state[2];
        if (remaining) {
            if (byte < low || byte > high) return null;
            return remaining > 1 ? [remaining - 1, 0x80, 0xBF] : [0, 0, 0];
        }
        if (byte >= 1 && byte <= 0x7F) return [0, 0, 0];
        if (byte >= 0xC2 && byte <= 0xDF) return [1, 0x80, 0xBF];
        if (byte === 0xE0) return [2, 0xA0, 0xBF];
        if (byte >= 0xE1 && byte <= 0xEF)
            return [2, 0x80, byte === 0xED ? 0x9F : 0xBF];
        if (byte === 0xF0) return [3, 0x90, 0xBF];
        if (byte >= 0xF1 && byte <= 0xF4)
            return [3, 0x80, byte === 0xF4 ? 0x8F : 0xBF];
        return null;
    }

    return {
        readU32LE: readU32LE,
        writeU32LE: writeU32LE,
        readU64LE: readU64LE,
        writeU64LE: writeU64LE,
        equal: equal,
        toHex: toHex,
        utf8Step: utf8Step
    };
})();
