#include "ps2_session_state.h"
#include "ps2_checkpoint_files.h"

#include "VMManager.h"
#include "MTGS.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "SPU2/spu2.h"
#include "Host/AudioStream.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#ifdef PS2_NATIVE_HEADLESS_CHECKPOINT
#include "SaveState.h"
#include "hbxl_vm_frontend.h"

// The thumbnail is optional in the archive. A second GPU download here
// would wait on the graphics worker even though the guest state is captured.
extern "C" std::unique_ptr<SaveStateScreenshotData> __wrap__Z24SaveState_SaveScreenshotv() {
    ps2_hbxl_vm_stage("checkpoint-guest-captured-before-zip");
    return {};
}
#endif
#ifdef PS2_NATIVE_SD_CHECKPOINT
#include "hbxl_vm_frontend.h"
#include "ps2_sd_saves.h"
#endif

#include <cerrno>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#if defined(__cpp_exceptions)
#include <exception>
#endif
#include <sys/stat.h>

// The checked memory-card profile exposes write/flush failures that the
// historical void core API could not report. Legacy core images remain usable.
extern "C" bool ps2_checked_memcard_flush() __attribute__((weak));

namespace ps2 {
namespace {
thread_local bool checkpoint_active = false;
// A failed current archive must not replace its working fallback backup on
// the next normal Stop. Only a successful new publication clears this mark.
thread_local std::string rejected_autosave_current;

bool ValidText(const std::string& value) {
    if (value.empty() || value.size() > 256)
        return false;
    for (const unsigned char c : value)
        if (c < 32 || c == 127 || c == '\\')
            return false;
    return true;
}

bool ResolveRoot(const std::string& scratch, std::string& root, std::string& error) {
    if (!ValidText(scratch) || !Path::IsAbsolute(scratch)) {
        error = "A private scratch directory is required";
        return false;
    }
    root = Path::RealPath(scratch);
    const std::string data = Path::RealPath(EmuFolders::DataRoot);
    struct stat st{};
    if (root.empty() || data.empty() || root != data || root == "/" ||
        ::stat(root.c_str(), &st) || !S_ISDIR(st.st_mode)) {
        error = "Checkpoint scratch must be the VM's existing private data directory";
        return false;
    }
    return true;
}

bool ResolvePath(const std::string& scratch, const std::string& checkpoint,
                 bool loading, CheckpointResult& result) {
    std::string root;
    if (!ValidText(checkpoint) || !ResolveRoot(scratch, root, result.error)) {
        if (result.error.empty())
            result.error = "A private checkpoint name is required";
        return false;
    }
    std::string name = checkpoint;
    if (Path::IsAbsolute(name)) {
        const std::string prefix = root + "/";
        if (name.compare(0, prefix.size(), prefix) != 0) {
            result.error = "Checkpoint is outside private scratch";
            return false;
        }
        name.erase(0, prefix.size());
    }
    if (name.empty() || name.find('/') != std::string::npos ||
        name == "." || name == ".." || name.size() < 5 ||
        name.compare(name.size() - 4, 4, ".p2s") != 0) {
        result.error = "Checkpoint must be a direct .p2s file in private scratch";
        return false;
    }
    result.path = root + "/" + name;
    struct stat st{};
    if (::lstat(result.path.c_str(), &st) == 0) {
        if (!S_ISREG(st.st_mode) || st.st_nlink != 1 ||
            (loading && st.st_size <= 0)) {
            result.error = "Checkpoint must be a private regular file";
            return false;
        }
    } else if (errno != ENOENT || loading) {
        result.error = loading ? "Checkpoint does not exist" : "Checkpoint path cannot be inspected";
        return false;
    }
    return true;
}

void Settle() {
    MTGS::WaitGS();
    Pad::ResetAllControllerInputs();
    // The original pause policy owns the stream, including suppression and
    // background rules. Do not create, close or replace its audio backend.
    SPU2::UpdateOutputPaused();
}

CheckpointResult Run(CheckpointOperation operation, const std::string& scratch,
                     const std::string& checkpoint) {
    CheckpointResult result;
    if (!VMManager::Internal::IsOnCPUThread()) {
        result.error = "Checkpoint operation must run on the VM CPU thread";
        return result;
    }
    const VMState before = VMManager::GetState();
    if (before != VMState::Running && before != VMState::Paused) {
        result.error = "Checkpoint requires a running or paused VM";
        return result;
    }
    result.state_restored = true;
    if (checkpoint_active) {
        result.error = "Another checkpoint operation is active";
        return result;
    }
    if (!ResolvePath(scratch, checkpoint, operation == CheckpointOperation::Load, result))
        return result;

    struct ActiveGuard {
        ActiveGuard() { checkpoint_active = true; }
        ~ActiveGuard() { checkpoint_active = false; }
    } active;
    bool pause_complete = before == VMState::Paused;
    result.state_restored = false;
#if defined(__cpp_exceptions)
    try
#endif
    {
        if (before == VMState::Running) {
            VMManager::SetState(VMState::Paused);
            pause_complete = true;
        }
#ifdef PS2_NATIVE_HEADLESS_CHECKPOINT
        if (operation == CheckpointOperation::Save)
            ps2_hbxl_vm_stage("checkpoint-gs-settle-before");
#endif
        Settle();
#ifdef PS2_NATIVE_HEADLESS_CHECKPOINT
        if (operation == CheckpointOperation::Save)
            ps2_hbxl_vm_stage("checkpoint-writer-drain-before");
#endif
        // Drain any earlier asynchronous save before touching the selected
        // path. This adapter itself never requests an asynchronous writer.
        VMManager::WaitForSaveStateFlush();
        result.attempted = true;
        if (operation == CheckpointOperation::Save) {
#ifdef PS2_NATIVE_HEADLESS_CHECKPOINT
            ps2_hbxl_vm_stage("checkpoint-card-flush-before");
#endif
            bool cards_flushed = true;
            if (ps2_checked_memcard_flush)
                cards_flushed = ps2_checked_memcard_flush();
            else
                FileMcd_Flush();
            if (!cards_flushed) {
#ifdef PS2_NATIVE_HEADLESS_CHECKPOINT
                ps2_hbxl_vm_stage("checkpoint-card-flush-failed");
#endif
                result.error = "Memory card writes could not be flushed; checkpoint was not saved";
            } else {
#ifdef PS2_NATIVE_HEADLESS_CHECKPOINT
                ps2_hbxl_vm_stage("checkpoint-guest-capture-before");
#endif
                bool failed = false;
                VMManager::SaveState(result.path.c_str(), false, false,
                    [&](const std::string& message) {
                        failed = true;
                        result.error = message.empty() ? "Original SaveState failed" : message;
                    });
                VMManager::WaitForSaveStateFlush();
#ifdef PS2_NATIVE_HEADLESS_CHECKPOINT
                ps2_hbxl_vm_stage(failed ? "checkpoint-zip-failed" : "checkpoint-zip-complete");
#endif
                result.success = !failed && FileSystem::GetPathFileSize(result.path.c_str()) > 0;
                if (!result.success && result.error.empty())
                    result.error = "Original SaveState did not produce a checkpoint";
            }
        } else {
            Error error;
            result.guest_state_may_have_changed = true;
            result.success = VMManager::LoadState(result.path.c_str(), &error);
            if (!result.success)
                result.error = error.GetDescription().empty() ? "Original LoadState failed" : error.GetDescription();
            else if (VMManager::GetState() == VMState::Paused) {
                // Old host PCM is outside the checkpoint's guest SPU2 state.
                // Drop it while the paused VM still holds the IOP producer.
                if (AudioStream* stream = SPU2::GetOutputStream())
                    stream->EmptyBuffer();
            }
        }
#ifdef PS2_NATIVE_HEADLESS_CHECKPOINT
        if (operation == CheckpointOperation::Save)
            ps2_hbxl_vm_stage("checkpoint-final-gs-settle-before");
#endif
        Settle();
    }
#if defined(__cpp_exceptions)
    catch (const std::exception& error) {
        result.success = false;
        result.error = error.what();
    } catch (...) {
        result.success = false;
        result.error = "Checkpoint operation threw an unknown exception";
    }
#endif

    // Do not overwrite Stopping/Resetting/Shutdown with a pretend restoration.
    // LoadState failures may have reset guest contents even when Paused remains.
#if defined(__cpp_exceptions)
    try
#endif
    {
        if (pause_complete && VMManager::GetState() == VMState::Paused) {
            if (before == VMState::Running)
                VMManager::SetState(VMState::Running);
            SPU2::UpdateOutputPaused();
            result.state_restored = VMManager::GetState() == before;
        }
    }
#if defined(__cpp_exceptions)
    catch (...) {
        result.state_restored = false;
    }
#endif
    if (!result.state_restored) {
        result.success = false;
        if (!result.error.empty())
            result.error += "; ";
        result.error += "Previous VM running/paused state could not be restored";
    }
    return result;
}
} // namespace

CheckpointResult SaveCheckpoint(const std::string& scratch, const std::string& checkpoint) {
    return Run(CheckpointOperation::Save, scratch, checkpoint);
}
CheckpointResult LoadCheckpoint(const std::string& scratch, const std::string& checkpoint) {
    return Run(CheckpointOperation::Load, scratch, checkpoint);
}

GameAutosaveLocation GameAutosavePaths(const std::string& scratch, const std::string& game_id) {
    GameAutosaveLocation result;
    std::string root;
    if (!ValidText(game_id) || !ResolveRoot(scratch, root, result.error)) {
        if (result.error.empty())
            result.error = "A stable game identity is required";
        return result;
    }
    // Stable portable naming; never place untrusted title/path text in a name.
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : game_id) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    char stem[40];std::snprintf(stem, sizeof(stem), "autosave-%016llx", static_cast<unsigned long long>(hash));
    result.current_name = std::string(stem) + ".p2s";
    result.backup_name = std::string(stem) + ".backup.p2s";
    result.staging_name = std::string(stem) + ".next.p2s";
    result.current = root + "/" + result.current_name;
    result.backup = root + "/" + result.backup_name;
    result.staging = root + "/" + result.staging_name;
    result.valid = true;
    return result;
}

AutosaveLookup FindGameAutosave(const std::string& scratch, const std::string& game_id,
                               const std::string& legacy) {
    AutosaveLookup result;
    const auto paths = GameAutosavePaths(scratch, game_id);
    if (!paths.valid) {
        result.error = paths.error;
        return result;
    }
    const std::string names[] = {paths.current_name, paths.backup_name, legacy};
    for (unsigned index = 0; index < 3; index++) {
        if (names[index].empty())
            continue;
        CheckpointResult candidate;
        if (!ResolvePath(scratch, names[index], true, candidate)) {
            // Absence is normal. A rejected path never prevents a private
            // backup from being considered, and is never opened for writing.
            if (candidate.error != "Checkpoint does not exist")
                result.error = candidate.error;
            continue;
        }
        if (std::find(result.candidates.begin(), result.candidates.end(), candidate.path) != result.candidates.end())
            continue;
        if (!result.found) {
            result.found = true;result.path = candidate.path;
            result.from_backup = index == 1;result.legacy = index == 2;
        }
        result.candidates.push_back(candidate.path);
    }
    return result;
}

AutosaveResult SaveGameAutosave(const std::string& scratch, const std::string& game_id) {
    AutosaveResult result;
    const auto paths = GameAutosavePaths(scratch, game_id);
    if (!paths.valid) {
        result.error = paths.error;
        return result;
    }
    static_cast<CheckpointResult&>(result) = SaveCheckpoint(scratch, paths.staging_name);
    result.backup_path = paths.backup;
    if (!result.success)
        return result;
    // The original VM used libzip's own .part+rename to complete staging.
    // Publishing names never runs guest instructions or touches source media.
    const auto root = paths.current.substr(0, paths.current.size() - paths.current_name.size() - 1);
    const auto published = checkpoint_files::Publish(root, paths.current_name, paths.backup_name,
                                                     paths.staging_name, rejected_autosave_current != paths.current);
    result.success = published.success;
    result.backup_updated = published.backup_updated;
    result.durability_confirmed = published.durability_confirmed;
    result.path = paths.current;
    if (!published.success)
        result.error = published.error;
    else if (rejected_autosave_current == paths.current)
        rejected_autosave_current.clear();
    return result;
}

#ifdef PS2_NATIVE_SD_CHECKPOINT
CheckpointResult SaveGameAutosaveToSd(const std::string& scratch,
                                      const GameAutosaveLocation& paths) {
    if (!paths.valid) {
        CheckpointResult result;
        result.error = paths.error;
        result.state_restored = true;
        return result;
    }

    ps2_hbxl_vm_stage("autosave-capture-before");
    auto result = SaveCheckpoint(scratch, paths.staging_name);
    if (!result.success) {
        ps2_hbxl_vm_stage("autosave-capture-failed");
        return result;
    }

    ps2_hbxl_vm_stage("autosave-sd-before");
    const auto stored = StoreSdSave(result.path, paths.current_name);
    result.success = stored.success;
    result.error = stored.error;
    ps2_hbxl_vm_stage(stored.success ? "autosave-sd-verified" : "autosave-sd-failed");
    if (stored.success) {
        // Scratch is RAM. Move the old inode to the fallback name without
        // allocating and copying a second archive. The durable SD generation
        // is already verified; even a scratch rename error cannot undo it.
        bool has_current = false, has_backup = false;
        std::string error;
        const bool valid =
            checkpoint_files::Regular(paths.current, true, true, has_current, error) &&
            checkpoint_files::Regular(paths.backup, true, true, has_backup, error);
        const bool make_backup = valid && has_current && rejected_autosave_current != paths.current;
        if (valid && (!make_backup || ::rename(paths.current.c_str(), paths.backup.c_str()) == 0) &&
            ::rename(result.path.c_str(), paths.current.c_str()) == 0) {
            if (rejected_autosave_current == paths.current)
                rejected_autosave_current.clear();
            result.path = paths.current;
            return result;
        }
        ps2_hbxl_vm_stage("autosave-scratch-publish-failed");
    }
    // The staging name is private to this session and ignored during restore.
    if (::unlink(result.path.c_str()) != 0 && errno != ENOENT)
        ps2_hbxl_vm_stage("autosave-staging-cleanup-failed");
    result.path.clear();
    return result;
}
#endif

CheckpointResult LoadGameAutosave(const std::string& scratch, const std::string& game_id,
                                 const std::string& legacy) {
    CheckpointResult result;
    const auto lookup = FindGameAutosave(scratch, game_id, legacy);
    const auto paths = GameAutosavePaths(scratch, game_id);
    if (!lookup.found) {
        const VMState state = VMManager::GetState();
        result.state_restored = VMManager::Internal::IsOnCPUThread() &&
            (state == VMState::Running || state == VMState::Paused);
        result.error = lookup.error.empty() ? "No private autosave is available" : lookup.error;
        return result;
    }
    bool changed = false;
    std::string failures;
    for (const auto& candidate : lookup.candidates) {
        const VMState state = VMManager::GetState();
        if (state != VMState::Running && state != VMState::Paused)
            break;
        result = LoadCheckpoint(scratch, candidate);
        changed = changed || result.guest_state_may_have_changed;
        if (result.success) {
            if (candidate == paths.current)
                rejected_autosave_current.clear();
            result.guest_state_may_have_changed = changed;
            return result;
        }
        if (candidate == paths.current && result.attempted)
            rejected_autosave_current = paths.current;
        if (!failures.empty())
            failures += "; ";
        failures += candidate + ": " + result.error;
        if (!result.state_restored)
            break;
    }
    result.guest_state_may_have_changed = changed;
    result.error = failures.empty() ? "The VM is not available for autosave restore" : failures;
    return result;
}
} // namespace ps2
