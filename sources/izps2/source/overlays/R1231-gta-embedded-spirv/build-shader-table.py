"""Extract validated portable stages from the historical GTA Vulkan cache packs."""
import ctypes
from hashlib import sha256
import json
from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory

here = Path(__file__).resolve().parent
series = here.parent
readback = series / 'console-cache'
packed = sorted(readback.glob('*vk_spirv_0dcf4d00.pack'))
assert len(packed) == 2
historical = json.loads((readback/'readback.json').read_text())
library = ctypes.CDLL('libxxhash.so.0')
library.XXH3_64bits.argtypes = (ctypes.c_char_p, ctypes.c_size_t)
library.XXH3_64bits.restype = ctypes.c_uint64
library.XXH3_64bits_withSeed.argtypes = (ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint64)
library.XXH3_64bits_withSeed.restype = ctypes.c_uint64
pack_header = struct.Struct('<4s4sII16sQQQ')
record_header = struct.Struct('<4sHHIIQ')

def read_pack(path):
    data = path.read_bytes()
    magic, kind, version, param, stamp, size, payload_hash, checksum = pack_header.unpack_from(data)
    assert (magic,kind,version,param,size,payload_hash) == (b'GSCP',b'1SPV',1,0,0,0)
    assert library.XXH3_64bits(data[:48],48) == checksum
    position = pack_header.size
    live = {}
    records = 0
    while position < len(data):
        magic, kind, key_len, data_len, aux, checksum = record_header.unpack_from(data,position)
        assert magic==b'GSPR' and kind==1 and key_len==24 and aux==0
        header = data[position:position+16] + bytes(8)
        position += record_header.size
        actual_size = key_len if data_len==0xffffffff else key_len+data_len
        payload = data[position:position+actual_size]
        assert len(payload)==actual_size
        seeded = library.XXH3_64bits(header,len(header))
        assert library.XXH3_64bits_withSeed(payload,len(payload),seeded)==checksum
        key = payload[:key_len]
        if data_len==0xffffffff:
            live.pop(key,None)
        else:
            live[key] = payload[key_len:]
        position += actual_size
        records += 1
    return stamp,live,records

def recorded_reference(path):
    if isinstance(historical,dict): collection=historical.values()
    else: collection=historical
    matches=[row for row in collection if isinstance(row,dict)
             and Path(row.get('path','')).name == path.name]
    assert len(matches)==1
    return matches[0]

sources={}
modules={}
stamp=None
for path in packed:
    ref=recorded_reference(path)
    digest=sha256(path.read_bytes()).hexdigest()
    assert digest==ref['sha256']
    actual,live,count=read_pack(path)
    assert stamp is None or actual==stamp
    stamp=actual
    sources[str(path)]={'sha256':digest,'records':count,'live':len(live)}
    for key,data in live.items():
        assert key not in modules or modules[key]==data
        modules[key]=data

# The immutable cards contain only Vulkan 1.0 shader module inputs.
# Recheck instruction framing and the entry point's execution model before packing.
program=bytearray()
items=[]
counts={}
with TemporaryDirectory() as tmp:
    tmp_path=Path(tmp)/'module.spv'
    for key,data in sorted(modules.items()):
        length,stage=struct.unpack_from('<II',key,16)
        assert 0 < length <= 1<<20 and stage in (0,1,2,3,4,5)
        assert 20 <= len(data) <= 1<<18 and len(data)%4==0
        words=struct.unpack(f'<{len(data)//4}I',data)
        assert words[0]==0x07230203 and words[1]==0x00010000
        pc=5
        models=[]
        while pc<len(words):
            count,op=words[pc]>>16,words[pc]&0xffff
            assert count and pc+count<=len(words)
            if op==15:
                assert count>=4
                models.append(words[pc+1])
            pc+=count
        assert models and all(x=={0:0,1:4,2:5,3:3,4:1,5:2}[stage] for x in models)
        if stage not in (0,1):
            continue
        tmp_path.write_bytes(data)
        subprocess.run(['spirv-val','--target-env','vulkan1.0',tmp_path],check=True)
        items.append((key,len(program)//4,len(words)))
        counts[stage]=counts.get(stage,0)+1
        program+=data
assert counts[0] > 0 and counts[1] > 0
blob=here/'spirv-shader-words.bin'
blob.write_bytes(program)
def literal(data):
    return ', '.join(f'0x{byte:02x}' for byte in data)
rows=''.join(f'    {{ {{{literal(key)}}}, {offset}, {count} }},\n'
             for key,offset,count in items)
(here/'spirv-shader-table.h').write_text(
    '#pragma once\n\n'
    f'static constexpr GSCacheFile::Digest CompilerStamp{{{{{literal(stamp)}}}}};\n'
    'static constexpr ShaderRecord ShaderRecords[] = {\n'+rows+'};\n'
    f'extern "C" const u32 izps2_embedded_shader_words[{len(program)//4}];\n'
    'asm(R"shader(.pushsection .rodata.izps2_embedded_shader_words, "a", %progbits\n'
    '.p2align 2\n.global izps2_embedded_shader_words\n.hidden izps2_embedded_shader_words\n'
    '.type izps2_embedded_shader_words, %object\nizps2_embedded_shader_words:\n'
    f'.incbin "{blob}"\n'
    '.size izps2_embedded_shader_words, . - izps2_embedded_shader_words\n'
    '.popsection)shader");\n')
audit={'sources':sources,'compilerStamp':stamp.hex(),'records':len(items),'stages':counts,
       'bytes':len(program),'sha256':sha256(program).hexdigest(),'validatedTarget':'vulkan1.0'}
(here/'shader-table.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps(audit))
