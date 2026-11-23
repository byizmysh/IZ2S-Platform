#ifndef PS2_USER_STORAGE_H
#define PS2_USER_STORAGE_H

#include <string>

namespace ps2 {

struct PrivateCardResult {
    bool success = false;
    bool created = false;
    // False for the exclusive-copy fallback used by filesystems without links.
    bool atomic_creation = false;
    bool durability_confirmed = false;
    std::string filename;
    std::string path;
    std::string error;
};

// Call before VM initialization, with no card writer active. DataRoot must be
// private_scratch; the configured MemoryCards directory must be a direct child
// of it. Creates that directory if absent. Existing regular raw PS2 cards are
// reused without writing; invalid/empty cards are rejected, never recreated.
// New 8MiB cards use the original FileMcd_CreateNewCard implementation and are
// initially UNFORMATTED, for the PS2 BIOS/game to format through normal use.
// No source BIOS, disc or existing card is modified. If exclusive-copy creation
// fails, its new destination is retained. A file with an incomplete raw-card
// size is rejected on next startup; no failed copy is automatically retried.
PrivateCardResult PreparePrivateSlot1Card(const std::string& private_scratch,
                                         const std::string& filename = "user-slot1.ps2");

} // namespace ps2
#endif
