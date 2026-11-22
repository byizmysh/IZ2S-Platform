// Acquire filesystem access, then start RUN25's existing IZ2s.
import coreRuntime from "./core/runtime.mjs";
import {run as acquireFilesystem} from "./core.js";
import {run as launchIz2s} from "./iz2s.js";

export async function run() {
    if (globalThis.__KH?.stages?.launch || globalThis.__KH?.stages?.complete)
        throw Error("IZ2S launch already attempted on this boot; restart the console first");
    delete globalThis.fsKaihen;
    await acquireFilesystem();
    if (!globalThis.fsKaihen?.domainSession) {
        await coreRuntime.log("IZ2SCiv: filesystem access unavailable; restart the game to retry");
        return;
    }
    await launchIz2s();
}
