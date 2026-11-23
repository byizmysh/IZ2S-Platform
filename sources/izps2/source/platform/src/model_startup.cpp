#include "code_model.h"
#include "code_runtime.h"
#include <cstdio>
#include <cstdlib>
namespace {
struct ModelLifetime {
    ModelLifetime() { ps2_model_open(64u*1024*1024); }
    ~ModelLifetime() {
        const auto a=ps2_code_arena();
        if(const auto path=std::getenv("PS2_JIT_REPORT")) {
            auto f=std::fopen(path,"w");
            if(!f)std::abort();
            std::fprintf(f,"{\"livePages\":%u,\"peakPages\":%u,\"poisoned\":%u,\"allocations\":%llu,\"releases\":%llu,\"publications\":%llu,\"maps\":%llu,\"unmaps\":%llu,\"consoleExecuted\":false}\n",
                a->live_pages,a->peak_pages,a->poisoned,(unsigned long long)a->allocations,(unsigned long long)a->releases,
                (unsigned long long)a->publications,(unsigned long long)a->maps,(unsigned long long)a->unmaps);
            std::fclose(f);
        }
        ps2_model_close();
    }
} modelLifetime;
}
