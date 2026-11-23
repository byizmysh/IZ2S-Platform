#include "hbxl_vm_frontend.h"
#include "hbxl_data_memory.h"
#include "hbxl_code_domains.h"
#include "sdk_threads.h"
#include "Memory.h"
#include <cstdio>
static_assert(HostMemoryMap::MainSize==PS2_HBXL_VM_DATA_BYTES);
static_assert(HostMemoryMap::CodeSize==PS2_HBXL_VM_CODE_BYTES);
#define CHECK(x) do{if(!(x))ps2_hbxl_vm_fatal(#x,__LINE__);}while(0)
void ps2_hbxl_vm_memory_initialize(){
    ps2_hbxl_vm_stage("sdk-data-bind-before");CHECK(!ps2_hbxl_data_initialize());
    const auto *config=ps2_hbxl_code_domains_default_config();CHECK(config && config->base.bytes==HostMemoryMap::CodeSize);
    ps2_hbxl_vm_stage("sdk-code-bind-before");CHECK(!ps2_hbxl_code_domains_initialize(config));
    ps2_hbxl_vm_stage("memory-bind-complete");
}
void ps2_hbxl_vm_memory_shutdown(){
    Ps2HbxlDataStatistics data{};Ps2HbxlCodeDomainsStatistics domains{};Ps2SdkThreadStatistics threads{};
    ps2_hbxl_data_statistics(&data);ps2_hbxl_code_domains_statistics(&domains);const auto&code=domains.code;ps2_sdk_threads_statistics(&threads);
    CHECK(data.ready && !data.quarantined && !data.active_heap_blocks && !data.active_regions && !data.live_bytes && !data.committed_pages);
    // Before SysMemory claims Main, it is still the launcher's allocation.
    // Its immutable prepared receipt must remain unclaimed for host cleanup.
    CHECK(data.allocations==data.releases && data.allocations<=1 && code.initialized && !code.poisoned && !code.live_pages && !code.mapped_pages && code.maps==code.unmaps);
    CHECK(threads.created==threads.joined && !threads.detached);
    CHECK(domains.gs_joined && (domains.gs_owner || !domains.gs_publications) && !domains.last_domain_error);
    FILE *domains_file=std::fopen("hbxl-vm-domains.json","wb");CHECK(domains_file);
    std::fprintf(domains_file,"{\"productionDomains\":true,\"gsJoined\":true,\"eePublications\":%llu,\"gsPublications\":%llu,\"writerWaits\":%llu,\"lastDomainError\":%u}\n",(unsigned long long)domains.ee_publications,(unsigned long long)domains.gs_publications,(unsigned long long)domains.writer_waits,domains.last_domain_error);CHECK(!std::fclose(domains_file));
    FILE *f=std::fopen("hbxl-vm-runtime.json","wb");CHECK(f);
    std::fprintf(f,"{\"passed\":true,\"sdkDataFingerprintsVerified\":%s,\"dataBytes\":%llu,\"codeBytes\":%llu,\"dataAllocations\":%llu,\"dataReleases\":%llu,\"codeMaps\":%llu,\"codeUnmaps\":%llu,\"codePublications\":%llu,\"codePeakReservedPages\":%u,\"createdThreads\":%llu,\"joinedThreads\":%llu,\"productionGuestMemoryBridge\":true,\"noAvMode\":false}\n",
        data.sdk_fingerprints_verified?"true":"false",(unsigned long long)HostMemoryMap::MainSize,(unsigned long long)code.bytes,
        (unsigned long long)data.allocations,(unsigned long long)data.releases,(unsigned long long)code.maps,(unsigned long long)code.unmaps,
        (unsigned long long)code.publications,code.peak_pages,(unsigned long long)threads.created,(unsigned long long)threads.joined);
    CHECK(!std::fclose(f));ps2_hbxl_vm_stage("code-shutdown-before");CHECK(!ps2_hbxl_code_domains_shutdown());
    ps2_hbxl_vm_stage("data-shutdown-before");CHECK(!ps2_hbxl_data_shutdown());ps2_hbxl_vm_stage("memory-shutdown-complete");
}
