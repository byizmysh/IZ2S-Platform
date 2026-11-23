/* Horizon bring-up candidate: use the same verified kernel tick/context clock
 * as HBXL, and bounded polling instead of unqualified EL0 CNTVCT/CNTFRQ/WFE.
 * The R13 worker failure is localized to Threading/WorkSema; the exact faulting
 * instruction has not been captured, so this is not a claimed root-cause fix. */
extern "C" {
#include "hbxl.h"
#include "hbx_sdk.h"
}
#include <atomic>
#include <cstdint>
#include <limits>
static uint64_t frequency(){
    if(!hbxl_ctx || !hbxl_ctx->tick_hz || hbxl_ctx->tick_hz>=1000000000ull){
        hbxl_log("hbxl: missing qualified tick frequency; native ownership retained");
        for(;;)hbxl_svc_sleep(10000000);
    }
    return hbxl_ctx->tick_hz;
}
extern "C" uint64_t __wrap__Z11GetCPUTicksv(){return hbxl_svc_tick();}
extern "C" uint64_t __wrap__Z16GetTickFrequencyv(){return frequency();}
extern "C" uint32_t __wrap__Z11ShortSpinOnRKSt6atomicIiEi(const std::atomic<int32_t>&word,int32_t expected){
    const uint64_t hz=frequency(),start=hbxl_svc_tick();
    for(unsigned i=0;i<64 && word.load(std::memory_order_acquire)==expected;i++){
        __asm__ volatile("yield":::"memory");
    }
    const uint64_t elapsed=hbxl_svc_tick()-start,seconds=elapsed/hz;
    if(seconds>=5)return std::numeric_limits<uint32_t>::max();
    const uint64_t ns=seconds*1000000000ull+(elapsed%hz)*1000000000ull/hz;
    return uint32_t(ns>UINT32_MAX?UINT32_MAX:ns?ns:1);
}
extern "C" void __wrap__ZN9Threading10SleepUntilEm(uint64_t target){
    const uint64_t hz=frequency();
    for(;;){
        const uint64_t now=hbxl_svc_tick();if(now>=target)return;
        const uint64_t delta=target-now;
        uint64_t ns=delta>=hz?50000000ull:delta*1000000000ull/hz;
        if(ns>50000000ull)ns=50000000ull;
        hbxl_svc_sleep(ns?int64_t(ns):1);
    }
}
