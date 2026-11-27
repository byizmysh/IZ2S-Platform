// kaiHEN port — state-diff dump. At the loader-copy point, emit this boot's
// chain state in the author engine's proof schema (their runs/switch2-*/
// switch2/ro/-layout.json, -thread-ring.json, root/current-root.json and the
// loader-root-copy proof), so a PC-side diff against the proven-good same-
// console run switch2-5T5Brx can expose the environmental divergence behind
// our copied-session death (all mechanics verified identical; see FINDINGS.md
// "STATIC SESSION"). Structural values are preferred; absolute addresses are
// logged once per section for orientation. Best-effort: a section error is
// logged and skipped, never fatal, and no handle is opened or sent on.

import {khLog, hex, readU64LE, readU32LE, bytesToHex} from "./state.mjs";
import {readFs, fsU64} from "./endpoint.mjs";

async function section(name, body) {
    try {
        await body();
    } catch (error) {
        await khLog("SD " + name + " ERROR " +
            (error && error.message ? error.message : error));
    }
}

export async function dumpChainState(context) {
    const layout = context.layout;

    await section("layout", async () => {
        const header = await readFs(context, layout.sdk + 0x830, 0x20);
        await khLog("SD1 layout sdk=" + hex(layout.sdk) +
            " rootVptr=" + hex(layout.rootVptr) +
            " manager=" + hex(layout.manager) +
            " table=" + hex(layout.table) +
            " holderVptr=" + hex(layout.holderVptr) +
            " slots=" + hex(layout.slots));
        await khLog("SD1 capture " + hex(layout.captureStart) + ".." +
            hex(layout.captureEnd) + " size=" +
            hex(layout.captureEnd - layout.captureStart) +
            " sdkFromCapture=" + hex(layout.sdk - layout.captureStart));
        await khLog("SD1 sdkPage first=" +
            hex(Number(readU64LE(header, 0))) +
            " count=" + readU32LE(header, 0x18));
    });

    await section("target", async () => {
        const t = context.loaderRoot;
        const index = Math.floor((t.entry - layout.table) / 0x30) + 1;
        const entryBytes = await readFs(context, t.entry, 0x30);
        const objectBytes = await readFs(context, t.object, 0x40);
        await khLog("SD2 target id=" + index + " entry=" + hex(t.entry) +
            " object=" + hex(t.object) + " domain=" + hex(t.domain) +
            " metadata=" + hex(Number(readU64LE(entryBytes, 0x28))));
        await khLog("SD2 target entry " + bytesToHex(entryBytes));
        await khLog("SD2 target object " + bytesToHex(objectBytes));
        await khLog("SD2 target vptrDelta=" +
            hex(Number(readU64LE(objectBytes, 0)) - layout.rootVptr) +
            " pid=" + Number(readU64LE(objectBytes, 0x30)) +
            " ref=" + readU32LE(objectBytes, 0x38));
    });

    await section("own", async () => {
        const owned = context.inspectionDomain;
        const copy = context.loaderCopy;
        const entry = layout.table + (owned.root - 1) * 0x30;
        const entryBytes = await readFs(context, entry, 0x30);
        const objectBytes = await readFs(context, copy.ownObject, 0x40);
        await khLog("SD3 own id=" + owned.root + " entry=" + hex(entry) +
            " object=" + hex(copy.ownObject) +
            " domain=" + hex(copy.ownDomain) +
            " copyHandle=" + hex(copy.handle));
        await khLog("SD3 own entry " + bytesToHex(entryBytes));
        await khLog("SD3 own object " + bytesToHex(objectBytes));
        await khLog("SD3 own vptrDelta=" +
            hex(Number(readU64LE(objectBytes, 0)) - layout.rootVptr) +
            " pid=" + Number(readU64LE(objectBytes, 0x30)) +
            " ref=" + readU32LE(objectBytes, 0x38));
    });

    await section("table", async () => {
        const tableBytes = await readFs(context, layout.table, 0x4000);
        let live = 0, rootFamily = 0;
        const liveIds = [];
        for (let i = 0; i + 0x30 <= 0x4000; i += 0x30) {
            const owner = readU64LE(tableBytes, i + 0x18);
            const object = readU64LE(tableBytes, i + 0x20);
            if (owner === 0n || object === 0n) continue;
            live++;
            liveIds.push(i / 0x30 + 1);
            if (readU64LE(tableBytes, i + 0x28) ===
                    BigInt(layout.rootVptr + 0x5880)) rootFamily++;
        }
        await khLog("SD4 table live=" + live + " rootFamily=" + rootFamily +
            " ids=" + liveIds.slice(0, 24).join(","));
    });

    await section("identity", async () => {
        for (const field of context.state.identityObjects || []) {
            const live = await fsU64(context, field.object + field.pidOffset);
            await khLog("SD5 " + field.name + " object=" + hex(field.object) +
                " pidOffset=" + hex(field.pidOffset) +
                " original=" + field.pid + " live=" + Number(live));
        }
    });

    await section("ring", async () => {
        const nodes = context.threadRing || [];
        let aliased = 0;
        const states = {};
        const map = [];
        for (const node of nodes) {
            if (node.aliased) aliased++;
            states[node.state] = (states[node.state] || 0) + 1;
            map.push((node.aliased ? "A" : "-") +
                (node.state < 10 ? node.state : "x"));
        }
        await khLog("SD6 ring nodes=" + nodes.length +
            " aliased=" + aliased +
            " states=" + Object.entries(states)
                .map(([k, v]) => k + ":" + v).join(","));
        for (let at = 0; at < map.length; at += 600)
            await khLog("SD6 map[" + at + "] " + map.slice(at, at + 600).join(""));
    });

    await section("handles", async () => {
        const census = {};
        for (const item of context.handles || [])
            census[item.label] = (census[item.label] || 0) + 1;
        await khLog("SD7 handles " + Object.entries(census)
            .map(([k, v]) => k + ":" + v).join(","));
    });

    await section("context", async () => {
        const pm = context.pm || {};
        const notifier = context.notifier || {};
        await khLog("SD8 pmKeys=" + Object.keys(pm).join("/") +
            " notifierObject=" + (notifier.object ? hex(notifier.object) : "-") +
            " eventHandle=" + (notifier.eventHandle ?
                hex(notifier.eventHandle) : "-"));
    });

    await khLog("SD done");
    return {ok: true};
}
