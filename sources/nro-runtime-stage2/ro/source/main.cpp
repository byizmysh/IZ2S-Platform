// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>
#include <switch.h>
#include "handle.hpp"
#include "fs_memory.h"
#include "session.h"

namespace {

constexpr u32 MaxSessions = 16;
constexpr u32 PortCount = 3;
constexpr u64 RegistrationRetryNs = 50000000;

// Native RO registers ldr:ro in service space sets 1 and 2. Its private
// ro:dmnt endpoint belongs to set 1.
struct RoPort {
    const char* name;
    u32 space;
    bool fs_memory; ///< ro:dmnt: FS memory access and diagnostics.
};
constexpr RoPort ServicePorts[PortCount] = {
    {"ldr:ro", 1, false},
    {"ldr:ro", 2, false},
    {"ro:dmnt", 1, true},
};

class RoServer {
  public:
    void run() {
        for (;;) {
            registerPorts();
            serveOnce();
        }
    }

  private:
    void registerPorts() {
        for (u32 i = 0; i < PortCount; i++) {
            if (ports_[i].get() != INVALID_HANDLE) {
                continue;
            }

            Handle port = INVALID_HANDLE;
            Result rc =
                smRegisterServiceInSpace(&port, ServicePorts[i].space,
                                         smEncodeName(ServicePorts[i].name), false, 4);
            if (R_SUCCEEDED(rc)) {
                ports_[i].reset(port);
            }
        }
    }

    void acceptSession(u32 port) {
        Handle session = INVALID_HANDLE;
        Result rc = svcAcceptSession(&session, ports_[port].get());
        if (R_FAILED(rc)) {
            return;
        }

        for (u32 i = 0; i < MaxSessions; i++) {
            if (sessions_[i].get() == INVALID_HANDLE) {
                sessions_[i].reset(session);
                fs_memory_[i] = ServicePorts[port].fs_memory;
                clients_[i].port_index = port;
                roRecordAcceptedSession(port);
                return;
            }
        }
        svcCloseHandle(session);
    }

    void closeSession(u32 slot) {
        // roSessionClose always releases the client, so the slot can serve
        // the next one; RoDiagnostics counts views it could not unmap.
        if (!fs_memory_[slot]) {
            roSessionClose(&clients_[slot]);
        }
        fs_memory_[slot] = false;
        sessions_[slot].reset();
    }

    void serveOnce() {
        Handle handles[PortCount + MaxSessions];
        u32 slots[PortCount + MaxSessions];
        s32 count = 0;

        for (u32 i = 0; i < PortCount; i++) {
            if (ports_[i].get() != INVALID_HANDLE) {
                handles[count] = ports_[i].get();
                slots[count++] = i;
            }
        }
        for (u32 i = 0; i < MaxSessions; i++) {
            if (sessions_[i].get() != INVALID_HANDLE) {
                handles[count] = sessions_[i].get();
                slots[count++] = PortCount + i;
            }
        }
        if (!count) {
            svcSleepThread(RegistrationRetryNs);
            return;
        }

        void* message = armGetTls();
        std::memset(message, 0, 0x100);
        s32 index = -1;
        Result rc = svcReplyAndReceive(&index, handles, count, INVALID_HANDLE, RegistrationRetryNs);
        if (index < 0 || index >= count) {
            return;
        }

        const u32 slot = slots[index];
        if (slot < PortCount) {
            if (R_SUCCEEDED(rc)) {
                acceptSession(slot);
            }
            return;
        }

        const u32 client = slot - PortCount;
        if (R_FAILED(rc)) {
            if (rc == KERNELRESULT(ConnectionClosed)) {
                closeSession(client);
            }
            return;
        }
        if (hipcParseRequest(message).meta.type == CmifCommandType_Close) {
            closeSession(client);
            return;
        }

        Handle copied_client = INVALID_HANDLE;
        if (fs_memory_[client]) {
            roFsMemoryDispatch(message);
        } else {
            copied_client = roDispatch(clients_, MaxSessions, client, message);
        }
        rc = svcReplyAndReceive(&index, nullptr, 0, sessions_[client].get(), 0);
        if (copied_client != INVALID_HANDLE) {
            svcCloseHandle(copied_client);
        }
        if (rc == KERNELRESULT(ConnectionClosed)) {
            closeSession(client);
        }
    }

    ounce::HandleOwner ports_[PortCount];
    ounce::HandleOwner sessions_[MaxSessions];
    RoSession clients_[MaxSessions] = {};
    bool fs_memory_[MaxSessions] = {};
};

RoServer g_server;

} // namespace

// libounce-nx runtime hooks must have C linkage.
extern "C" {
u32 __nx_applet_type = AppletType_None;
size_t __nx_heap_size = 0x200000;

void __appInit(void) {
}

void __appExit(void) {
}
}

int main(int argc, char** argv) {
    NX_IGNORE_ARG(argc);
    NX_IGNORE_ARG(argv);
    Result rc = smInitialize();
    if (R_FAILED(rc)) {
        return static_cast<int>(rc);
    }
    g_server.run();
}
