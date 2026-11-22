/* SPDX-License-Identifier: MIT; copyright (c) 2026 Gezine. */
/* SDK-backed file operations. Offsets and entry words belong to the supplied SDK.
 * No operation starts until every fingerprint is checked. Never call v8Version.
 */
window.__cfsCreateSaveFS = function (c) {
    var profile = {
        create: [681728, [706675682, 336922295]],
        open: [
            686240,
            [
                3506537471, 2835512317, 2835570680, 2835634166, 2835697652,
                2432730109, 704775157, 2852193270
            ]
        ],
        close: [
            678512,
            [
                2847833085, 2835435508, 2432697341, 2852127731, 2484406384,
                905970400, 2853372896, 2484406385
            ]
        ],
        write: [
            679424,
            [
                3506521087, 2835446781, 2835505144, 2835568630, 2835632116,
                2432713725, 2852389879, 2852324340
            ]
        ],
        commit: [687584, [4026548513, 2434409505, 335544322]],
        mkdir: [
            682128,
            [
                3506521087, 2835512317, 2835568630, 2835632116, 2432730109,
                2852127731, 2484405478, 905970464
            ]
        ],
        entry: [
            685040,
            [
                3506553855, 2835577853, 2835636216, 2835699702, 2835763188,
                2432746493, 2852193267, 2852127732
            ]
        ],
        read: [
            679280,
            [
                3506488319, 2835446781, 2835501044, 2432713725, 2852127720,
                3506443173, 3089089471, 2852193248
            ]
        ],
        size: [
            680224,
            [
                2847702013, 4177529847, 2835503094, 2835566580, 2432697341,
                2852193268, 2852127734, 2484405953
            ]
        ],
        space: [
            685520,
            [
                3506521087, 2835446781, 4177531895, 2835568630, 2835632116,
                2432713725, 1384890455, 1923089911
            ]
        ],
        rename: [
            683664,
            [
                3506553855, 2835512317, 4177533945, 2835636216, 2835699702,
                2835763188, 2432730109, 2852193267
            ]
        ],
        delete: [
            681744,
            [
                3506521087, 2835512317, 2835568630, 2835632116, 2432730109,
                2852127731, 2484405574, 905970464
            ]
        ]
    };
    Object.keys(profile).forEach(function (name) {
        var item = profile[name];
        item[1].forEach(function (word, index) {
            if (
                Number(c.read32(c.sdk_base + BigInt(item[0] + index * 4))) !==
                word
            )
                throw new Error('Incompatible SDK: FS ' + name);
        });
    });
    var allocations = [],
        disposed = false;
    function alloc(size) {
        var pointer = c.malloc(size + 8);
        if (pointer === 0n) throw new Error('FS allocation failed');
        allocations.push(pointer);
        return pointer;
    }
    function dispose() {
        if (disposed) return;
        disposed = true;
        allocations.reverse().forEach(function (pointer) {
            c.free(pointer);
        });
        allocations = [];
    }
    var path1, path2, result, buffer, option;
    try {
        path1 = alloc(1024);
        path2 = alloc(1024);
        result = alloc(8);
        buffer = alloc(65536);
        option = alloc(8);
        c.write32(option, 1n); // WriteOption::Flush, passed by reference.
    } catch (error) {
        dispose();
        throw error;
    }
    function path(pointer, name) {
        if (
            typeof name !== 'string' ||
            (name !== 'save' && !/^save:(\/[-a-zA-Z0-9._]+)*\/?$/.test(name)) ||
            name.length >= 1024
        )
            throw new Error('Invalid save path');
        var bytes = window.__cfsSaveCodec.ascii(name);
        c.write_buffer(pointer, bytes);
        c.write8(pointer + BigInt(bytes.length), 0n);
        return pointer;
    }
    function call(name, args) {
        if (disposed) throw new Error('FS closed');
        return c.call_rop.apply(
            null,
            [c.sdk_base + BigInt(profile[name][0])].concat(args)
        );
    }
    function check(name, args) {
        var code = call(name, args) & 0xffffffffn;
        if (code !== 0n) throw new Error(name + ' : 0x' + code.toString(16));
    }
    function pause() {
        return new Promise(function (resolve) {
            setTimeout(resolve, 0);
        });
    }
    function exists(name) {
        var code = call('entry', [result, path(path1, name)]) & 0xffffffffn;
        if (code === 0x202n) return false; // PathNotFound, observed on this SDK.
        if (code !== 0n)
            throw new Error('GetEntryType : 0x' + code.toString(16));
        return true;
    }
    function open(name, mode) {
        check('open', [result, path(path1, name), BigInt(mode)]);
        var handle = c.read64(result);
        if (handle === 0n) throw new Error('Invalid file handle');
        return handle;
    }
    return {
        exists: exists,
        read: async function (name) {
            var handle = open(name, 1);
            try {
                check('size', [result, handle]);
                var size = Number(c.read64(result));
                if (
                    !Number.isSafeInteger(size) ||
                    size < 1 ||
                    size > window.__cfsSaveCodec.maximum
                )
                    throw new Error('Invalid save size');
                var out = new Uint8Array(size);
                for (var offset = 0; offset < size; offset += 65536) {
                    var count = Math.min(65536, size - offset);
                    check('read', [
                        result,
                        handle,
                        BigInt(offset),
                        buffer,
                        BigInt(count)
                    ]);
                    if (c.read64(result) !== BigInt(count))
                        throw new Error('Incomplete save read');
                    out.set(c.read_buffer(buffer, count), offset);
                    await pause();
                }
                return out;
            } finally {
                call('close', [handle]);
            } // CloseFile returns void.
        },
        writeNew: async function (name, bytes) {
            check('create', [path(path1, name), BigInt(bytes.length)]);
            var handle = open(name, 2);
            try {
                for (var offset = 0; offset < bytes.length; offset += 65536) {
                    var part = bytes.subarray(offset, offset + 65536);
                    c.write_buffer(buffer, part);
                    check('write', [
                        handle,
                        BigInt(offset),
                        buffer,
                        BigInt(part.length),
                        option
                    ]);
                    await pause();
                }
            } finally {
                call('close', [handle]);
            }
        },
        writeAt: async function (name, offset, bytes) {
            if (!(bytes instanceof Uint8Array) || bytes.length < 1)
                throw new Error('Invalid write data');
            if (
                !Number.isSafeInteger(offset) ||
                offset < 0 ||
                !Number.isSafeInteger(offset + bytes.length)
            )
                throw new Error('Invalid file offset');
            var handle = open(name, 3);
            try {
                for (var at = 0; at < bytes.length; at += 65536) {
                    var part = bytes.subarray(at, at + 65536);
                    c.write_buffer(buffer, part);
                    var code = call('write', [
                        handle,
                        BigInt(offset + at),
                        buffer,
                        BigInt(part.length),
                        option
                    ]) & 0xffffffffn;
                    if (code !== 0n)
                        return {
                            ok: false,
                            error: 'WriteFile : 0x' + code.toString(16)
                        };
                    await pause();
                }
                return { ok: true };
            } finally {
                call('close', [handle]);
            }
        },
        mkdir: function (name) {
            if (!exists(name)) check('mkdir', [path(path1, name)]);
            else if (Number(c.read32(result)) !== 0)
                throw new Error('Invalid staging directory');
        },
        rename: function (from, to) {
            check('rename', [path(path1, from), path(path2, to)]);
        },
        delete: function (name) {
            check('delete', [path(path1, name)]);
        },
        commit: function () {
            var code = call('commit', [path(path1, 'save')]) & 0xffffffffn;
            if (code === 0n) return { ok: true };
            return { ok: false, error: 'Commit : 0x' + code.toString(16) };
        },
        freeSpace: function () {
            check('space', [result, path(path1, 'save:/')]);
            var size = Number(c.read64(result));
            if (!Number.isSafeInteger(size) || size < 0)
                throw new Error('Invalid free space');
            return size;
        },
        dispose: dispose
    };
};
