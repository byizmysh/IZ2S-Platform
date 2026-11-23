#include "hbxl_vm_frontend.h"
#include "hbxl_data_memory.h"
#include "hbxl_code_memory.h"
#include "sdk_threads.h"
#include "Memory.h"
#include <cstdio>
static_assert(HostMemoryMap::MainSize==PS2_HBXL_VM_DATA_BYTES);
static_assert(HostMemoryMap::CodeSize==PS2_HBXL_VM_CODE_BYTES);
#define CHECK(x) do{if(!(x))ps2_hbxl_vm_fatal(#x,__LINE__);}while(0)
void ps2_hbxl_vm_memory_initialize(){
    ps2_hbxl_vm_stage("sdk-data-bind-before");CHECK(!ps2_hbxl_data_initialize());
    const auto *config=ps2_hbxl_code_default_config();CHECK(config && config->bytes==HostMemoryMap::CodeSize);
    ps2_hbxl_vm_stage("sdk-code-bind-before");CHECK(!ps2_hbxl_code_initialize(config));
    ps2_hbxl_vm_stage("memory-bind-complete");
}
void ps2_hbxl_vm_memory_shutdown(){
    Ps2HbxlDataStatistics data{};Ps2HbxlCodeStatistics code{};Ps2SdkThreadStatistics threads{};
    ps2_hbxl_data_statistics(&data);ps2_hbxl_code_statistics(&code);ps2_sdk_threads_statistics(&threads);
    CHECK(data.ready && !data.quarantined && !data.active_heap_blocks && !data.active_regions && !data.live_bytes && !data.committed_pages);
    CHECK(data.allocations==1 && data.releases==1 && code.initialized && !code.poisoned && !code.live_pages && !code.mapped_pages && code.maps && code.maps==code.unmaps);
    CHECK(threads.created==threads.joined && !threads.detached);
    FILE *f=std::fopen("hbxl-vm-runtime.json","wb");CHECK(f);
    std::fprintf(f,"{\"passed\":true,\"sdkDataFingerprintsVerified\":%s,\"dataBytes\":%llu,\"codeBytes\":%llu,\"dataAllocations\":%llu,\"dataReleases\":%llu,\"codeMaps\":%llu,\"codeUnmaps\":%llu,\"codePublications\":%llu,\"codePeakReservedPages\":%u,\"createdThreads\":%llu,\"joinedThreads\":%llu,\"productionGuestMemoryBridge\":true,\"noAvMode\":true}\n",
        data.sdk_fingerprints_verified?"true":"false",(unsigned long long)HostMemoryMap::MainSize,(unsigned long long)code.bytes,
        (unsigned long long)data.allocations,(unsigned long long)data.releases,(unsigned long long)code.maps,(unsigned long long)code.unmaps,
        (unsigned long long)code.publications,code.peak_pages,(unsigned long long)threads.created,(unsigned long long)threads.joined);
    CHECK(!std::fclose(f));ps2_hbxl_vm_stage("code-shutdown-before");CHECK(!ps2_hbxl_code_shutdown());
    ps2_hbxl_vm_stage("data-shutdown-before");CHECK(!ps2_hbxl_data_shutdown());ps2_hbxl_vm_stage("memory-shutdown-complete");
}
