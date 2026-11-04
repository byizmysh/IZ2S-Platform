# Host log retention

Event journals and the terminal transcript rotate when the next append would
exceed 8 MiB. Each file keeps the current segment and two archives (`.1` is newer
than `.2`), so an actively written log retains at most 24 MiB. Rotation drops the
oldest segment. An already oversized active file keeps only complete trailing
lines within 8 MiB when it first rotates. Other old files and session directories
are not pruned.

A tiny `.retention.json` sidecar records possible historical loss before a
rotation can discard bytes. It is written once and read with a 2 KiB cap. The
export shows a fixed omission marker after such a rotation, even when all
remaining segments fit its read limits. A failed rotation can leave this
conservative marker in place without deleting data.

Live history reads at most 1 MiB and 250 recent events across retained segments.
Full retained history reads less than 24 MiB and at most 100,000 entries,
reserving response overhead and a fixed omission marker when its byte or entry
limit is reached. It keeps the latest events when limited. Events are returned in chronological
order by the attempt ledger. Bytes outside these retention/read limits are not
included in exports. This is a per-file policy: the total across many saved
sessions is not bounded automatically.

The optional terminal transcript uses an asynchronous queue of at most 256 KiB.
When full, it drops further complete lines and writes a fixed omission marker
after the queue recovers. A logical line over 64 KiB is discarded in full until
its newline, with a fixed marker. Credentials and UTF-8 characters split across
input chunks remain buffered until the complete safe line can be redacted.
There are at most 16 channel buffers; extra unknown channels are omitted rather
than merging unrelated partial lines. `close()` returns a Promise for the flush.
Storage failures produce one warning and do not stop the host application.

Journal appends remain strict: storage errors reject to their caller. Writes are
serialized per path. Reads do not wait for the write queue: a stalled log write
cannot block live status. Opened file descriptors preserve individual segment
snapshots across rotation; a concurrent rotation can omit a segment, and repeated
UUID-bearing entries are deduplicated. Separate processes should not share an event file.
Atomic JSON snapshots use a separate shared path queue; snapshot bytes are
captured on invocation and failed operations do not block later saves.
