# SD engine image loading

An `IZNATRL1` image contains the original linked code/data and a bounded table
of runtime address fixups. It is an engine image, not a native-app controller
or permission to enter the engine. A module still owns engine-specific setup,
exit requests and cleanup receipts. The loader never invokes an engine.

Code and data may be far apart in the process's 39-bit address space. Choosing
the first free executable address is insufficient: cross-segment ARM64 ADRP
and PREL32 fields have finite reach. Before allocating executable storage,
the loader streams and validates the fixup table and intersects the exact
inclusive bounds on `code_base - data_base`. Both bases are page aligned.

When a finite bound exists, the loader allocates and verifies the writable
data first, then finds a currently free executable range within those bounds.
An explicit address plan must satisfy the same constraint. Images without
such references retain their previous allocation order. The chosen range is
checked again against fresh process identity, reserved regions and free
memory immediately before executable mapping.

This adds one fixup-table read pass, using the existing 64 KiB scratch buffer
and cooperative work budget. It does not add a PC request, cache a console
address or change the image format. For the current PS2 R176 image this is
3,110,208 additional SD bytes for a 44,094,088-byte image. Data, code and fixup
CRCs, original operand values and relocation ranges are still checked before
any executable page is published. Known failures unwind; uncertain ownership
retains the allocations rather than releasing potentially live memory.

`node tools/sd-launcher/reach-local.cjs` exercises the production loader with
real host RW/RX aliases and a modeled sparse console address space. The
independent oracle decodes every relocated operand and compares all bytes
outside fixup fields to the original image. The bounds model uses ASan and
UBSan; the sparse-address model uses UBSan because ASan reserves that host
address range. No ARM64 engine or console service is executed by these tests.

`--ps2-image <path>` optionally checks an already exported PS2 image through
the same path. Exporting the current engine, matching its ELF symbols and
loading/releasing its image are preparation evidence only. The current
host-based PS2 package still requires a native startup/resource controller
before it can run without the PC. Engine bytes remain unchanged.
