// SPDX-License-Identifier: GPL-2.0-only
#include "control.h"
#include "../../common/control.h"

namespace {

Service g_hen_control = {};
bool g_hen_control_ready = false;

} // namespace

Result roControlAdopt(Handle copied_session, u64* out_hen_pid) {
    if (g_hen_control_ready) {
        svcCloseHandle(copied_session);
        return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    }

    Service incoming = {};
    serviceCreate(&incoming, copied_session);
    HenStatus status = {};
    Result rc = henGetStatus(&incoming, &status);
    if (R_FAILED(rc)) {
        svcCloseHandle(copied_session);
        return rc;
    }

    g_hen_control = incoming;
    g_hen_control_ready = true;
    *out_hen_pid = status.process_id;
    return 0;
}

Result roControlCreateHomebrewSession(Handle process, Handle* out_client) {
    if (!g_hen_control_ready) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }
    return henCreateHomebrewSession(&g_hen_control, process, out_client);
}
