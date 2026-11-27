// kaiHEN port — activate the launched RO process (identify, start, terminate
// the original ldr:ro) and verify the Loader session returned to idle.

import {check, khLog, hex, readU64LE, readU32LE, arenaSlots} from "./state.mjs";
import {readFs} from "./endpoint.mjs";
import {pmIpcU64} from "./pm.mjs";

// pm:dmnt command 7 returns the program id of a pid.
function getProgramId(context, pid, label) {
    const reply = pmIpcU64(context, context.pm.monitor, 7, pid, "GetProgramId " + label);
    check(reply.result === 0, "pm did not identify " + label + ": " + hex(reply.result));
    return readU64LE(reply.data, 0);
}

export async function activateRoProcess(context, expectedProgramId) {
    const launch = context.launch;
    check(launch && launch.pid, "no accepted launch to activate");
    const pid = launch.pid;
    const originalPid = context.pm.originalPid;
    check(pid !== originalPid && pid > 1 && originalPid > 1,
        "the original and replacement RO pids must be distinct");

    const replacement = getProgramId(context, pid, "replacement");
    const original = getProgramId(context, originalPid, "original");
    check(replacement === BigInt(expectedProgramId) &&
        original === BigInt(expectedProgramId),
        "PM did not identify both pids as the RO title (" +
        replacement.toString(16) + "/" + original.toString(16) + ")");

    const start = pmIpcU64(context, context.pm.monitor, 1, pid, "StartProcess");
    check(start.result === 0, "the accepted RO process did not start");

    const terminate = pmIpcU64(context, context.pm.shell, 1, originalPid,
        "TerminateProcess original");
    check(terminate.result === 0,
        "the original RO process did not release its services");

    context.activation = {pid, originalPid, started: true, terminated: originalPid};
    await khLog("RO process " + pid + " started; original " +
        originalPid + " terminated");
    return context.activation;
}

// handoff: the Loader session must be back on its idle receive (reference
// runtime.handoff).
export async function verifyHandoff(context) {
    const loaderDomain = context.loaderRoot.domain;
    const row = context.bound.rows.find(r => r.domain === loaderDomain);
    check(row, "the Loader session is missing");
    const b = await readFs(context, row.address, 0x88);
    const raw = [];
    for (let i = 0; i < 0x88; i++)
        raw.push(parseInt(row.raw.substr(i * 2, 2), 16));
    check(readU64LE(b, 0) === readU64LE(raw, 0) &&
        readU64LE(b, 0x70) === BigInt(row.domain) &&
        readU64LE(b, 0x78) === BigInt(row.metadata) &&
        readU64LE(b, 0x60) === BigInt(row.request) &&
        readU32LE(b, 0x30) === row.receive &&
        readU32LE(b, 0x48) === row.receive && readU32LE(b, 0x40) === 1 &&
        context.layout.waiters.includes(Number(readU64LE(b, 0x28))),
        "Loader has not returned to its original idle receive session");
    context.handoff = {verified: true, loader: {address: row.address,
        wait: row.receive, receive: row.receive, state: 1}};
    await khLog("handoff verified: loader session idle");
    return context.handoff;
}
