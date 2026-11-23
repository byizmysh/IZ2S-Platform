// SPDX-License-Identifier: GPL-3.0+
// Unavailable desktop devices are reported as unavailable, never as a live
// transport. ISO/CHD readers, software GS, SPU2 and pad emulation remain real.
#include "CDVD/CDVDdiscReader.h"
#include "DEV9/pcap_io.h"
#include "DEV9/sockets.h"
#include "common/Error.h"
#include "common/HTTPDownloader.h"
#include "common/Assertions.h"
#include "MTVU.h"
#include "Config.h"
#include "ps2_mtvu_jit.h"

std::unique_ptr<HTTPDownloader> HTTPDownloader::Create(std::string) { return nullptr; }
std::vector<std::string> GetOpticalDriveList() { return {}; }
void GetValidDrive(std::string& drive) { drive.clear(); }
IOCtlSrc::IOCtlSrc(std::string) {}
IOCtlSrc::~IOCtlSrc() = default;
bool IOCtlSrc::Reopen(Error* error) { Error::SetStringView(error, "Switch 2 has no physical optical drive; select an image on SD."); return false; }
bool IOCtlSrc::DiscReady() { return false; }
u32 IOCtlSrc::GetSectorCount() const { return 0; }
s32 IOCtlSrc::GetMediaType() const { return 0; }
const std::vector<toc_entry>& IOCtlSrc::ReadTOC() const { static const std::vector<toc_entry> none; return none; }
bool IOCtlSrc::ReadSectors2048(u32, u32, u8*) const { return false; }
bool IOCtlSrc::ReadSectors2352(u32, u32, u8*) const { return false; }
bool IOCtlSrc::ReadTrackSubQ(cdvdSubQ*) const { return false; }
u32 IOCtlSrc::GetLayerBreakAddress() const { return 0; }
std::vector<AdapterEntry> PCAPAdapter::GetAdapters() { return {}; }
AdapterOptions PCAPAdapter::GetAdapterOptions() { return AdapterOptions::None; }
std::vector<AdapterEntry> SocketAdapter::GetAdapters() { return {}; }
AdapterOptions SocketAdapter::GetAdapterOptions() { return AdapterOptions::None; }

// CPU-side memory and reset operations occur only after the MTVU queue drains.
// A call made by the active worker itself already owns that state.
extern "C" void ps2_mtvu_lifecycle_cpu_quiesce() {
    if (!iz_is_mtvu_worker() && vu1Thread.IsOpen())
        vu1Thread.WaitVU();
}
extern "C" void ps2_mtvu_lifecycle_access_guard() { ps2_mtvu_lifecycle_cpu_quiesce(); }
extern "C" void ps2_mtvu_lifecycle_close_before_release() {
    ps2_mtvu_lifecycle_cpu_quiesce();
    // The native arena stays mapped until CPU providers have saved and released their code.
    if (!iz_mtvu_native_is_open())
        vu1Thread.Close();
}
