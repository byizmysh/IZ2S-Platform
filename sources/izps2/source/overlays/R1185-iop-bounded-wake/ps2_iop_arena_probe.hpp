#pragma once

#include <cstddef>

struct Ps2Sb4Worker;
struct Sb4JitCacheApi;

// True covers success and clean refusal. False leaves the outstanding native
// objects owned by the module's retained-session path.
bool iz_probe_iop_arena(Ps2Sb4Worker& worker, const Sb4JitCacheApi* api, std::size_t bytes);
