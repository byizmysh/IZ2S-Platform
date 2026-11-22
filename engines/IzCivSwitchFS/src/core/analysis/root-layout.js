// Pure parsers and candidate calculations for fsp-srv allocator/root layouts.
// Depends on: core/analysis/bytes.js (AnalysisBytes).
// API: RootLayout. Capture reads and IPC remain in the calling payload.

var RootLayout = (function() {
    var DEFAULTS = {
        controlSize: 0x4000,
        poolSize: 0x2000000,
        poolAlignment: 0x200000,
        maxPoolCandidates: 4,
        minimumPointer: 0x100000000n,
        maximumPointerExclusive: 0x8000000000n,
        externalLinkMinimumDistance: 0x8000000n,
        minimumExtent: 0x10000n,
        maximumExtent: 0x10000000n,
        extentAlignment: 0x1000n,
        linkAlignment: 8n,
        metadataBlockSize: 0x4000,
        metadataPageSize: 0x1000
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

    function rangeContains(start, length, address, size) {
        return Number.isSafeInteger(start) && Number.isSafeInteger(length) &&
            Number.isSafeInteger(address) && Number.isSafeInteger(size) &&
            length > 0 && size > 0 && address >= start &&
            address + size <= start + length;
    }

    function isCanonicalPointer(value, options) {
        var opts = settings(options);
        return typeof value === "bigint" &&
            value >= opts.minimumPointer &&
            value < opts.maximumPointerExclusive &&
            value % opts.linkAlignment === 0n;
    }

    function derivePoolBounds(links, seed, options) {
        var opts = settings(options);
        var poolSize = opts.poolSize;
        var alignment = opts.poolAlignment;
        if (!links || !links.length || !Number.isSafeInteger(seed) ||
            seed % opts.controlSize) {
            return {candidates: [], size: poolSize, alignment: alignment};
        }

        var candidates = [];
        var firstStart = Math.ceil((seed + opts.controlSize - poolSize) /
            alignment) * alignment;
        for (var start = firstStart; start <= seed; start += alignment) {
            var inside = [];
            for (var i = 0; i < links.length; i++) {
                var link = links[i];
                if (link.source >= start && link.source + opts.controlSize <=
                    start + poolSize) inside.push(link);
            }
            if (!inside.length) continue;
            var allInside = true;
            for (var j = 0; j < inside.length; j++) {
                if (inside[j].value < start ||
                    inside[j].value + opts.controlSize > start + poolSize) {
                    allInside = false;
                    break;
                }
            }
            if (allInside) candidates.push(start);
        }
        return {first: candidates.length ? candidates[0] : null,
            last: candidates.length ? candidates[candidates.length - 1] : null,
            candidates: candidates, size: poolSize, alignment: alignment,
            observedLinks: links.length};
    }

    function choosePoolRefinementStart(bounds, links, captures, bias, cycle,
        attemptIndex, options) {
        var opts = settings(options);
        if (!bounds || !bounds.candidates || !links || !captures ||
            !Number.isSafeInteger(cycle) || cycle <= 0 ||
            bounds.candidates.length <= opts.maxPoolCandidates)
            return null;

        function covered(start) {
            for (var i = 0; i < captures.length; i++) {
                var item = captures[i];
                if (item.address <= start && start + cycle <=
                    item.address + item.length) return true;
            }
            return false;
        }

        function inside(start) {
            return start >= bounds.last &&
                start + cycle <= bounds.first + bounds.size;
        }

        var targets = [];
        var targetSeen = Object.create(null);
        for (var li = 0; li < links.length; li++) {
            var target = links[li].value;
            var key = String(target);
            if (!targetSeen[key]) {
                targetSeen[key] = true;
                targets.push(target);
            }
        }
        targets.sort(function(a, b) {
            return attemptIndex % 2 ? b - a : a - b;
        });

        for (var ti = 0; ti < targets.length; ti++) {
            var targetStart = bias + Math.floor((targets[ti] - bias) / cycle) *
                cycle;
            if (inside(targetStart) && !covered(targetStart)) return targetStart;
        }

        var start = bias + Math.ceil((bounds.last - bias) / cycle) * cycle;
        for (; inside(start); start += cycle)
            if (!covered(start)) return start;
        return null;
    }

    function parseExternalLink(bytes, offset, address, options) {
        var opts = settings(options);
        if (offset < 0 || offset + 24 > bytes.length) return null;
        var next = AnalysisBytes.readU64LE(bytes, offset);
        var previous = AnalysisBytes.readU64LE(bytes, offset + 8);
        var extent = AnalysisBytes.readU64LE(bytes, offset + 16);
        var linkAddress = BigInt(address);
        var distance = next >= linkAddress
            ? next - linkAddress : linkAddress - next;
        if (next < opts.minimumPointer ||
            next >= opts.maximumPointerExclusive ||
            next % opts.linkAlignment !== 0n || next !== previous ||
            distance <= opts.externalLinkMinimumDistance ||
            extent < opts.minimumExtent || extent > opts.maximumExtent ||
            extent % opts.extentAlignment !== 0n) return null;
        return {next: next, previous: previous, extent: extent};
    }

    function metadataWindowFromNext(next, options) {
        var opts = settings(options);
        if (next < opts.minimumPointer ||
            next >= opts.maximumPointerExclusive) return null;
        var blockOffset = Number(next % BigInt(opts.metadataBlockSize));
        var length = blockOffset >= opts.metadataPageSize
            ? opts.metadataPageSize : opts.metadataBlockSize;
        var page = Number(next / BigInt(length) * BigInt(length));
        return {address: page, length: length,
            anchorOffset: Number(next - BigInt(page)),
            originalBlockOffset: blockOffset};
    }

    // Layout-specific deltas are required inputs so a caller-owned root rule
    // cannot silently become a Loader-target rule (or vice versa).
    function makeDomainCandidate(address, first, second, manager, options) {
        var opts = settings(options);
        if (!options || typeof options.domainVptrDelta !== "bigint" ||
            typeof options.rootVptrDelta !== "bigint") return null;
        if (second - first !== options.domainVptrDelta ||
            first < options.rootVptrDelta ||
            !isCanonicalPointer(first, opts) ||
            !isCanonicalPointer(second, opts) ||
            !isCanonicalPointer(manager, opts)) return null;
        var rootVptr = first - options.rootVptrDelta;
        if (!isCanonicalPointer(rootVptr, opts)) return null;
        return {address: address, first: first, second: second,
            rootVptr: Number(rootVptr), manager: Number(manager)};
    }

    function groupDomainCandidates(candidates) {
        var groups = [];
        var indexes = Object.create(null);
        for (var i = 0; i < candidates.length; i++) {
            var candidate = candidates[i];
            var key = candidate.rootVptr.toString(16) + ":" +
                candidate.manager.toString(16);
            var groupIndex = indexes[key];
            if (groupIndex === undefined) {
                groupIndex = groups.length;
                indexes[key] = groupIndex;
                groups.push({rootVptr: candidate.rootVptr,
                    manager: candidate.manager, domains: []});
            }
            groups[groupIndex].domains.push(candidate);
        }
        return groups;
    }

    return {
        rangeContains: rangeContains,
        isCanonicalPointer: isCanonicalPointer,
        derivePoolBounds: derivePoolBounds,
        choosePoolRefinementStart: choosePoolRefinementStart,
        parseExternalLink: parseExternalLink,
        metadataWindowFromNext: metadataWindowFromNext,
        makeDomainCandidate: makeDomainCandidate,
        groupDomainCandidates: groupDomainCandidates
    };
})();
