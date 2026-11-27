/* SPDX-License-Identifier: ISC
 * Original libounce-nx Switch 2 dynamic linker core.
 *
 * A module is an NRO image. The word at +4 gives its MOD0 header, and MOD0
 * gives its ELF dynamic section. NROs have no program headers, so what an
 * ELF loader reads from them comes from the blocks after MOD0 instead:
 * libnx's "LNY1" for the data that is read-only after relocation, and
 * libounce's "OTL" blocks for thread-local storage (switch_crt0.s,
 * switch2/module/module_crt0.s), and "ODH0" for the module's __dso_handle.
 * The runtime library has an "ORT0" block with its entry
 * (switch2/rtld/rtld_crt0.s).
 *
 * A module's record (DynlinkModule) is in the module's own image, in space
 * that the module's start object reserves in the BSS. MOD0's last word, the
 * "module object" field, locates the space, and the "OMR0" block gives its
 * size. The modules of a program are a list through their records, so the
 * linker allocates nothing and a program has no fixed number of modules.
 * Nintendo's linker keeps its own record of a module where the same field
 * points; the space, DYNLINK_MODULE_RECORD_SIZE, is larger than that record.
 *
 * Addresses in the dynamic section and the symbol table are relative to the
 * image's first byte. A reference is looked up in the global scope, in
 * module order, so a program's definition takes the place of a library's;
 * then in the module that has the reference and the libraries it needs.
 * Two libraries that are added as local, such as plugins, therefore do not
 * see each other's symbols.
 *
 * Thread-local storage follows AArch64's variant 1 as libounce's threads lay
 * it out: the thread pointer, then tls_start_offset bytes (the larger of 16
 * and the program's TLS alignment), then the program's block, then each
 * library's block at its alignment. A TLS_TPREL relocation gets the distance
 * from the thread pointer to its variable. Code of the general-dynamic model
 * passes __tls_get_addr two words of its GOT instead: the module's tls_id
 * (TLS_DTPMOD) and the variable's offset in the module's block (TLS_DTPREL).
 * The block of such a module need not be in the area.
 *
 * Unwinding reads a module's .eh_frame_hdr section, which MOD0 locates: a
 * table of every function's first address and unwind entry, sorted by
 * address. GNU ld writes it with --eh-frame-hdr, and always in one format,
 * the only one read here.
 */
#include <string.h>
#include "switch2/runtime/dynlink.h"

#include "nro.h"

// MOD0: its magic and six words, then extension blocks of a magic and two
// words each. Offsets are relative to MOD0.
#define MOD0_OFFSET_WORD        4
#define MOD0_HEADER_SIZE        0x1C
#define MOD0_EXTENSION_SIZE     0xC
#define MOD0_EXTENSIONS_MAX     12
#define MOD0_MAGIC_LIBNX        0x00594E4C  // "LNY", then a digit.
#define MOD0_MAGIC_RELRO        0x31594E4C  // "LNY1": start and end of the data that is read-only after relocation.
#define MOD0_MAGIC_RUNTIME      0x3054524F  // "ORT0": the runtime library's entry.
#define MOD0_MAGIC_DSO_HANDLE   0x3048444F  // "ODH0": the module's __dso_handle.
#define MOD0_MAGIC_RECORD       0x30524D4F  // "OMR0": the size of the space for the module's record.
#define MOD0_MAGIC_TLS_TEMPLATE 0x304C544F  // "OTL0": the initial TLS bytes' start and end.
#define MOD0_MAGIC_TLS_BLOCK    0x314C544F  // "OTL1": start and end of a block of the TLS size.
#define MOD0_MAGIC_TLS_ALIGN    0x324C544F  // "OTL2": the word that holds the TLS alignment.

#define TLS_TCB_SIZE            16

// DT_RELR and its size, for a toolchain whose elf.h has no names for them.
#ifndef DT_RELR
#define DT_RELRSZ 35
#define DT_RELR   36
#endif

typedef struct {
    u32 magic;
    s32 dynamic_offset;
    s32 bss_start_offset;
    s32 bss_end_offset;
    s32 eh_frame_hdr_start_offset;
    s32 eh_frame_hdr_end_offset;
    s32 module_object_offset;
} Mod0Header;

typedef struct {
    u32 magic;
    s32 first;
    s32 second;
} Mod0Extension;

static Result _dynlinkBadImage(void) {
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

// Whether [offset, offset + size) is inside an image of image_size bytes.
static bool _dynlinkIsInside(size_t image_size, u64 offset, u64 size) {
    return offset <= image_size && size <= image_size - offset;
}

// Reads the blocks after MOD0. Of the TLS block only its size is used: it is
// in the BSS, which a file image does not have.
static void _dynlinkReadBlocks(DynlinkModule* m, const u8* mod0, size_t mod0_offset, size_t image_size) {
    for (u32 i = 0; i < MOD0_EXTENSIONS_MAX; i++) {
        const size_t block_offset = MOD0_HEADER_SIZE + i * MOD0_EXTENSION_SIZE;
        if (!_dynlinkIsInside(image_size, mod0_offset + block_offset, MOD0_EXTENSION_SIZE))
            return;

        Mod0Extension block;
        memcpy(&block, mod0 + block_offset, sizeof(block));
        if (block.magic == MOD0_MAGIC_RELRO) {
            m->relro = (void*)(mod0 + block.first);
            m->relro_size = (size_t)(block.second - block.first);
        } else if (block.magic == MOD0_MAGIC_RUNTIME) {
            m->runtime_entry = (uintptr_t)(mod0 + block.first);
        } else if (block.magic == MOD0_MAGIC_DSO_HANDLE) {
            m->dso_handle = (void*)(mod0 + block.first);
        } else if (block.magic == MOD0_MAGIC_TLS_TEMPLATE) {
            m->tls_template = mod0 + block.first;
            m->tls_template_size = (size_t)(block.second - block.first);
        } else if (block.magic == MOD0_MAGIC_TLS_BLOCK) {
            m->tls_size = (size_t)(block.second - block.first);
        } else if (block.magic == MOD0_MAGIC_TLS_ALIGN) {
            const u64 align_offset = mod0_offset + (s64)block.first;
            if (_dynlinkIsInside(image_size, align_offset, sizeof(u64))) {
                u64 align;
                memcpy(&align, mod0 + block.first, sizeof(align));
                m->tls_align = (size_t)align;
            }
        } else if ((block.magic & 0x00FFFFFF) != MOD0_MAGIC_LIBNX && block.magic != NROOUNCE_MAGIC &&
                   block.magic != MOD0_MAGIC_RECORD) {
            // Code follows the last block.
            return;
        }
    }
}

// The number of symbols that a GNU hash table reaches: the highest chain
// index, then to the end of that chain.
static u32 _dynlinkGnuSymbolCount(const u32* table) {
    const u32 bucket_count = table[0];
    const u32 symbol_offset = table[1];
    const u32 bloom_size = table[2];
    const u32* buckets = table + 4 + bloom_size * 2;
    const u32* chains = buckets + bucket_count;

    u32 last = 0;
    for (u32 i = 0; i < bucket_count; i++) {
        if (buckets[i] > last)
            last = buckets[i];
    }
    if (last < symbol_offset)
        return symbol_offset;
    while (!(chains[last - symbol_offset] & 1))
        last++;
    return last + 1;
}

Result dynlinkModuleOpen(DynlinkModule* m, const void* image, size_t size) {
    memset(m, 0, sizeof(*m));
    const u8* bytes = (const u8*)image;
    if (!_dynlinkIsInside(size, MOD0_OFFSET_WORD, sizeof(u32)))
        return _dynlinkBadImage();

    u32 mod0_offset;
    memcpy(&mod0_offset, bytes + MOD0_OFFSET_WORD, sizeof(mod0_offset));
    if (!_dynlinkIsInside(size, mod0_offset, MOD0_HEADER_SIZE))
        return _dynlinkBadImage();

    const u8* mod0 = bytes + mod0_offset;
    Mod0Header header;
    memcpy(&header, mod0, sizeof(header));
    const u64 dynamic_offset = (u64)mod0_offset + (s64)header.dynamic_offset;
    if (header.magic != NROMOD0_MAGIC || !_dynlinkIsInside(size, dynamic_offset, sizeof(Elf64_Dyn)))
        return _dynlinkBadImage();

    m->base = (uintptr_t)image;
    m->size = size;
    m->dynamic = (const Elf64_Dyn*)(bytes + dynamic_offset);
    const s64 eh_frame_hdr_size = (s64)header.eh_frame_hdr_end_offset - header.eh_frame_hdr_start_offset;
    const u64 eh_frame_hdr_offset = (u64)mod0_offset + (s64)header.eh_frame_hdr_start_offset;
    if (eh_frame_hdr_size > 0 && _dynlinkIsInside(size, eh_frame_hdr_offset, (u64)eh_frame_hdr_size)) {
        m->eh_frame_hdr = bytes + eh_frame_hdr_offset;
        m->eh_frame_hdr_size = (size_t)eh_frame_hdr_size;
    }
    m->tls_align = 1;
    _dynlinkReadBlocks(m, mod0, mod0_offset, size);

    u64 soname_offset = 0;
    bool has_soname = false;
    size_t rela_size = 0, jmprel_size = 0, relr_size = 0, init_size = 0, fini_size = 0;
    for (const Elf64_Dyn* entry = m->dynamic; entry->d_tag != DT_NULL; entry++) {
        if (!_dynlinkIsInside(size, (const u8*)(entry + 1) - bytes, sizeof(Elf64_Dyn)))
            return _dynlinkBadImage();

        const u64 value = entry->d_un.d_val;
        switch (entry->d_tag) {
            case DT_SONAME:       soname_offset = value; has_soname = true; break;
            case DT_NEEDED:       m->needed_count++; break;
            case DT_SYMTAB:       m->symtab = (const Elf64_Sym*)(bytes + value); break;
            case DT_STRTAB:       m->strtab = (const char*)(bytes + value); break;
            case DT_STRSZ:        m->strtab_size = value; break;
            case DT_HASH:         m->sysv_hash = (const u32*)(bytes + value); break;
            case DT_GNU_HASH:     m->gnu_hash = (const u32*)(bytes + value); break;
            case DT_RELA:         m->rela = (const Elf64_Rela*)(bytes + value); break;
            case DT_RELASZ:       rela_size = value; break;
            case DT_JMPREL:       m->jmprel = (const Elf64_Rela*)(bytes + value); break;
            case DT_PLTRELSZ:     jmprel_size = value; break;
            case DT_RELR:         m->relr = (const Elf64_Relr*)(bytes + value); break;
            case DT_RELRSZ:       relr_size = value; break;
            case DT_INIT_ARRAY:   m->init_array = (void (**)(void))(bytes + value); break;
            case DT_INIT_ARRAYSZ: init_size = value; break;
            case DT_FINI_ARRAY:   m->fini_array = (void (**)(void))(bytes + value); break;
            case DT_FINI_ARRAYSZ: fini_size = value; break;
            default: break;
        }
    }
    if (!m->symtab || !m->strtab || (!m->sysv_hash && !m->gnu_hash))
        return _dynlinkBadImage();

    m->rela_count = rela_size / sizeof(Elf64_Rela);
    m->jmprel_count = jmprel_size / sizeof(Elf64_Rela);
    m->relr_count = relr_size / sizeof(Elf64_Relr);
    m->init_count = init_size / sizeof(void*);
    m->fini_count = fini_size / sizeof(void*);
    for (size_t i = 0; i < m->rela_count && !m->needs_static_tls; i++)
        m->needs_static_tls = ELF64_R_TYPE(m->rela[i].r_info) == R_AARCH64_TLS_TPREL;
    for (size_t i = 0; i < m->jmprel_count && !m->needs_static_tls; i++)
        m->needs_static_tls = ELF64_R_TYPE(m->jmprel[i].r_info) == R_AARCH64_TLS_TPREL;
    // The SysV table's chain count is the symbol count.
    m->symbol_count = m->sysv_hash ? m->sysv_hash[1] : _dynlinkGnuSymbolCount(m->gnu_hash);

    if (has_soname) {
        if (soname_offset >= m->strtab_size)
            return _dynlinkBadImage();
        m->soname = m->strtab + soname_offset;
    }
    for (u32 i = 0; i < m->needed_count; i++) {
        if (!dynlinkModuleGetNeeded(m, i))
            return _dynlinkBadImage();
    }
    return 0;
}

Result dynlinkModuleOpenInImage(void* image, size_t size, DynlinkModule** out_module) {
    const u8* bytes = (const u8*)image;
    if (!_dynlinkIsInside(size, MOD0_OFFSET_WORD, sizeof(u32)))
        return _dynlinkBadImage();

    u32 mod0_offset;
    memcpy(&mod0_offset, bytes + MOD0_OFFSET_WORD, sizeof(mod0_offset));
    if (!_dynlinkIsInside(size, mod0_offset, MOD0_HEADER_SIZE))
        return _dynlinkBadImage();

    Mod0Header header;
    memcpy(&header, bytes + mod0_offset, sizeof(header));
    if (header.magic != NROMOD0_MAGIC || !header.module_object_offset)
        return _dynlinkBadImage();

    // The size of the record space is in the "OMR0" block.
    size_t record_size = 0;
    for (u32 i = 0; i < MOD0_EXTENSIONS_MAX && !record_size; i++) {
        const size_t block_offset = mod0_offset + MOD0_HEADER_SIZE + i * MOD0_EXTENSION_SIZE;
        if (!_dynlinkIsInside(size, block_offset, MOD0_EXTENSION_SIZE))
            break;
        Mod0Extension block;
        memcpy(&block, bytes + block_offset, sizeof(block));
        if (block.magic == MOD0_MAGIC_RECORD)
            record_size = (size_t)block.first;
    }
    const u64 record_offset = (u64)mod0_offset + (s64)header.module_object_offset;
    if (record_size < sizeof(DynlinkModule) || !_dynlinkIsInside(size, record_offset, record_size) ||
        record_offset % _Alignof(DynlinkModule))
        return _dynlinkBadImage();

    DynlinkModule* m = (DynlinkModule*)((u8*)image + record_offset);
    *out_module = m;
    return dynlinkModuleOpen(m, image, size);
}

const char* dynlinkModuleGetNeeded(const DynlinkModule* m, u32 index) {
    for (const Elf64_Dyn* entry = m->dynamic; entry->d_tag != DT_NULL; entry++) {
        if (entry->d_tag != DT_NEEDED)
            continue;
        if (index--)
            continue;
        return entry->d_un.d_val < m->strtab_size ? m->strtab + entry->d_un.d_val : NULL;
    }
    return NULL;
}

static u32 _dynlinkSysvHash(const char* name) {
    u32 hash = 0;
    for (const u8* c = (const u8*)name; *c; c++) {
        hash = (hash << 4) + *c;
        const u32 high = hash & 0xF0000000;
        if (high)
            hash ^= high >> 24;
        hash &= ~high;
    }
    return hash;
}

static u32 _dynlinkGnuHash(const char* name) {
    u32 hash = 5381;
    for (const u8* c = (const u8*)name; *c; c++)
        hash = hash * 33 + *c;
    return hash;
}

// A definition that other modules can bind to.
static bool _dynlinkIsExported(const Elf64_Sym* symbol) {
    const u8 bind = ELF64_ST_BIND(symbol->st_info);
    const u8 type = ELF64_ST_TYPE(symbol->st_info);
    const u8 visibility = ELF64_ST_VISIBILITY(symbol->st_other);
    return symbol->st_shndx != SHN_UNDEF &&
           (bind == STB_GLOBAL || bind == STB_WEAK) &&
           type != STT_SECTION && type != STT_FILE &&
           (visibility == STV_DEFAULT || visibility == STV_PROTECTED);
}

static const Elf64_Sym* _dynlinkFindWithGnuHash(const DynlinkModule* m, const char* name) {
    const u32* table = m->gnu_hash;
    const u32 bucket_count = table[0];
    const u32 symbol_offset = table[1];
    const u32 bloom_size = table[2];
    const u32 bloom_shift = table[3];
    const u64* bloom = (const u64*)(table + 4);
    const u32* buckets = table + 4 + bloom_size * 2;
    const u32* chains = buckets + bucket_count;
    if (!bucket_count || !bloom_size)
        return NULL;

    const u32 hash = _dynlinkGnuHash(name);
    const u64 word = bloom[(hash / 64) % bloom_size];
    const u64 mask = (1ULL << (hash % 64)) | (1ULL << ((hash >> bloom_shift) % 64));
    if ((word & mask) != mask)
        return NULL;

    u32 index = buckets[hash % bucket_count];
    if (index < symbol_offset)
        return NULL;
    for (;; index++) {
        const u32 chain_hash = chains[index - symbol_offset];
        const Elf64_Sym* symbol = &m->symtab[index];
        if ((chain_hash | 1) == (hash | 1) && !strcmp(m->strtab + symbol->st_name, name) &&
            _dynlinkIsExported(symbol))
            return symbol;
        if (chain_hash & 1)
            return NULL;
    }
}

static const Elf64_Sym* _dynlinkFindWithSysvHash(const DynlinkModule* m, const char* name) {
    const u32 bucket_count = m->sysv_hash[0];
    const u32 chain_count = m->sysv_hash[1];
    const u32* buckets = m->sysv_hash + 2;
    const u32* chains = buckets + bucket_count;
    if (!bucket_count)
        return NULL;

    for (u32 index = buckets[_dynlinkSysvHash(name) % bucket_count];
         index != STN_UNDEF && index < chain_count; index = chains[index]) {
        const Elf64_Sym* symbol = &m->symtab[index];
        if (!strcmp(m->strtab + symbol->st_name, name) && _dynlinkIsExported(symbol))
            return symbol;
    }
    return NULL;
}

const Elf64_Sym* dynlinkModuleFindSymbol(const DynlinkModule* m, const char* name) {
    if (m->gnu_hash)
        return _dynlinkFindWithGnuHash(m, name);
    return _dynlinkFindWithSysvHash(m, name);
}

void dynlinkProgramCreate(DynlinkProgram* p) {
    memset(p, 0, sizeof(*p));
}

void dynlinkProgramAddModule(DynlinkProgram* p, DynlinkModule* m, bool is_global) {
    m->is_global = is_global;
    m->next = NULL;
    if (p->last)
        p->last->next = m;
    else
        p->first = m;
    p->last = m;
    p->module_count++;
    m->tls_id = ++p->last_tls_id;
}

void dynlinkProgramRemoveModule(DynlinkProgram* p, DynlinkModule* m) {
    DynlinkModule* previous = NULL;
    for (DynlinkModule* current = p->first; current; previous = current, current = current->next) {
        if (current != m)
            continue;
        if (previous)
            previous->next = m->next;
        else
            p->first = m->next;
        if (p->last == m)
            p->last = previous;
        p->module_count--;
        m->next = NULL;
        return;
    }
}

DynlinkModule* dynlinkProgramFindModule(const DynlinkProgram* p, const char* soname) {
    if (!soname)
        return NULL;
    for (DynlinkModule* m = p->first; m; m = m->next) {
        if (m->soname && !strcmp(m->soname, soname))
            return m;
    }
    return NULL;
}

const Elf64_Sym* dynlinkProgramFindSymbol(const DynlinkProgram* p, const char* name, const DynlinkModule** out_module) {
    for (const DynlinkModule* m = p->first; m; m = m->next) {
        const Elf64_Sym* symbol = m->is_global ? dynlinkModuleFindSymbol(m, name) : NULL;
        if (symbol) {
            if (out_module) *out_module = m;
            return symbol;
        }
    }
    return NULL;
}

const Elf64_Sym* dynlinkProgramFindSymbolAfter(const DynlinkProgram* p, const DynlinkModule* m, const char* name,
                                               const DynlinkModule** out_module) {
    NX_IGNORE_ARG(p);
    for (const DynlinkModule* current = m->next; current; current = current->next) {
        const Elf64_Sym* symbol = current->is_global ? dynlinkModuleFindSymbol(current, name) : NULL;
        if (symbol) {
            if (out_module) *out_module = current;
            return symbol;
        }
    }
    return NULL;
}

DynlinkModule* dynlinkProgramFindModuleByAddress(const DynlinkProgram* p, uintptr_t address) {
    for (DynlinkModule* m = p->first; m; m = m->next) {
        if (address >= m->base && address - m->base < m->size)
            return m;
    }
    return NULL;
}

// Chains a module and the libraries it needs, each one time: the module,
// then what it needs, then what those need. The chain starts at the module
// and goes through search_next; it lasts until the next call. A module that
// this search has reached carries the search's number.
static void _dynlinkQueueDependencies(DynlinkProgram* p, DynlinkModule* m) {
    const u64 mark = ++p->search_mark;
    DynlinkModule* queue_last = m;
    m->search_mark = mark;
    m->search_next = NULL;

    for (DynlinkModule* current = m; current; current = current->search_next) {
        for (u32 i = 0; i < current->needed_count; i++) {
            DynlinkModule* needed = dynlinkProgramFindModule(p, dynlinkModuleGetNeeded(current, i));
            if (!needed || needed->search_mark == mark)
                continue;
            needed->search_mark = mark;
            needed->search_next = NULL;
            queue_last->search_next = needed;
            queue_last = needed;
        }
    }
}

// Finds a symbol in the chain that _dynlinkQueueDependencies made for a module.
static const Elf64_Sym* _dynlinkFindInQueue(const DynlinkModule* m, const char* name, const DynlinkModule** out_module) {
    for (const DynlinkModule* current = m; current; current = current->search_next) {
        const Elf64_Sym* symbol = dynlinkModuleFindSymbol(current, name);
        if (symbol) {
            if (out_module) *out_module = current;
            return symbol;
        }
    }
    return NULL;
}

const Elf64_Sym* dynlinkProgramFindSymbolFrom(DynlinkProgram* p, DynlinkModule* m, const char* name,
                                              const DynlinkModule** out_module) {
    _dynlinkQueueDependencies(p, m);
    return _dynlinkFindInQueue(m, name, out_module);
}

void dynlinkProgramMakeGlobal(DynlinkProgram* p, DynlinkModule* m) {
    _dynlinkQueueDependencies(p, m);
    for (DynlinkModule* current = m; current; current = current->search_next)
        current->is_global = true;
}

static size_t _dynlinkAlignUp(size_t value, size_t align) {
    return (value + align - 1) & ~(align - 1);
}

void dynlinkProgramLayoutTls(DynlinkProgram* p) {
    p->tls_start_offset = TLS_TCB_SIZE;
    p->tls_size = 0;
    p->tls_align = TLS_TCB_SIZE;
    if (!p->first)
        return;

    // The program's code has the distance from the thread pointer to its
    // block built in: the larger of the control block's size and its alignment.
    const size_t program_align = p->first->tls_align;
    if (program_align > p->tls_start_offset)
        p->tls_start_offset = program_align;

    for (DynlinkModule* m = p->first; m; m = m->next) {
        const size_t align = m->tls_align ? m->tls_align : 1;
        m->tls_offset = _dynlinkAlignUp(p->tls_size, align);
        m->is_tls_static = true;
        p->tls_size = m->tls_offset + m->tls_size;
        if (align > p->tls_align)
            p->tls_align = align;
    }
}

// What a relocation's symbol resolves to.
typedef struct {
    const DynlinkModule* module;    // The module that defines it. NULL for an undefined weak symbol.
    const Elf64_Sym* symbol;
} DynlinkDefinition;

// A reference binds inside its own module when the symbol is local or not
// visible to others. Every other reference takes the first definition in
// the global scope, and without one, the first in the module's own chain of
// dependencies, which dynlinkProgramRelocate made.
static bool _dynlinkResolve(const DynlinkProgram* p, const DynlinkModule* m, u32 symbol_index,
                            DynlinkDefinition* out, const char** out_name) {
    const Elf64_Sym* reference = &m->symtab[symbol_index];
    const u8 visibility = ELF64_ST_VISIBILITY(reference->st_other);
    if (reference->st_shndx != SHN_UNDEF &&
        (ELF64_ST_BIND(reference->st_info) == STB_LOCAL || visibility == STV_HIDDEN ||
         visibility == STV_INTERNAL || visibility == STV_PROTECTED)) {
        out->module = m;
        out->symbol = reference;
        return true;
    }

    const char* name = m->strtab + reference->st_name;
    out->symbol = dynlinkProgramFindSymbol(p, name, &out->module);
    if (out->symbol)
        return true;
    // A module of the global scope has all it needs there.
    if (!m->is_global) {
        out->symbol = _dynlinkFindInQueue(m, name, &out->module);
        if (out->symbol)
            return true;
    }
    if (ELF64_ST_BIND(reference->st_info) == STB_WEAK) {
        out->module = NULL;
        return true;
    }
    *out_name = name;
    return false;
}

static u64 _dynlinkSymbolAddress(const DynlinkDefinition* definition) {
    if (!definition->module)
        return 0;
    if (definition->symbol->st_shndx == SHN_ABS)
        return definition->symbol->st_value;
    return definition->module->base + definition->symbol->st_value;
}

static Result _dynlinkApplyRela(const DynlinkProgram* p, const DynlinkModule* m, const Elf64_Rela* rela,
                                size_t count, const char** out_symbol) {
    for (; count--; rela++) {
        const u32 type = ELF64_R_TYPE(rela->r_info);
        const u32 symbol_index = ELF64_R_SYM(rela->r_info);
        u64* target = (u64*)(m->base + rela->r_offset);

        if (type == R_AARCH64_NONE)
            continue;
        if (type == R_AARCH64_RELATIVE) {
            if (!m->is_self_relocated)
                *target = m->base + rela->r_addend;
            continue;
        }

        DynlinkDefinition definition = { m, NULL };
        if (symbol_index && !_dynlinkResolve(p, m, symbol_index, &definition, out_symbol))
            return MAKERESULT(Module_Libnx, LibnxError_NotFound);

        switch (type) {
            case R_AARCH64_ABS64:
            case R_AARCH64_GLOB_DAT:
            case R_AARCH64_JUMP_SLOT:
                *target = _dynlinkSymbolAddress(&definition) + rela->r_addend;
                break;

            case R_AARCH64_TLS_TPREL: {
                // An undefined weak thread-local variable has no address to
                // give, and a block outside the TLS area has no fixed offset.
                if (!definition.module || !definition.module->is_tls_static)
                    return MAKERESULT(Module_Libnx, LibnxError_BadReloc);
                const u64 value = definition.symbol ? definition.symbol->st_value : 0;
                *target = p->tls_start_offset + definition.module->tls_offset + value + rela->r_addend;
                break;
            }

            case R_AARCH64_TLS_DTPMOD:
                if (!definition.module)
                    return MAKERESULT(Module_Libnx, LibnxError_BadReloc);
                *target = definition.module->tls_id;
                break;

            case R_AARCH64_TLS_DTPREL:
                *target = (definition.symbol ? definition.symbol->st_value : 0) + rela->r_addend;
                break;

            // TLS descriptors, copy relocations and indirect functions are
            // not supported: see dynlink.h.
            default:
                return MAKERESULT(Module_Libnx, LibnxError_BadReloc);
        }
    }
    return 0;
}

static void _dynlinkApplyRelr(const DynlinkModule* m) {
    u64* where = NULL;
    const Elf64_Relr* relr = m->relr;
    for (size_t count = m->relr_count; count--; relr++) {
        if (!(*relr & 1)) {
            where = (u64*)(m->base + *relr);
            *where++ += m->base;
            continue;
        }
        for (u64 bitmap = *relr >> 1, bit = 0; bitmap; bitmap >>= 1, bit++) {
            if (bitmap & 1)
                where[bit] += m->base;
        }
        where += 63;
    }
}

Result dynlinkProgramRelocate(DynlinkProgram* p, const char** out_symbol) {
    const char* missing = NULL;
    Result rc = 0;
    for (DynlinkModule* m = p->first; m && R_SUCCEEDED(rc); m = m->next) {
        if (m->is_relocated)
            continue;

        if (!m->is_global)
            _dynlinkQueueDependencies(p, m);
        if (!m->is_self_relocated)
            _dynlinkApplyRelr(m);
        rc = _dynlinkApplyRela(p, m, m->rela, m->rela_count, &missing);
        if (R_SUCCEEDED(rc))
            rc = _dynlinkApplyRela(p, m, m->jmprel, m->jmprel_count, &missing);
        if (R_SUCCEEDED(rc))
            m->is_relocated = true;
    }
    if (out_symbol) *out_symbol = missing;
    return rc;
}

//-----------------------------------------------------------------------------
// Unwind entries
//-----------------------------------------------------------------------------

// DWARF pointer encodings (DW_EH_PE_*): the low four bits give the value's
// format, the next three what it is relative to.
#define EH_FORMAT_ABSOLUTE      0x00
#define EH_FORMAT_UDATA2        0x02
#define EH_FORMAT_UDATA4        0x03
#define EH_FORMAT_UDATA8        0x04
#define EH_FORMAT_SDATA2        0x0A
#define EH_FORMAT_SDATA4        0x0B
#define EH_FORMAT_SDATA8        0x0C
#define EH_ENCODING_OMITTED     0xFF
// The encodings of the .eh_frame_hdr that GNU ld writes: the section's
// address as a 4-byte offset from the field, the entry count as 4 bytes, and
// the table as 4-byte offsets from the start of the section.
#define EH_HDR_VERSION          1
#define EH_HDR_FRAME_ENCODING   0x1B
#define EH_HDR_COUNT_ENCODING   0x03
#define EH_HDR_TABLE_ENCODING   0x3B
#define EH_HDR_TABLE_OFFSET     12

typedef struct {
    s32 function_offset;    // The function's first address, from the start of .eh_frame_hdr.
    s32 entry_offset;       // Its unwind entry, from the same place.
} EhFrameHdrEntry;

// The size of a value in a pointer encoding, or 0 for a format with no fixed size.
static size_t _dynlinkEncodedSize(u8 encoding) {
    switch (encoding & 0x0F) {
        case EH_FORMAT_ABSOLUTE: return sizeof(u64);
        case EH_FORMAT_UDATA2:
        case EH_FORMAT_SDATA2:   return sizeof(u16);
        case EH_FORMAT_UDATA4:
        case EH_FORMAT_SDATA4:   return sizeof(u32);
        case EH_FORMAT_UDATA8:
        case EH_FORMAT_SDATA8:   return sizeof(u64);
        default:                 return 0;
    }
}

// Steps over a LEB128 number.
static const u8* _dynlinkSkipLeb128(const u8* bytes) {
    while (*bytes & 0x80)
        bytes++;
    return bytes + 1;
}

// The encoding of the addresses in the unwind entries of a CIE: its 'R'
// augmentation, or absolute addresses without one. Returns
// EH_ENCODING_OMITTED for a CIE that cannot be read.
static u8 _dynlinkCieAddressEncoding(const u8* cie) {
    const u8 version = cie[8];
    const char* augmentation = (const char*)(cie + 9);
    const u8* bytes = (const u8*)augmentation + strlen(augmentation) + 1;
    if (augmentation[0] != 'z')
        return augmentation[0] == '\0' ? EH_FORMAT_ABSOLUTE : EH_ENCODING_OMITTED;

    bytes = _dynlinkSkipLeb128(bytes);                          // Code alignment.
    bytes = _dynlinkSkipLeb128(bytes);                          // Data alignment.
    bytes = version == 1 ? bytes + 1 : _dynlinkSkipLeb128(bytes); // Return address register.
    bytes = _dynlinkSkipLeb128(bytes);                          // Size of the augmentation data.

    for (const char* letter = augmentation + 1; *letter; letter++) {
        if (*letter == 'R')
            return *bytes;
        if (*letter == 'L') {
            bytes++;
        } else if (*letter == 'P') {
            // The personality routine: an encoding, then its address.
            const size_t size = _dynlinkEncodedSize(*bytes);
            if (!size)
                return EH_ENCODING_OMITTED;
            bytes += 1 + size;
        }
        // 'S', 'B' and 'G' have no data.
    }
    return EH_FORMAT_ABSOLUTE;
}

// The size of the function that an unwind entry describes, or 0 for an entry
// that cannot be read. The entry is its length, the distance back to its CIE,
// the function's address and the function's size.
static u64 _dynlinkUnwindEntryRange(const u8* entry) {
    u32 cie_distance;
    memcpy(&cie_distance, entry + 4, sizeof(cie_distance));
    const u8 encoding = _dynlinkCieAddressEncoding(entry + 4 - cie_distance);
    const size_t size = _dynlinkEncodedSize(encoding);
    if (encoding == EH_ENCODING_OMITTED || !size)
        return 0;

    u64 range = 0;
    memcpy(&range, entry + 8 + size, size);
    return range;
}

const void* dynlinkProgramFindUnwindEntry(const DynlinkProgram* p, uintptr_t address, uintptr_t* out_function) {
    const DynlinkModule* m = dynlinkProgramFindModuleByAddress(p, address);
    if (!m || m->eh_frame_hdr_size < EH_HDR_TABLE_OFFSET)
        return NULL;

    const u8* hdr = (const u8*)m->eh_frame_hdr;
    if (hdr[0] != EH_HDR_VERSION || hdr[1] != EH_HDR_FRAME_ENCODING || hdr[2] != EH_HDR_COUNT_ENCODING ||
        hdr[3] != EH_HDR_TABLE_ENCODING)
        return NULL;

    u32 count;
    memcpy(&count, hdr + 8, sizeof(count));
    if (count > (m->eh_frame_hdr_size - EH_HDR_TABLE_OFFSET) / sizeof(EhFrameHdrEntry))
        return NULL;

    // The last function that starts at or before the address.
    const u8* table = hdr + EH_HDR_TABLE_OFFSET;
    const s64 offset = (s64)(address - (uintptr_t)hdr);
    u32 low = 0;
    u32 high = count;
    while (low < high) {
        const u32 middle = low + (high - low) / 2;
        EhFrameHdrEntry candidate;
        memcpy(&candidate, table + (size_t)middle * sizeof(candidate), sizeof(candidate));
        if (candidate.function_offset <= offset)
            low = middle + 1;
        else
            high = middle;
    }
    if (!low)
        return NULL;

    EhFrameHdrEntry found;
    memcpy(&found, table + (size_t)(low - 1) * sizeof(found), sizeof(found));
    const u8* entry = hdr + found.entry_offset;
    const uintptr_t function = (uintptr_t)hdr + found.function_offset;
    if (address - function >= _dynlinkUnwindEntryRange(entry))
        return NULL;

    *out_function = function;
    return entry;
}

// Runs the constructors of the libraries a module needs, then its own. A
// module is marked before its dependencies run, so a cycle ends.
static void _dynlinkInitialize(DynlinkProgram* p, DynlinkModule* m) {
    if (m->is_initialized)
        return;
    m->is_initialized = true;

    for (u32 i = 0; i < m->needed_count; i++) {
        DynlinkModule* needed = dynlinkProgramFindModule(p, dynlinkModuleGetNeeded(m, i));
        if (needed)
            _dynlinkInitialize(p, needed);
    }

    for (size_t i = 0; i < m->init_count; i++)
        m->init_array[i]();
    m->initialized_before = p->last_initialized;
    p->last_initialized = m;
}

void dynlinkProgramRunConstructors(DynlinkProgram* p) {
    if (!p->first)
        return;
    // The libraries first, also those that the program does not need.
    for (DynlinkModule* m = p->first->next; m; m = m->next)
        _dynlinkInitialize(p, m);
    _dynlinkInitialize(p, p->first);
}

static void _dynlinkFinalize(DynlinkModule* m) {
    for (size_t i = m->fini_count; i--;)
        m->fini_array[i]();
    m->is_initialized = false;
}

void dynlinkProgramRunDestructors(DynlinkProgram* p) {
    while (p->last_initialized) {
        DynlinkModule* m = p->last_initialized;
        p->last_initialized = m->initialized_before;
        m->initialized_before = NULL;
        _dynlinkFinalize(m);
    }
}

void dynlinkProgramRunModuleDestructors(DynlinkProgram* p, DynlinkModule* m) {
    // Takes the module out of the chain of constructed modules, where it is
    // only when its constructors ran.
    DynlinkModule* after = NULL;
    for (DynlinkModule* current = p->last_initialized; current; after = current, current = current->initialized_before) {
        if (current != m)
            continue;
        if (after)
            after->initialized_before = m->initialized_before;
        else
            p->last_initialized = m->initialized_before;
        m->initialized_before = NULL;
        _dynlinkFinalize(m);
        return;
    }
}
