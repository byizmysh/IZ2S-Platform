import test from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import path from "node:path";
import {inflateRawSync} from "node:zlib";
import esbuild from "esbuild";
import {buildPayload, ROOT, MAX_PAYLOAD_BYTES} from "./bundle.mjs";
import {buildPersistentImage} from "./persist-build.mjs";
import {loadIz2sRuntime, iz2sRuntimePlugin} from "./iz2s-runtime.mjs";

async function moduleAt(entry, context = {}, plugins = [iz2sRuntimePlugin]) {
    const result = await esbuild.build({entryPoints: [path.join(ROOT, entry)],
        bundle: true, format: "cjs", platform: "node", write: false, plugins});
    const module = {exports: {}};
    vm.runInNewContext(result.outputFiles[0].text, {...context, module, exports: module.exports});
    return module.exports;
}

test("Packaged runtime is verified and all targets stay in /iz2s", () => {
    const data = loadIz2sRuntime();
    assert.equal(data.packagePath, "@Sdcard:/iz2s/core/bin/core/exefs.nsp");
    assert.equal(data.manifest.launcher.path, "/iz2s/izlauncher.iza");
    assert.equal(data.manifest.resources.path, "/iz2s/izlauncher.romfs");
    for (const item of Object.values(data.manifest)) {
        assert(item.path.startsWith("/iz2s/"));
        assert(item.size > 0);
        assert.match(item.sha256, /^[a-f0-9]{64}$/);
    }
});

test("native argument block keeps the SB4 ABI with the IZ2S package target", async () => {
    const module = await moduleAt("src/kaihen/bootstrap.mjs",
        {__CIVSWITCH_CORE_API__: {}, sdk_base: 0x100000n});
    const context = {
        launch: {}, loaderRoot: {domain: 1, entry: 0x1030},
        bound: {rows: [{domain: 1, receive: 1, wait: 2, address: 0x5000,
            raw: "0000000000000000", metadata: 3, request: 4}]},
        donorRow: {entry: 5, object: 6, metadata: 7, vptr: 8},
        layout: {table: 0x1000, waiters: [1, 2, 3, 4, 5, 6, 7]},
        gateEvent: {sourceHandle: 3, read: 4, write: 5},
        loaderServerExport: {handle: 6},
        apiWorkers: Array.from({length: 7}, (_, i) => ({handle: i + 7})),
        candidateHandle: 14, state: {endpoint: {dataOffset: 8, dataEntryOffset: 16}},
        pm: {shell: 15, monitor: 16, sm: 17}, bootstrapFsRoot: 18,
        fsProcess: {handle: 19}, notifier: {handle: 20}, loaderCopy: {handle: 21}
    };
    const args = Buffer.from((await module.buildBootstrapArgs(context)).bytes);
    assert.equal(args.length, 432);
    assert.equal(args.readUInt32LE(296), 18);
    assert.equal(args.subarray(300, 396).toString().split("\0")[0],
        "@Sdcard:/iz2s/core/bin/core/exefs.nsp");
    assert.equal(args.readUInt32LE(396), 19);
    assert.equal(args.readUInt32LE(400), 20);
    assert.equal(args.readUInt32LE(404), 21);
    assert(args.subarray(408).every(byte => byte === 0));
    assert.equal(args.readBigUInt64LE(264), 0x100000n + 0x142180n);
});

test("promotion must succeed before IZ2S; stale sessions and repeat boots cannot launch", async () => {
    const mocks = {
        "./core.js": "export async function run(){calls.push('fs');if(success)globalThis.fsKaihen={domainSession:{handle:1}}}",
        "./iz2s.js": "export async function run(){calls.push('iz2s')}",
        "./core/runtime.mjs": "export default {log:async()=>{}}"
    };
    const plugin = {name: "chain-fixture", setup(build) {
        build.onResolve({filter: /^\.\/(core\.js|iz2s\.js|core\/runtime\.mjs)$/},
            args => ({path: args.path, namespace: "fixture"}));
        build.onLoad({filter: /.*/, namespace: "fixture"}, args => ({contents: mocks[args.path]}));
    }};
    for (const success of [false, true]) {
        const calls = [];
        const module = await moduleAt("src/chain.mjs",
            {calls, success, fsKaihen: {domainSession: {stale: true}}}, [plugin]);
        await module.run();
        assert.deepEqual(calls, success ? ["fs", "iz2s"] : ["fs"]);
    }
    const calls = [];
    const module = await moduleAt("src/chain.mjs",
        {calls, success: true, __KH: {stages: {launch: {}}}}, [plugin]);
    await assert.rejects(module.run(), /restart the console/);
    assert.deepEqual(calls, []);
});

test("default and compatibility payloads cannot boot stock kaiHEN", async () => {
    for (const entry of ["src/chain.mjs", "src/chain-sd.mjs", "src/kaihen.js"]) {
        const built = await buildPayload({entry: path.join(ROOT, entry)});
        assert(built.bytes < MAX_PAYLOAD_BYTES);
        assert.match(built.payload, /@Sdcard:\/iz2s\/core\/bin\/core\/exefs\.nsp/);
        assert.doesNotMatch(built.payload, /\/kaihen\/|\/hbmenu\.nro|HEN IS RUNNING|starting kaiHEN/);
    }
});

test("readable persistent image fits save and loader limits and round-trips", async () => {
    const result = await buildPersistentImage({chain: true});
    assert(result.image.bytes < 200000);
    assert(Buffer.byteLength(result.bootstrap) < MAX_PAYLOAD_BYTES);
    assert.equal(result.image.minifyPayload, false);
    const encoded = result.image.stage.match(/B64\("([A-Za-z0-9+/=]+)"\)/)[1];
    assert.equal(inflateRawSync(Buffer.from(encoded, "base64")).toString("ascii"), result.standalone);
    assert.doesNotMatch(result.standalone, /\/kaihen\/|\/hbmenu\.nro|HEN IS RUNNING/);
    assert.match(result.standalone, /IZ2s 0\.3\.2 by IZMYSH/);
    assert.doesNotMatch(result.standalone, /CivSwitch 1\.0 by Gezine|IZCIV 0\.1 by IZMYSH/);
});
