/* SPDX-License-Identifier: MIT; copyright (c) 2026 Gezine. */
/* CIV7 trigger codec. No filesystem or native calls in this factory. */
window.__cfsSaveCodec = (function () {
    var stager =
        "<img src=x onerror=\"eval(document.body.textContent.match(/XSSP:[0-9]+:[0-9a-f]+/g).map(x=>x.split(':')[2]).join('').replace(/../g,x=>String.fromCharCode(parseInt(x,16))))\">";
    // Diagnostic only: preserve the same direct eval and hex decoder, with
    // timestamps before the DOM read and after each transformation.
    var timedStager =
        '<img src=x onerror="' +
        "window.__cfsTriggerTiming=[['onerror',Date.now()]];" +
        'var __cfsText=document.body.textContent;' +
        "window.__cfsTriggerTiming.push(['body-read',Date.now()]);" +
        "var __cfsHex=__cfsText.match(/XSSP:[0-9]+:[0-9a-f]+/g).map(x=>x.split(':')[2]).join('');" +
        "window.__cfsTriggerTiming.push(['hex-found',Date.now()]);" +
        'var __cfsCode=__cfsHex.replace(/../g,x=>String.fromCharCode(parseInt(x,16)));' +
        "window.__cfsTriggerTiming.push(['hex-decoded',Date.now()]);" +
        'eval(__cfsCode)">';
    var hiddenStart = '<span style="display:none">';
    var hiddenEnd = '</span>';
    var maximum = 4 * 1024 * 1024;

    function ascii(text) {
        if (typeof text !== 'string' || /[^\x00-\x7f]/.test(text))
            throw new Error('Non-ASCII image');
        var bytes = new Uint8Array(text.length);
        for (var i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i);
        return bytes;
    }
    function text(bytes) {
        var parts = [];
        for (var i = 0; i < bytes.length; i += 4096)
            parts.push(
                String.fromCharCode.apply(null, bytes.subarray(i, i + 4096))
            );
        return parts.join('');
    }
    function equal(a, b) {
        if (a.length !== b.length) return false;
        for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
        return true;
    }
    function scan(bytes) {
        if (
            !(bytes instanceof Uint8Array) ||
            bytes.length < 28 ||
            bytes.length > maximum ||
            text(bytes.subarray(0, 4)) !== 'CIV7'
        )
            throw new Error('Invalid CIV7 save');
        var view = new DataView(
            bytes.buffer,
            bytes.byteOffset,
            bytes.byteLength
        );
        var records = [];
        for (var i = 4; i <= bytes.length - 20; i++) {
            if (
                bytes[i] !== 0xfe ||
                bytes[i + 1] !== 0x5a ||
                bytes[i + 2] !== 0x61 ||
                bytes[i + 3] !== 0xe8
            )
                continue;
            var framing = view.getUint32(i + 12, true);
            if (
                view.getUint32(i + 4, true) !== 2 ||
                view.getUint32(i + 8, true) !== 0 ||
                (framing & 0xe0000000) !== 0x60000000 ||
                view.getUint32(i + 16, true) !== 1
            )
                throw new Error('Invalid currentTurnDate header');
            var count = framing & 0x1fffffff;
            if (
                count < 1 ||
                count > bytes.length - i - 20 ||
                bytes[i + 20 + count - 1] !== 0
            )
                throw new Error('Invalid currentTurnDate length');
            records.push({ header: i, start: i + 20, end: i + 20 + count });
        }
        if (records.length !== 1)
            throw new Error('Missing or duplicate currentTurnDate');
        var record = records[0];
        record.value = text(bytes.subarray(record.start, record.end - 1));
        return record;
    }
    function inspect(bytes) {
        var record = scan(bytes);
        var value = record.value;
        var prefix = value.indexOf(timedStager) === 0 ? timedStager : stager;
        var hidden = value.indexOf(prefix + hiddenStart + 'XSSP:0:') === 0;
        var start = prefix + (hidden ? hiddenStart : '') + 'XSSP:0:';
        var end = hidden ? hiddenEnd : '';
        var hex = value.slice(start.length, value.length - end.length);
        if (
            value.indexOf(start) !== 0 ||
            (end && value.slice(-end.length) !== end) ||
            !/^[0-9a-f]+$/.test(hex) ||
            hex.length % 2
        )
            throw new Error('Existing trigger unrecognized');
        return record;
    }
    // The pristine template as distributed: value exactly "4000 BCE".
    function inspectTemplate(bytes) {
        var record = scan(bytes);
        if (record.value !== '4000 BCE')
            throw new Error('Template unrecognized');
        return record;
    }
    function replace(bytes, stage, timings, hideText) {
        var record;
        try {
            record = inspect(bytes);
        } catch (ignored) {
            record = inspectTemplate(bytes);
        }
        var code = ascii(stage),
            hex = [];
        if (!code.length) throw new Error('Empty image');
        for (var i = 0; i < code.length; i++)
            hex.push(('0' + code[i].toString(16)).slice(-2));
        var value = ascii(
            (timings ? timedStager : stager) +
                (hideText ? hiddenStart : '') +
                'XSSP:0:' +
                hex.join('') +
                (hideText ? hiddenEnd : '')
        );
        var size =
            bytes.length - (record.end - record.start) + value.length + 1;
        if (size > maximum) throw new Error('Trigger too large');
        var result = new Uint8Array(size);
        result.set(bytes.subarray(0, record.start));
        result.set(value, record.start);
        result.set(bytes.subarray(record.end), record.start + value.length + 1);
        new DataView(result.buffer).setUint32(
            record.header + 12,
            0x60000000 | (value.length + 1),
            true
        );
        // Rebrand only the template's framed game-name fields, keeping their
        // byte widths and offsets unchanged. The pristine template stays intact.
        var oldName = ascii("Gezine's Game"), newName = ascii('IZ2S 0.3.2  ');
        var headers = new DataView(result.buffer);
        for (var at = 20; at + oldName.length < result.length; at++) {
            if (result[at] !== oldName[0] || result[at + oldName.length] !== 0) continue;
            if (headers.getUint32(at - 20, true) !== 0x01ee2ec7 ||
                headers.getUint32(at - 16, true) !== 2 ||
                headers.getUint32(at - 12, true) !== 0 ||
                headers.getUint32(at - 8, true) !== 0x6000000e ||
                headers.getUint32(at - 4, true) !== 1) continue;
            if (equal(result.subarray(at, at + oldName.length), oldName)) result.set(newName, at);
        }
        inspect(result);
        return result;
    }
    // SHA-256 over bytes; independent of browser crypto APIs and native callbacks.
    function createHash(bytes) {
        var constants = [
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
            0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
            0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
            0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
            0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        ];
        var h = [
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f,
            0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        ];
        var padded = new Uint8Array(Math.ceil((bytes.length + 9) / 64) * 64);
        padded.set(bytes);
        padded[bytes.length] = 128;
        var view = new DataView(padded.buffer);
        view.setUint32(
            padded.length - 8,
            Math.floor(bytes.length / 0x20000000)
        );
        view.setUint32(padded.length - 4, bytes.length * 8);
        var w = new Int32Array(64);
        function rotate(x, n) {
            return (x >>> n) | (x << (32 - n));
        }
        var offset = 0;
        function step(blocks) {
            var end = Math.min(padded.length, offset + blocks * 64);
            for (; offset < end; offset += 64) {
                for (var i = 0; i < 64; i++) {
                    if (i < 16) w[i] = view.getInt32(offset + i * 4);
                    else {
                        var x = w[i - 15],
                            y = w[i - 2];
                        w[i] =
                            w[i - 16] +
                            (rotate(x, 7) ^ rotate(x, 18) ^ (x >>> 3)) +
                            w[i - 7] +
                            (rotate(y, 17) ^ rotate(y, 19) ^ (y >>> 10));
                    }
                }
                var a = h[0],
                    b = h[1],
                    c = h[2],
                    d = h[3],
                    e = h[4],
                    f = h[5],
                    g = h[6],
                    k = h[7];
                for (i = 0; i < 64; i++) {
                    var t1 =
                        (k +
                            (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) +
                            ((e & f) ^ (~e & g)) +
                            constants[i] +
                            w[i]) |
                        0;
                    var t2 =
                        ((rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) +
                            ((a & b) ^ (a & c) ^ (b & c))) |
                        0;
                    k = g;
                    g = f;
                    f = e;
                    e = (d + t1) | 0;
                    d = c;
                    c = b;
                    b = a;
                    a = (t1 + t2) | 0;
                }
                [a, b, c, d, e, f, g, k].forEach(function (v, index) {
                    h[index] = (h[index] + v) | 0;
                });
            }
            return offset === padded.length;
        }
        return {
            step: step,
            finish: function () {
                return h
                    .map(function (v) {
                        return ('00000000' + (v >>> 0).toString(16)).slice(-8);
                    })
                    .join('');
            }
        };
    }
    function sha256(bytes) {
        var hash = createHash(bytes);
        while (!hash.step(256)) {}
        return hash.finish();
    }
    async function sha256Async(bytes) {
        var hash = createHash(bytes);
        // Hash 64 KiB per turn, matching the filesystem chunk size.
        while (!hash.step(1024))
            await new Promise(function (resolve) {
                setTimeout(resolve, 0);
            });
        return hash.finish();
    }
    return {
        ascii: ascii,
        text: text,
        equal: equal,
        scan: scan,
        inspect: inspect,
        inspectTemplate: inspectTemplate,
        replace: replace,
        sha256: sha256,
        sha256Async: sha256Async,
        stager: stager,
        maximum: maximum
    };
})();
