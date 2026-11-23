#ifndef PS2_SESSION_STATE_H
#define PS2_SESSION_STATE_H

#include <string>
#include <vector>

namespace ps2 {

enum class CheckpointOperation { Save, Load };

struct CheckpointResult {
    bool success = false;
    bool attempted = false;
    bool state_restored = false;
    // Original LoadState() can reset the VM when an archive cannot be loaded.
    // A failed attempted load must never be reported as "failed, no change".
    bool guest_state_may_have_changed = false;
    std::string path;
    std::string error;
};

// Call only on the initialized VM's CPU thread, between execution slices.
// private_scratch must resolve to EmuFolders::DataRoot. checkpoint is a .p2s
// basename or its absolute direct-child path. No directories are created.
// Both APIs complete synchronously and preserve the previous Running/Paused
// state when the core remains valid. These are emulator checkpoints, not
// saves performed by a game's memory-card code.
CheckpointResult SaveCheckpoint(const std::string& private_scratch,
                                const std::string& checkpoint);
CheckpointResult LoadCheckpoint(const std::string& private_scratch,
                                const std::string& checkpoint);

struct GameAutosaveLocation {
    bool valid = false;
    std::string current, backup, staging;
    std::string current_name, backup_name, staging_name;
    std::string error;
};
struct AutosaveLookup {
    bool found = false;
    bool from_backup = false;
    bool legacy = false;
    std::string path;
    // Ordered private regular files only; staging/.part files are excluded.
    // Presence does not claim that the original VM can load the archive.
    std::vector<std::string> candidates;
    std::string error;
};
struct AutosaveResult : CheckpointResult {
    bool backup_updated = false;
    bool durability_confirmed = false;
    std::string backup_path;
};

// stable_game_id must identify content, not just its pathname. The caller can
// use VMManager::GetDiscSerial() + ':' + the hexadecimal GetDiscCRC() after
// booting the ELF. It must be identical at lookup, manual save and normal Stop.
GameAutosaveLocation GameAutosavePaths(const std::string& private_scratch,
                                       const std::string& stable_game_id);
AutosaveLookup FindGameAutosave(const std::string& private_scratch,
                               const std::string& stable_game_id,
                               const std::string& legacy_checkpoint = "session-checkpoint.p2s");
// Save only on explicit normal Stop or a manual request, at the same CPU safe
// point as SaveCheckpoint. A failed save/publication does not authorize Stop.
// Current is replaced only after a complete staged save and a flushed backup
// of its predecessor. Logical atomic publication is distinct from confirmed
// power-loss durability, which requires directory fsync support.
AutosaveResult SaveGameAutosave(const std::string& private_scratch,
                                const std::string& stable_game_id);
#ifdef PS2_NATIVE_SD_CHECKPOINT
// Commit the captured archive to the durable SD banks before rotating the
// in-session RAM files. Keeps the previous autosave available for fallback.
CheckpointResult SaveGameAutosaveToSd(const std::string& private_scratch,
                                     const GameAutosaveLocation& paths);
#endif
// Try current, backup, then the named legacy checkpoint. A rejected LoadState
// may reset guest contents; fallback is attempted only while the VM remains
// Running/Paused. The returned path is the archive actually loaded. Explicit
// startup state selection should bypass this default lookup entirely.
// A legacy filename is a caller-qualified same-game fallback; it has no new
// game-identity filename binding and must not name a different game's state.
CheckpointResult LoadGameAutosave(const std::string& private_scratch,
                                 const std::string& stable_game_id,
                                 const std::string& legacy_checkpoint = "session-checkpoint.p2s");

} // namespace ps2
#endif
