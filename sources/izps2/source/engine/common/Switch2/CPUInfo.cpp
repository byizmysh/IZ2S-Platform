#include "common/HostSys.h"
#include "native-system.h"

const CPUInfo& GetCPUInfo()
{
    static const CPUInfo info = [] {
        CPUInfo value{};
        value.name = "Nintendo Switch 2 / AArch64";
        value.num_threads = iz_available_cpu_count();
        // The process core mask tells us which CPUs this application may use.
        // It does not expose physical package/cluster topology or frequencies.
        // Keep that unavailable topology zero instead of querying cpuinfo.
        return value;
    }();
    return info;
}
