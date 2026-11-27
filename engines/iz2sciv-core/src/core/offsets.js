// SDK offsets discovered via static analysis
// These are injected automatically by tools/bundle.mjs into the payload

// -- TLS / IPC internals (discovered via Ghidra + byte search) -------
const IPC_SEND_DISPATCHER = sdk_base + 0x14ba90n; // checks tpidrro_el0, calls svc

// -- Mount table / FS internals (confirmed by the reference port, same SDK) ---
const F_FindMount = sdk_base + 0xaaf10n; // nn::fs::detail::Find(FileSystemAccessor**, const char*)

// -- Service Manager (sm:) ------------------------------------------
const SM_Initialize = sdk_base + 0x14c960n; // InitializeHipcServiceResolution
const SM_GetService = sdk_base + 0x14cab0n; // ConnectToHipcService(out_handle, name)
const SM_Finalize = sdk_base + 0x14c990n; // FinalizeHipcServiceResolution

// -- Handle management ----------------------------------------------
const F_CloseHandle = sdk_base + 0x168be4n; // svc #0x16 wrapper

// -- Threading (nn::os::*) - offsets confirmed by the reference on same SDK -
const F_Sleep = sdk_base + 0x137100n; // nn::os::SleepThread(nanoseconds)
const F_Tick = sdk_base + 0x137240n; // nn::os::GetSystemTick -> returns u64 counter
const SYSTEM_TICK_FREQUENCY_HZ = 31250000; // native counter; legacy SDK frequency reports 19.2 MHz

// -- ROP frame gadgets (for worker thread execution chains) ----------
const GADGET_LOAD_TABLE = sdk_base + 0x4e094n; // loads function table, dispatches call
const GADGET_CALL4 = sdk_base + 0x359b88n; // sets up 4 args from stack, calls function
const GADGET_SAVE_RESULT = sdk_base + 0x19bcc0n; // stores x0 to [addr], returns
const GADGET_LONGJMP = sdk_base + 0x5511b4n; // restores context from frame, starts chain
const GADGET_MEMSET = sdk_base + 0x522af0n; // memset(dst, val, len) - SDK implementation
const GADGET_MEMCPY = sdk_base + 0x5224e0n; // memcpy(dst, src, len) - SDK implementation

// -- Synchronization (nn::os::*) ------------------------------------
const F_WaitForAddress = sdk_base + 0x168d1cn; // WaitForAddress(addr, type, value, timeout_ns)
const F_SignalToAddress = sdk_base + 0x168d24n; // SignalToAddress(addr, type, value, count)

// -- Raw SVC wrappers (bare kernel calls, no nn::os bookkeeping) -----
// Source: the ns2jb CivSwitch port, hardware-qualified on the same build.
// Raw svcCreateThread bypasses the nn::os::CreateThread hang.
const SVC_CreateThread = sdk_base + 0x168b2cn; // svc #0x08; x0=&out_handle, x1=entry, x2=arg, x3=stack_top, w4=prio, w5=core
const SVC_StartThread = sdk_base + 0x168b44n; // svc #0x09; w0=handle
const SVC_ExitThread = sdk_base + 0x168b4cn; // svc #0x0A; never returns. NOT svc #0x07 (ExitProcess!)
const SVC_GetThreadPriority = sdk_base + 0x168b5cn; // x0=&out_prio, w1=handle
const SVC_GetThreadCoreMask = sdk_base + 0x168b7cn; // x0=&out_core, x1=&out_mask, w2=handle
// svcSetThreadCoreMask (svc #0x0F): w0=handle, w1=ideal_core, x2=affinity_mask.
// Offset discovered at runtime by scanning near SVC_GetThreadCoreMask.
var SVC_SetThreadCoreMask = 0n;
const SVC_WaitSync = sdk_base + 0x168bf4n; // x0=&out_idx, x1=handles, w2=count, x3=timeout_ns

// -- nn::os thread manager list (for borrowing priority from live threads) --
const THREAD_MANAGER_PTR = sdk_base + 0xb22280n; // read64 -> manager; manager+0xF0 = list; +0x1D0 = sentinel
