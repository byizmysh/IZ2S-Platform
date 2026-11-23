#pragma once

#include <cstddef>
#include <cstdint>

struct Ps2Sb4Worker;
struct Sb4JitCacheApi;
struct MtvuCodeView {
    std::uint8_t* rw = nullptr;
    std::uint8_t* rx = nullptr;
    std::size_t bytes = 0;
};

void iz_mtvu_configure(Ps2Sb4Worker* worker, const Sb4JitCacheApi* api, std::size_t bytes);
bool iz_mtvu_requested();
bool iz_mtvu_open_native(void (*run)(void*), void* context);
bool iz_mtvu_native_is_open();
void iz_mtvu_close_native();
bool iz_is_mtvu_worker();
MtvuCodeView iz_mtvu_code_view();
std::uint32_t iz_mtvu_publish_on_owner(std::uint32_t offset, std::uint32_t bytes);
std::uint8_t* iz_mtvu_redirect_code(std::size_t primary_offset);
void iz_mtvu_publish_packet(std::uint32_t offset, std::uint32_t bytes);
