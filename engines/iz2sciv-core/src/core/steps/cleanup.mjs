import coreRuntime from "../runtime.mjs";
import {verboseLog} from "../logger.mjs";

const {
  closeHandle,
  hex32,
  ipcCommitChecked,
  ipcDeleteDirectoryRecursivelyChecked,
  ipcDeleteFileChecked,
  ipcGetEntryTypeChecked,
  ipcFileInvalidate
} = coreRuntime;

// kaiHEN's donor pair sits at the root of the game's save: /main (the RO
// image) and /main.npdm (the captured policy). It is deliberately KEPT —
// the next kaiHEN run reuses it by hash instead of rewriting the save, so
// this every-boot cleanup must not wipe it (see kaihen/donor.mjs). Only the
// kaiHEN donor stage may remove the pair, and only when its content no
// longer matches. Put the names back in this list to restore the wipe.
const DONOR_LEFTOVER_FILES = [];

// ── Cleanup and retirement (S19–S21) ────────────────────────────────────────

export async function retireCandidateCacheAndClose({
  helpers,
  addressCopy,
  candidateCacheNeedsRetirement,
  candidateHandle,
  retireRetainedEndpoint,
  workerStateSafe,
}) {
    // S21: Verify the synthetic cache entry, retiring it only if needed.
    let candidateCacheRetired = false;
    let candidateCacheRetirementSkipped = false;
    var candidateCloseSafe = workerStateSafe;
    var workersQuiesced = workerStateSafe === true;
    var stateSafe = workerStateSafe === true;
    var candidateCloseResult = null;
    var retirementFailureReason = null;
    if (addressCopy && addressCopy.retainedEndpointActive) {
        if (!candidateCloseSafe || !candidateHandle ||
            typeof retireRetainedEndpoint !== "function") {
            addressCopy = {...addressCopy, retainedEndpointUnsafe: true,
                workerStateSafe: false};
            return {ok: false, addressCopy, candidateCacheRetired,
                candidateHandle, workerStateSafe: false,
                workersQuiesced,
                failureReason: "retained-endpoint-state-unsafe"};
        }
        try {
            var retainedRetirement = await retireRetainedEndpoint();
            if (!retainedRetirement || !retainedRetirement.retired)
                throw new Error("retained endpoint did not confirm retirement");
            addressCopy = retainedRetirement.addressCopy || addressCopy;
        } catch (retirementError) {
            addressCopy = {...addressCopy, retainedEndpointUnsafe: true,
                workerStateSafe: false};
            return {ok: false, addressCopy, candidateCacheRetired,
                candidateHandle, workerStateSafe: false,
                workersQuiesced,
                failureReason: "retained-endpoint-retirement-failed: " +
                    retirementError.message};
        }

        try {
            candidateCloseResult = Number(closeHandle(candidateHandle)) >>> 0;
            if (candidateCloseResult !== 0) {
                addressCopy = {...addressCopy, retainedEndpointUnsafe: true,
                    workerStateSafe: false};
                return {ok: false, addressCopy, candidateCacheRetired,
                    candidateHandle, workerStateSafe: false,
                    workersQuiesced,
                    candidateCloseResult,
                    failureReason: "retained-candidate-close-failed"};
            }
            candidateHandle = 0;
            candidateCacheRetired = true;
            addressCopy = {...addressCopy, retainedEndpointActive: false,
                retainedEndpointRetired: true, candidateCacheRetired: true};
        } catch (closeError) {
            addressCopy = {...addressCopy, retainedEndpointUnsafe: true,
                workerStateSafe: false};
            return {ok: false, addressCopy, candidateCacheRetired,
                candidateHandle, workerStateSafe: false,
                workersQuiesced,
                candidateCloseResult,
                failureReason: "retained-candidate-close-threw: " +
                    closeError.message};
        }
        return {ok: true, addressCopy, candidateCacheRetired,
            candidateHandle, candidateCloseResult,
            workerStateSafe: stateSafe, workersQuiesced};
    }
    if (candidateHandle && candidateCacheNeedsRetirement && candidateCloseSafe) {
        var retirement = await helpers.retireCandidateCache({
            candidateHandle, candidateCacheNeedsRetirement,
            candidateCacheRetired, addressCopy
        });
        workersQuiesced = retirement.addressCopy?.workerStateSafe !== false &&
            workersQuiesced;
        stateSafe = workersQuiesced;
        candidateCloseSafe = retirement.ok && workersQuiesced;
        candidateCacheRetired = retirement.candidateCacheRetired;
        candidateCacheRetirementSkipped = retirement.retirementSkipped === true;
        if (!retirement.ok)
            retirementFailureReason = "candidate-cache-retirement-failed";
        addressCopy = {...retirement.addressCopy, candidateCacheRetired};
    }

    if (candidateHandle && candidateCloseSafe) {
        try {
            candidateCloseResult = Number(closeHandle(candidateHandle)) >>> 0;
            candidateHandle = 0;
            if (candidateCloseResult !== 0)
                retirementFailureReason = "candidate-close-failed";
        } catch (closeError) {
            // Cache safety was verified before close. A failed close leaks the
            // service handle, but does not leave an active cache alias behind.
            candidateHandle = 0;
            retirementFailureReason = "candidate-close-threw: " +
                closeError.message;
        }
    } else if (candidateHandle) {
        retirementFailureReason = retirementFailureReason ||
            "candidate-close-skipped-unsafe-state";
    }

  return {ok: true, addressCopy, candidateCacheRetired, candidateHandle,
      candidateCacheRetirementSkipped,
      workerStateSafe: stateSafe, workersQuiesced, candidateCloseResult,
      failureReason: retirementFailureReason};
}

export async function cleanupScratchAndHandles({
  SCRATCH_DIR,
  auxiliaryStorageHandle,
  candidateCacheNeedsRetirement,
  candidateCacheRetired,
  candidateHandle,
  retainedEndpointActive,
  domainSession,
  extentTriggerHandle,
  extraStorages,
  pinHandle,
  rootClones,
  rootSession,
  saveClones,
  savedHandle,
  storageHandle,
  temporaryHandle,
  triggerHandle,
  workerStateSafe,
  keepDomainSessionOpen = false,
  dirtyFileSize = 0
}) {
    // S22: Cleanup. Report a failed or skipped cleanup as unsafe to retry.
    var cleanupAttempted = false;
    var cleanupDeleteTransport = null;
    var cleanupRemoveResult = null;
    var cleanupCommitTransport = null;
    var cleanupCommitResult = null;
    var cleanupDonorWiped = 0;
    var cleanupHandlesClosed = 0;
    var cleanupExpectedHandles = 0;
    var cleanupCloseFailures = [];
    var cleanupErrors = [];
    var cleanupAllowed = workerStateSafe && (retainedEndpointActive ||
        !candidateCacheNeedsRetirement || candidateCacheRetired || !candidateHandle);
    var cleanupSkipReason = workerStateSafe
        ? "candidate-cache-not-retired" : "worker-state-unsafe";

    if (cleanupAllowed) {
        var countExpectedHandle = function(handle) {
            if (handle) cleanupExpectedHandles++;
        };
        for (var ci = 0; ci < saveClones.length; ci++)
            countExpectedHandle(saveClones[ci]);
        for (var ci = 0; ci < rootClones.length; ci++)
            countExpectedHandle(rootClones[ci]);
        countExpectedHandle(triggerHandle);
        countExpectedHandle(extentTriggerHandle);
        countExpectedHandle(savedHandle);
        countExpectedHandle(temporaryHandle);
        countExpectedHandle(storageHandle);
        countExpectedHandle(auxiliaryStorageHandle);
        countExpectedHandle(pinHandle);
        for (var cei = 0; cei < extraStorages.length; cei++)
            countExpectedHandle(extraStorages[cei]);
        if (domainSession && !keepDomainSessionOpen)
            countExpectedHandle(domainSession.handle);
        countExpectedHandle(rootSession);
        cleanupAttempted = true;
        var cacheInvalidateResult = null;
        if (dirtyFileSize > 0 && SCRATCH_DIR) {
            try {
                cacheInvalidateResult = ipcFileInvalidate(
                    savedHandle, SCRATCH_DIR + "/big8", dirtyFileSize);
            } catch (error) {
                cacheInvalidateResult = "throw:" + String(error);
            }
            await verboseLog("CLEANUP: cache invalidate " + SCRATCH_DIR +
                "/big8 result=" + (typeof cacheInvalidateResult === "number"
                    ? hex32(cacheInvalidateResult) : cacheInvalidateResult));
        }
        // Donor leftovers would ride the same commit: absent files are
        // skipped silently, and a present file that fails to delete is a
        // real cleanup failure. The list is empty by default because the
        // donor pair is kept on purpose (DONOR_LEFTOVER_FILES above).
        for (var di = 0; di < DONOR_LEFTOVER_FILES.length; di++) {
            var donorFile = DONOR_LEFTOVER_FILES[di];
            try {
                var donorEntry = ipcGetEntryTypeChecked(savedHandle, donorFile);
                if (donorEntry.transport !== 0 || donorEntry.result !== 0)
                    continue;
                var donorDelete = ipcDeleteFileChecked(savedHandle, donorFile);
                if (donorDelete.transport === 0 && donorDelete.result === 0) {
                    cleanupDonorWiped++;
                    continue;
                }
                cleanupErrors.push("donor-" + donorFile + "-delete=" +
                    (donorDelete.transport !== 0
                        ? hex32(donorDelete.transport) : hex32(donorDelete.result)));
            } catch (donorError) {
                cleanupErrors.push("donor-" + donorFile + "=" + String(donorError));
            }
        }
        try {
            var deleteStatus = ipcDeleteDirectoryRecursivelyChecked(
                savedHandle, SCRATCH_DIR);
            cleanupDeleteTransport = deleteStatus.transport;
            cleanupRemoveResult = deleteStatus.result;
        } catch (error) {
            cleanupErrors.push("delete=" + String(error));
        }
        try {
            var commitStatus = ipcCommitChecked(savedHandle);
            cleanupCommitTransport = commitStatus.transport;
            cleanupCommitResult = commitStatus.result;
        } catch (error) {
            cleanupErrors.push("commit=" + String(error));
        }

        var closeForCleanup = function(handle, label) {
            if (!handle) return;
            try {
                var result = Number(closeHandle(handle)) >>> 0;
                if (result !== 0) {
                    cleanupCloseFailures.push(label + "=" + hex32(result));
                } else {
                    cleanupHandlesClosed++;
                }
            } catch (error) {
                cleanupCloseFailures.push(label + "=throw:" + String(error));
            }
        };

        for (var i = 0; i < saveClones.length; i++)
            closeForCleanup(saveClones[i], "save-clone-" + i);
        for (var i = 0; i < rootClones.length; i++)
            closeForCleanup(rootClones[i], "root-clone-" + i);
        closeForCleanup(triggerHandle, "trigger");
        closeForCleanup(extentTriggerHandle, "extent-trigger");
        closeForCleanup(savedHandle, "save-fs");
        closeForCleanup(temporaryHandle, "temporary-fs");
        closeForCleanup(storageHandle, "storage");
        closeForCleanup(auxiliaryStorageHandle, "aux-storage");
        closeForCleanup(pinHandle, "pin");
        for (var ei = 0; ei < extraStorages.length; ei++)
            closeForCleanup(extraStorages[ei], "extra-storage-" + ei);
        if (domainSession && !keepDomainSessionOpen) {
            closeForCleanup(domainSession.handle, "identity-domain-session");
            domainSession = null;
        }
        closeForCleanup(rootSession, "fsp-session");
    }

    var cleanupOk = cleanupAttempted && cleanupDeleteTransport === 0 &&
        cleanupRemoveResult === 0 && cleanupCommitTransport === 0 &&
        cleanupCommitResult === 0 &&
        cleanupHandlesClosed === cleanupExpectedHandles &&
        cleanupCloseFailures.length === 0 && cleanupErrors.length === 0;
    return {
        // Preserve the legacy `ok` contract for callers outside this workflow;
        // the payload consumes cleanupVerified for its explicit status line.
        ok: true,
        cleanupVerified: cleanupOk,
        attempted: cleanupAttempted,
        skipped: !cleanupAttempted,
        workerStateSafe: Boolean(workerStateSafe),
        skipReason: cleanupAttempted ? null : cleanupSkipReason,
        deleteTransport: cleanupDeleteTransport,
        deleteResult: cleanupRemoveResult,
        commitTransport: cleanupCommitTransport,
        commitResult: cleanupCommitResult,
        donorFilesWiped: cleanupDonorWiped,
        handlesClosed: cleanupHandlesClosed,
        expectedHandles: cleanupExpectedHandles,
        closeErrors: cleanupCloseFailures,
        errors: cleanupErrors,
        domainSession
    };
}

