// IZ2SCiv core: run the packaged IZ2S runtime after FS acquisition.
// Transport helpers retain their upstream filenames and authorship.
import coreRuntime from "./core/runtime.mjs";
import runtime from "./core/runtime.mjs";
import {createContext, khLog, hex, check, stageDone, stageRecord,
    arenaSlots, ensureArena, RO_TITLE} from "./kaihen/state.mjs";
import {armEndpoint} from "./kaihen/endpoint.mjs";
import {deriveLayout, findLoaderRoot, readThreadRing} from "./kaihen/layout.mjs";
import {dumpChainState} from "./kaihen/diff.mjs";
import {instantSendProbe} from "./kaihen/instant-send.mjs";
import {openNotifier, exportHandle, openInspectionDomain, copyLoaderRoot}
    from "./kaihen/objects.mjs";
import {verifyNativeStubs, openPmSessions} from "./kaihen/pm.mjs";
import {verifyEventCode, prepareApiWorkers} from "./kaihen/workers.mjs";
import {prepareDonor, cleanupDonor} from "./kaihen/donor.mjs";
import {openCodeFileSystem, openCodeFileSystemViaDomain, adoptCodeFileSystem,
    captureCodeFile, codeDomain, donorDomainRow, verifyAdapterTypes, bindSessions}
    from "./kaihen/codefs.mjs";
import {Gate, prepareGateEvent} from "./kaihen/gate.mjs";
import {launchRoFrontend} from "./kaihen/launch.mjs";
import {activateRoProcess, verifyHandoff} from "./kaihen/activate.mjs";
import {connectRoFrontend} from "./kaihen/ro.mjs";
import {runBootstrap} from "./kaihen/bootstrap.mjs";
import {openSdCard, sdReadFile}
    from "./kaihen/sdcard.mjs";
import {cmifSend} from "./kaihen/wire.mjs";
import {SD_MANIFEST} from "./iz2s-runtime.mjs";
import {verifySdRuntime} from "./sd-verify.mjs";

const {closeHandle, log} = coreRuntime;

export async function run() {
    await khLog("Starting IZ2s from /iz2s");
    const context = createContext();
    if (stageRecord("launch") || stageRecord("complete"))
        throw Error("IZ2S launch already attempted on this boot; restart the console first");
    // Reclaim only an earlier failed attempt, before creating this run's SD handle.
    for (const item of (globalThis.__KH_TRACKED || []).slice().reverse()) {
        try { closeHandle(item.handle); } catch (_) { /* stale handle */ }
    }
    globalThis.__KH_TRACKED = null;
    globalThis.__KH.sd = null;
    ensureArena(context);
    arenaSlots(context);
    const stages = [];
    const stage = async (name, action) => {
        await khLog("--- stage: " + name);
        stages.push(name);
        const result = await action();
        stageDone(name, {ok: true});
        return result;
    };
    try {
        // Pin the boot core, but accept compatible launcher rebuilds of any
        // supported size. No skipcheck flag or SD relocation is consulted.
        await stage("sd-verify", async () => {
            verifySdRuntime(context, SD_MANIFEST);
            await khLog("IZ2S boot core SHA-256 verified; launcher headers valid (SD update policy v1)");
        });
        await stage("endpoint", () => armEndpoint(context));
        await stage("layout", async () => {
            await deriveLayout(context);
            await findLoaderRoot(context);
            await readThreadRing(context);
        });
        await stage("sdk-stubs", () => {
            verifyEventCode();
            verifyNativeStubs(context);
        });
        await stage("notifier", () => openNotifier(context));
        await stage("pm", () => openPmSessions(context, RO_TITLE));
        await stage("loader-copy", async () => {
            await openInspectionDomain(context);
            await copyLoaderRoot(context);
            await instantSendProbe(context, RO_TITLE);
            await dumpChainState(context);
        });
        await stage("api-workers", () => prepareApiWorkers(context));
        // Donor: the RO image from the SD card, the policy from the real
        // RO title's CodeFS through the copied Loader root.
        await stage("donor", async () => {
            let codefs;
            if (context.instantCodeFs) {
                // The instant-send probe already opened the CodeFS through
                // the domain-routed WithContext path (run69: transport 0,
                // result 0, handle moved) — adopt it directly.
                codefs = adoptCodeFileSystem(context, context.instantCodeFs,
                    context.instantCodeFsInfo, context.instantCodeFsAdded);
                await khLog("code fs adopted from IS (added=" +
                    hex(context.instantCodeFsAdded) + ")");
            } else {
                try {
                    codefs = openCodeFileSystem(context, context.loaderCopy.handle,
                        RO_TITLE);
                    await khLog("code fs opened via copied root");
                } catch (error) {
                    await khLog("copied-root code open failed: " +
                        (error && error.message ? error.message : error) +
                        "; trying domain route");
                    const loaderObjectId = Math.floor(
                        (context.loaderRoot.entry - context.layout.table) / 0x30) + 1;
                    check(loaderObjectId > 0 && loaderObjectId <= 0x4000,
                        "loader object id out of table range");
                    codefs = await openCodeFileSystemViaDomain(context,
                        context.inspectionDomain.handle, loaderObjectId, RO_TITLE);
                }
            }
            await khLog("code fs opened (signed=" + codefs.codeInfo.isSigned + ")");
            check(codefs.added, "donor requires a domain-added CodeFS object");
            const npdm = await captureCodeFile(context, codefs.added,
                "main.npdm");
            await khLog("captured main.npdm (" + npdm.size + " bytes)");
            const roMain = sdReadFile(context, openSdCard(context), SD_MANIFEST.ro.path, 0x100000);
            await codeDomain(context, codefs.handle, codefs.added);
            await prepareDonor(context,
                {"main": roMain.bytes, "main.npdm": npdm.bytes});
            await donorDomainRow(context);
            verifyAdapterTypes(context);
        });
        await stage("bind-sessions", () => bindSessions(context));
        await stage("gate-event", () => prepareGateEvent(context));
        await stage("gate-test", async () => {
            // Park the LOADER's server session row — the row the launch
            // gates (the author's launch.cjs parks root.targetDomain = the
            // loader's domain; their test-gate parked ownDomain only because
            // their daemon's memory access didn't traverse it — OUR endpoint
            // storage lives in our own domain session, so parking ownDomain
            // wedges our only memory primitive, runs 90-95).
            const domain = context.inspectionDomain;
            const row = context.bound.rows.find(r =>
                r.domain === context.loaderRoot.domain);
            check(row, "the loader's bound server session is missing");
            const copy = await exportHandle(context, row.receive,
                "loader server readiness");
            const gate = new Gate(context, row, copy.handle, context.gateEvent);
            try {
                await gate.parkSession();
                await khLog("GTA parked the loader row alone");
                await gate.restore();
                await khLog("GTA restored");
                context.gateTest = {verified: true};
            } finally {
                await gate.restore();
            }
            await closeHandle(copy.handle);
        });
        await stage("launch", async () => {
            // Stash the rich launch proof on the ledger so a failed run's
            // postmortem can read the full request-by-request record.
            globalThis.__KH.launchProof = await launchRoFrontend(context);
        });
        await stage("activate", async () => {
            await activateRoProcess(context, RO_TITLE);
            await verifyHandoff(context);
        });
        await stage("donor-cleanup", () => cleanupDonor(context));
        await stage("ro-frontend", async () => {
            connectRoFrontend(context);
            await khLog("ldr:ro frontend connected");
            check(context.fsRootPlain,
                "the plain fsRoot clone is missing (created with the notifier)");
        });
        await stage("fs-process", async () => {
            context.fsProcess = await exportHandle(context, 0xffff8001,
                "FS process");
        });
        await stage("bootstrap", () => runBootstrap(context));

        await khLog("=== IZ2S CORE IS RUNNING ===");
        await khLog("Close Civilization VII normally; hold R while opening " +
            "a Switch 2 game; repeat once if needed. IZ2s opens /iz2s/izlauncher.iza.");
        stageDone("complete", {ok: true});

    } catch (error) {
        await khLog("FAILED at stage '" +
            (stages.length ? stages[stages.length - 1] : "?") + "': " +
            (error && error.message ? error.message : error));
        if (error && error.stack) await log(String(error.stack).split("\n")
            .slice(0, 3).join(" | "));
        // The boot may still be usable; report what completed.
        await khLog("completed stages: " + stages.join(" > "));
        throw error;
    } finally {
        // Retire the IStorage cache alias so stale heap pointers don't
        // persist in the BC save.  Every writeFs call during the chain
        // wrote heap-targeted bytes into the dirty buffer via
        // write-through; intermediate commits (donor, cleanupDonor)
        // flushed them to NAND.
        //
        // A plain ipcStorageWrite at offset 0 goes through the alias
        // and lands in the heap, not the save. Invalidating the I/O
        // cache first drops the dirty buffer entries so the subsequent
        // write bypasses the alias and reaches the backing store.
        try {
            if (context.candidateHandle) {
                runtime.ipcStorageInvalidate(
                    context.candidateHandle, 0, 0x8000);

                const retireBuf = runtime.allocPage(0x1000);
                for (let i = 0; i < 16; i++)
                    runtime.write8(retireBuf + BigInt(i), 0x42n);
                runtime.ipcStorageWrite(
                    context.candidateHandle, 0n, retireBuf, 16);

                const saveMount = runtime.openMount("save");
                if (saveMount && saveMount.handle) {
                    const pkt = runtime.allocPage(0x2000);
                    const commitReply = cmifSend(saveMount.handle,
                        {pkt, cmd: 10, input: [], rawWords: 8});
                    closeHandle(saveMount.handle);
                    await khLog("RETIRE: cache invalidated + " +
                        "alias neutralised tx=" +
                        hex(commitReply.transport || 0) +
                        " rc=" + hex(commitReply.result || 0));
                }
            }
        } catch (retireError) {
            try {
                await khLog("RETIRE: best-effort failed: " +
                    String(retireError?.message || retireError));
            } catch (_) { /* silent */ }
        }
        
        if (!context.henStartup) {
            // Failed run: release every tracked handle (reverse order) so the
            // boot stays reusable — leaked sm/pm sessions otherwise exhaust
            // service quotas within a few attempts on the same boot.
            for (let i = context.handles.length - 1; i >= 0; i--) {
                try { await closeHandle(context.handles[i].handle); }
                catch (error) { /* closing a dead handle is harmless */ }
            }
            globalThis.__KH_TRACKED = null;
            // The closed set may include the SD session cached in
            // globalThis.__KH.sd — drop the cache so later evals reopen
            // instead of sending on a closed handle (run 26 showed every SD
            // file "missing" and create failing 0xE401 from exactly this).
            if (globalThis.__KH) globalThis.__KH.sd = null;
        }
        if (context.loaderServerExport && context.henStartup) {
            try { await closeHandle(context.loaderServerExport.handle); }
            catch (error) { await khLog("export close failed"); }
        }
        if (context.fsProcess && context.henStartup) {
            try { await closeHandle(context.fsProcess.handle); }
            catch (error) { await khLog("fs process close failed"); }
        }
    }
}
