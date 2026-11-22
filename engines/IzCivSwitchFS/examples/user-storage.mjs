// Open and close User content storage using the session kept by the main payload.

import coreRuntime from "../src/core/runtime.mjs";

// Switchbrew documents command 110 as OpenContentStorageFileSystem and maps it
// to the CanMountContentStorage FS permission. fsp-srv checks the permissions
// registered for the PID attached to the session; PIDs <= 6 use built-in
// permission data. A successful User-storage open therefore proves that this
// session passes that privileged FS permission check. It does not identify
// PID 1 by itself: the main payload separately verifies its PID promotion.
// Source: https://switchbrew.org/wiki/Filesystem_services#Permissions

function hex32(value) {
    return "0x" + (Number(value) >>> 0).toString(16).padStart(8, "0");
}

const {
    allocPage,
    beginAllocationTracking,
    endAllocationTracking,
    freeTrackedAllocations,
    log,
    prepPacket,
    readDomainObjects,
    sendDomainCmd,
    sendIPC,
    write32,
    write8
} = coreRuntime;

export async function run() {
    const session = globalThis.fspSrv;
    if (!session) {
        await log("Run the main payload successfully before this example.");
        return;
    }

    const allocations = beginAllocationTracking();
    if (!allocations) {
        await log("Another payload is using allocation tracking.");
        return;
    }

    let filesystemObjects = [];
    try {
        const packet = allocPage(0x2000);
        const reply = sendDomainCmd(session.handle, session.rootObj, 110,
            packet, () => write32(packet + 0x30n, 1n), 4);
        const objects = reply.ok
            ? readDomainObjects(packet, reply.objCount, 0)
            : [];
        filesystemObjects = objects.filter(objectId => objectId !== 0);
        const accessPassed = reply.ok && reply.transport === 0 &&
            reply.result === 0 && reply.objCount === 1 &&
            filesystemObjects.length === 1;

        await log("OpenContentStorageFileSystem(User=1): reply.ok=" +
            reply.ok + " transport=" + hex32(reply.transport) +
            " result=" + hex32(reply.result) +
            " objCount=" + reply.objCount + " domainObjectIds=[" +
            objects.map(hex32).join(", ") + "]");
        await log(accessPassed
            ? "Permission evidence: this fsp-srv session passed " +
                "CanMountContentStorage and received one filesystem object."
            : "Permission check failed or returned an unexpected response.");
    } finally {
        for (const filesystemObject of filesystemObjects) {
            const packet = allocPage(0x2000);
            prepPacket(packet, 4, 8, []);
            write8(packet + 0x10n, 2n);
            write32(packet + 0x14n, BigInt(filesystemObject));
            sendIPC(session.handle, packet);
        }

        if (endAllocationTracking(allocations))
            freeTrackedAllocations(allocations);
    }
}
