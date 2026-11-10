# Parent ownership after a retained failure

The native SDK can start an engine pthread before its create call returns.
A contradictory create result, unknown wait, or invalid STOP can end the
supervisor while that child still has not entered its native trampoline.
Keeping executable/data allocations alone is insufficient: the trampoline's
`IzNative`, borrowed `IzFs`, application/environment and graphics tables must
also outlive the supervisor function.

Those objects now belong to `IzContext.owner`, in the existing bootstrap-owned
allocation. They are not automatic variables in `iz_run`. The SDK thread may
return through `entry.S` and restore its original stack without invalidating
the child's parent metadata. The existing poisoned-context guard keeps the
whole allocation, mappings and capabilities retained until real cleanup is
proven. A late entry result does not fabricate an SDK join or engine cleanup
receipt. The application remains retained after such an uncertain outcome.

A context has one supervisor lifetime. Nonzero state or an initialized owner
rejects another `iz_run` before any SDK/FS/graphics operation, without clearing
the existing owner. Ordinary app return/relaunch inside the running supervisor
continues using the existing native load/unload lifecycle and fresh tickets.

All ABI 1–6 public prefixes and older context offsets remain unchanged. The
context grows from 8,392 to 10,432 bytes, occupying unused space before the
bootstrap's temporary area; the existing bootstrap allocation and SDK/RO page
counts do not grow for this image. The startup context write remains one
32,768-byte native write chunk. Status reads use the unchanged 8,392-byte
public prefix and revision guards, excluding private ownership data.

`node tools/sd-launcher/lifetime-local.cjs` uses two real host RX programs and
a real pthread. The child waits outside the actual production trampoline until
`iz_run` returns on ambiguous create, unknown wait or invalid STOP. It then
executes the production trampoline and loaded host engine. ASan's detection
of stack use after return is explicitly enabled. The old implementation
failed inside production `native-loader.c:worker_entry`; the fixed owner
passes all three cases. Re-entry and attempted unload perform no SDK replay.

These are local C lifetime tests with modeled Switch SDK/FS/Vulkan boundaries,
not a reproduction of a physical console crash. AArch64 is compiled and PIC
checked. The fix does not complete the native PS2 controller or its required
VM/Main/AV/storage/JIT and nested-worker cleanup validation.
