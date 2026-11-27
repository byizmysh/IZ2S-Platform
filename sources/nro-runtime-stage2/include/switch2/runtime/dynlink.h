/**
 * @file dynlink.h
 * @brief Dynamic linking (Switch 2): the modules of a program and their relocation.
 * @author kurumuz
 * @copyright 2026 libounce contributors
 * @license ISC
 * @note A module is an NRO image with a MOD0 header and an ELF dynamic section: the
 *       program, or a shared library that it names with DT_NEEDED. A \ref DynlinkProgram
 *       holds the modules in lookup order: the program first, then its libraries
 *       breadth-first. A module's references are looked up in the program's global scope,
 *       then in the module and the libraries it needs. The global scope is the program,
 *       the libraries loaded with it, and the libraries that were added as global; a
 *       library that was added as local is seen only by the modules that need it.
 * @note A loader can put libraries between the program and the libraries it needs, as
 *       Linux does with LD_PRELOAD. A definition in such a library is found before the one
 *       in a library after it, so it takes its place for every module that looks the name
 *       up, and it reaches the one it replaced with \ref dynlinkProgramFindSymbolAfter.
 *       A library's references to its own symbols are replaced only when the library was
 *       not linked to bind them to itself. These functions parse and relocate images that are already in
 *       memory. Loading the files and mapping them is the loader's work: it maps the
 *       program and every library it needs, and enters the runtime library (libounce.so)
 *       with a \ref DynlinkLoadInfo. The runtime library links the modules and calls main.
 * @note A module's thread-local variables use one of two models. With the initial-exec model
 *       (-ftls-model=initial-exec) the module's block is in every thread's TLS area and its
 *       code reads a variable at a fixed offset from the thread pointer: this is the model of
 *       the modules that are loaded with the program. With the general-dynamic model in its
 *       traditional form (-ftls-model=global-dynamic -mtls-dialect=trad) the code calls
 *       __tls_get_addr, and the block can be anywhere: a library that is loaded later with
 *       dlopen can then have thread-local variables of any size. TLS descriptors, the
 *       compiler's default form of that model, are refused: the compiler generates wrong code
 *       for them with -mtp=soft.
 */
#pragma once
#if !defined(__SWITCH2__)
#error "Dynamic linking is available only in a native Switch 2 build"
#endif

#include <elf.h>
#include <switch/types.h>
#include <switch/result.h>

/// Size of the space that a module reserves in its own image for its \ref DynlinkModule.
/// MOD0's module object field locates it, and the "OMR0" block after MOD0 gives its size.
#define DYNLINK_MODULE_RECORD_SIZE 0x200

/// Key of the loader's configuration entry for a dynamically linked program ("OMOD").
/// Value[0] is the address of a \ref DynlinkLoadInfo.
#define DYNLINK_CONFIG_ENTRY_KEY 0x4F4D4F44

/// @name Results of a dynamically linked program that did not start
/// The loader or the runtime library returns one of these in place of running the program,
/// and a menu shows it as the program's result (2346-0200 and so on). Each has one cause,
/// so that the number alone tells a user what to do. The loader's record has the detail,
/// such as the library's name.
///@{

/// The loader does not load shared libraries: it entered the program as a statically linked
/// one, without its runtime library. The program's own start code returns this. The loader
/// is older than dynamic linking.
#define DYNLINK_RESULT_NOT_LINKED           MAKERESULT(Module_HomebrewAbi, 200)
/// A shared library that the program or one of its libraries needs is not in the program's
/// directory or in the loader's library directory.
#define DYNLINK_RESULT_LIBRARY_NOT_FOUND    MAKERESULT(Module_HomebrewAbi, 201)
/// A shared library's file was found and is not usable: it is not a shared library that was
/// built for this libounce, it has another DT_SONAME than its name, or it could not be mapped.
#define DYNLINK_RESULT_LIBRARY_NOT_LOADED   MAKERESULT(Module_HomebrewAbi, 202)
/// A module refers to a symbol that no module of the program defines: a library is older
/// or newer than the one the program was built with.
#define DYNLINK_RESULT_UNDEFINED_SYMBOL     MAKERESULT(Module_HomebrewAbi, 203)
/// A module has a relocation that the linker does not support, as from a thread-local
/// variable that was not compiled for the initial-exec model.
#define DYNLINK_RESULT_BAD_RELOCATION       MAKERESULT(Module_HomebrewAbi, 204)
/// A library of the loader's preload directory could not be loaded.
#define DYNLINK_RESULT_PRELOAD_NOT_LOADED   MAKERESULT(Module_HomebrewAbi, 205)
/// None of the program's libraries is the runtime library (libounce.so), or the runtime
/// library got no list of modules from the loader.
#define DYNLINK_RESULT_NO_RUNTIME_LIBRARY   MAKERESULT(Module_HomebrewAbi, 206)

///@}

/// An image that the loader mapped.
typedef struct DynlinkLoadedModule {
    u64 base;                                   ///< Address of the image's first byte.
    u64 size;                                   ///< Size of the image with its BSS.
} DynlinkLoadedModule;

/// What the loader gives the runtime library.
typedef struct DynlinkLoadInfo {
    const DynlinkLoadedModule* modules;         ///< The program first, then its libraries breadth-first.
    u32 module_count;                           ///< Their number.
    u32 message_size;                           ///< Size of the message buffer.
    char* message;                              ///< When linking fails, the runtime library writes the reason here.
    const char* const* library_directories;     ///< The directories where the loader looked for the program's libraries, in order, as paths that fopen takes. dlopen looks in the same ones.
    u32 library_directory_count;                ///< Their number.
} DynlinkLoadInfo;

/// One module: a parsed NRO image. A module of a program is in a list, and its record is
/// in the module's own image, so that a program has no fixed number of modules.
typedef struct DynlinkModule {
    uintptr_t base;                             ///< Address of the image's first byte.
    size_t size;                                ///< Size of the image with its BSS.
    const Elf64_Dyn* dynamic;                   ///< The dynamic section.
    const char* soname;                         ///< DT_SONAME, NULL for a program.
    const Elf64_Sym* symtab;                    ///< Dynamic symbol table.
    const char* strtab;                         ///< Dynamic string table.
    size_t strtab_size;                         ///< Its size.
    u32 symbol_count;                           ///< Number of dynamic symbols, from the SysV hash table.
    const u32* sysv_hash;                       ///< DT_HASH, or NULL.
    const u32* gnu_hash;                        ///< DT_GNU_HASH, or NULL.
    const Elf64_Rela* rela;                     ///< DT_RELA entries.
    size_t rela_count;                          ///< Their number.
    const Elf64_Rela* jmprel;                   ///< DT_JMPREL entries.
    size_t jmprel_count;                        ///< Their number.
    const Elf64_Relr* relr;                     ///< DT_RELR entries.
    size_t relr_count;                          ///< Their number.
    void (**init_array)(void);                  ///< Constructors.
    size_t init_count;                          ///< Their number.
    void (**fini_array)(void);                  ///< Destructors.
    size_t fini_count;                          ///< Their number.
    u32 needed_count;                           ///< Number of libraries it names with DT_NEEDED; see \ref dynlinkModuleGetNeeded.
    const void* tls_template;                   ///< Initial bytes of the module's thread-local data.
    size_t tls_template_size;                   ///< Their size.
    size_t tls_size;                            ///< Size of the module's thread-local block.
    size_t tls_align;                           ///< Its alignment.
    size_t tls_offset;                          ///< Offset of the block in a thread's TLS area, when it is there.
    u64 tls_id;                                 ///< What __tls_get_addr gets for the module's block. \ref dynlinkProgramAddModule sets it; no two modules of a program ever have the same.
    bool needs_static_tls;                      ///< The module has initial-exec code: its block must be in every thread's TLS area.
    bool is_tls_static;                         ///< The block is in every thread's TLS area, at tls_offset. \ref dynlinkProgramLayoutTls sets it.
    const void* eh_frame_hdr;                   ///< Index of the unwind entries (.eh_frame_hdr), or NULL.
    size_t eh_frame_hdr_size;                   ///< Its size.
    void* relro;                                ///< Data that is read-only after relocation, or NULL.
    size_t relro_size;                          ///< Its size.
    uintptr_t runtime_entry;                    ///< Entry of the runtime library; zero for every other module.
    void* dso_handle;                           ///< The module's __dso_handle, which its C++ code gives __cxa_atexit, or NULL.
    bool is_self_relocated;                     ///< The module already applied its own base relocations (RELATIVE and RELR).
    bool is_relocated;                          ///< \ref dynlinkProgramRelocate finished this module.
    bool is_initialized;                        ///< Its constructors have run.
    bool is_global;                             ///< It is in the program's global scope.
    void* owner_data;                           ///< For the code that loaded the module. The linker does not use it.
    struct DynlinkModule* next;                 ///< The next module of its program in lookup order, or NULL.
    struct DynlinkModule* initialized_before;   ///< Private: the module whose constructors ran before this one's.
    struct DynlinkModule* search_next;          ///< Private: the next module of a search in progress.
    u64 search_mark;                            ///< Private: the last search that reached the module.
} DynlinkModule;

#ifdef __cplusplus
static_assert(sizeof(DynlinkModule) <= DYNLINK_MODULE_RECORD_SIZE, "A module's record must fit the space that modules reserve");
#else
_Static_assert(sizeof(DynlinkModule) <= DYNLINK_MODULE_RECORD_SIZE, "A module's record must fit the space that modules reserve");
#endif

/// The modules of one program, in lookup order.
typedef struct DynlinkProgram {
    DynlinkModule* first;                        ///< The program. The libraries follow it breadth-first, linked by DynlinkModule::next.
    DynlinkModule* last;                         ///< The module that was added last.
    u32 module_count;                            ///< Number of modules.
    size_t tls_start_offset;                     ///< Distance from the thread pointer to the TLS area.
    size_t tls_size;                             ///< Size of the TLS area of all modules.
    size_t tls_align;                            ///< Its alignment.
    DynlinkModule* last_initialized;             ///< Private: the module whose constructors ran last.
    u64 last_tls_id;                             ///< Private: the tls_id of the module that was added last.
    u64 search_mark;                             ///< Private: the number of the last search.
} DynlinkProgram;

/**
 * @brief Parses a module's MOD0 header and dynamic section.
 * @param[out] m Output module. It refers to the image; it does not copy from it.
 * @param[in] image The NRO image from its first byte: mapped, or a file image in memory.
 * @param[in] size Size of the image.
 * @return LibnxError_BadInput for an image without a MOD0 header or a usable dynamic section.
 */
Result dynlinkModuleOpen(DynlinkModule* m, const void* image, size_t size);

/**
 * @brief Parses a module into the record space that the module's own image reserves.
 * @param[in] image The NRO image from its first byte, with its BSS.
 * @param[in] size Size of the image.
 * @param[out] out_module The module's record, inside the image.
 * @return As \ref dynlinkModuleOpen, and LibnxError_BadInput for an image that reserves no record
 *         space or less than this library needs: the module was not built as a module, or with
 *         an older libounce.
 */
Result dynlinkModuleOpenInImage(void* image, size_t size, DynlinkModule** out_module);

/**
 * @brief Gets the name of a library that a module needs.
 * @param[in] m Module.
 * @param[in] index Index of the DT_NEEDED entry, less than DynlinkModule::needed_count.
 * @return The library's DT_SONAME, or NULL.
 */
const char* dynlinkModuleGetNeeded(const DynlinkModule* m, u32 index);

/**
 * @brief Finds a symbol that a module defines and exports.
 * @param[in] m Module.
 * @param[in] name Symbol name.
 * @return The symbol, or NULL.
 */
const Elf64_Sym* dynlinkModuleFindSymbol(const DynlinkModule* m, const char* name);

/// Starts an empty program.
void dynlinkProgramCreate(DynlinkProgram* p);

/**
 * @brief Adds a module at the end of the program's lookup order.
 * @param[in] p Program.
 * @param[in] m Module. The program keeps the pointer.
 * @param[in] is_global Whether the module is in the global scope: true for the program and the
 *                      libraries that are loaded with it.
 */
void dynlinkProgramAddModule(DynlinkProgram* p, DynlinkModule* m, bool is_global);

/**
 * @brief Puts a module and the libraries it needs in the global scope.
 * @param[in] p Program.
 * @param[in] m Module of the program.
 * @note References that are already relocated keep what they were bound to.
 */
void dynlinkProgramMakeGlobal(DynlinkProgram* p, DynlinkModule* m);

/**
 * @brief Removes a module from the program.
 * @param[in] p Program.
 * @param[in] m Module, after \ref dynlinkProgramRunModuleDestructors. No other module may still use its symbols.
 */
void dynlinkProgramRemoveModule(DynlinkProgram* p, DynlinkModule* m);

/// Finds the program's module with a DT_SONAME, or NULL.
DynlinkModule* dynlinkProgramFindModule(const DynlinkProgram* p, const char* soname);

/**
 * @brief Finds the first definition of a symbol in the program's global scope.
 * @param[in] p Program.
 * @param[in] name Symbol name.
 * @param[out] out_module The module that defines it. May be NULL.
 * @return The symbol, or NULL.
 */
const Elf64_Sym* dynlinkProgramFindSymbol(const DynlinkProgram* p, const char* name, const DynlinkModule** out_module);

/**
 * @brief Finds the next definition of a symbol in the global scope, after one module.
 * @param[in] p Program.
 * @param[in] m Module of the program. The modules after it in lookup order are searched.
 * @param[in] name Symbol name.
 * @param[out] out_module The module that defines it. May be NULL.
 * @return The symbol, or NULL.
 */
const Elf64_Sym* dynlinkProgramFindSymbolAfter(const DynlinkProgram* p, const DynlinkModule* m, const char* name,
                                               const DynlinkModule** out_module);

/// Finds the program's module whose image contains an address, or NULL.
DynlinkModule* dynlinkProgramFindModuleByAddress(const DynlinkProgram* p, uintptr_t address);

/**
 * @brief Finds a symbol in a module, then in the libraries it needs, breadth-first.
 * @param[in] p Program.
 * @param[in] m Module of the program.
 * @param[in] name Symbol name.
 * @param[out] out_module The module that defines it. May be NULL.
 * @return The symbol, or NULL.
 */
const Elf64_Sym* dynlinkProgramFindSymbolFrom(DynlinkProgram* p, DynlinkModule* m, const char* name,
                                              const DynlinkModule** out_module);

/**
 * @brief Gives every module its place in a thread's TLS area.
 * @param[in] p Program, with all its modules added.
 * @note The program's block is first: its code uses offsets fixed when it was linked.
 */
void dynlinkProgramLayoutTls(DynlinkProgram* p);

/**
 * @brief Applies the relocations of every module that is not relocated yet.
 * @param[in] p Program, after \ref dynlinkProgramLayoutTls.
 * @param[out] out_symbol Name of the symbol that had no definition, on LibnxError_NotFound. May be NULL.
 * @return LibnxError_NotFound for an undefined symbol that is not weak, LibnxError_BadReloc for a
 *         relocation type that is not supported, or for initial-exec code that reads a variable of
 *         a module whose block is not in the TLS area.
 */
Result dynlinkProgramRelocate(DynlinkProgram* p, const char** out_symbol);

/**
 * @brief Finds the unwind entry (FDE) of the function that contains an address.
 * @param[in] p Program.
 * @param[in] address Address in the code of one of the modules.
 * @param[out] out_function Address of the function's first instruction.
 * @return The entry in the module's .eh_frame section, or NULL when the address is in no module
 *         or in code without unwind information.
 * @note A module has the index that this reads when it is linked with --eh-frame-hdr.
 */
const void* dynlinkProgramFindUnwindEntry(const DynlinkProgram* p, uintptr_t address, uintptr_t* out_function);

/**
 * @brief Runs the constructors of every module, each after the libraries it needs.
 * @param[in] p Program, relocated.
 * @note The program's own run last: a library that the program does not need, such as one
 *       that the loader added, is ready before any code of the program runs.
 */
void dynlinkProgramRunConstructors(DynlinkProgram* p);

/// Runs the destructors of every module, in the reverse order of their constructors.
void dynlinkProgramRunDestructors(DynlinkProgram* p);

/// Runs the destructors of one module whose constructors ran.
void dynlinkProgramRunModuleDestructors(DynlinkProgram* p, DynlinkModule* m);
