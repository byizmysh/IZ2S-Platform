#ifndef PS2_SD_CARD_H
#define PS2_SD_CARD_H
#include "ps2_sd_saves.h"

namespace ps2 {
// root is an already-mounted, existing SDK directory. The mount owner keeps
// its API and filesystem alive until ShutdownLiveSdCard succeeds. Calls which
// import/export or bind require the VM's card writer to be quiescent.
bool ConfigureLiveSdCard(const Ps2SdkFsApi&, uint32_t (*rename_no_replace)(const char*, const char*), const std::string& root);
// Existing raw SD data is authoritative. Before binding, missing is success
// with found=false; malformed cards are preserved and fail rather than recreated.
SdSaveResult RestoreLiveSdCard(const std::string& local, const std::string& key);
// Import a private exact-size local raw card only when no SD card exists.
// Publish a unique verified temporary with SDK rename-without-replacement.
SdSaveResult CreateLiveSdCard(const std::string& local, const std::string& key);
bool BindLiveSdCard(const std::string& logical, const std::string& key);
bool IsLiveSdCardPath(const char*);
// Handles belong only to this client, with bits62/63 clear. Scalar callbacks
// take the bound logical path, and scope each actual SDK operation internally.
// Writable opens always update an existing card; creation is import-only.
const Ps2SdkFsApi& LiveSdCardBridgeApi();
// Export the bound authoritative SD card to an atomic private local file.
// Existing local data is preserved in a unique backup before replacement.
// A missing bound SD file fails and poisons the client; a stale local export
// must never be acknowledged as a newly exported card.
SdSaveResult ExportLiveSdCard(const std::string& local);
// Store the bound raw card directly in the two-generation SD snapshot backend.
// The VM writer must be paused. No local export or backup file is created.
SdSaveResult SnapshotLiveSdCard();
bool LiveSdCardHealthy();
// Refuses while any FILE/SDK stream is open; never unmounts the filesystem.
bool ShutdownLiveSdCard();
}
#endif
