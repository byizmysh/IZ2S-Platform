/* SPDX-License-Identifier: GPL-2.0-only
 * Requests built by libnx's own CMIF encoder, as libnx ldrRo* sends them
 * (nx/source/services/ro.c _rosrvInitialize/_rosrvLoadNro/...: plain
 * Request, PID, payload starting with a PID placeholder), must decode.
 * The kernel's PID and copied-handle translation is simulated.
 */
#include <switch/sf/cmif.h>

#include "../source/wire.h"

#include <assert.h>
#include <string.h>

static uint32_t g_words[64];

static void* build(uint32_t command, const void* payload, uint32_t size, uint32_t handles) {
    memset(g_words, 0, sizeof g_words);
    CmifRequest req = cmifMakeRequest(g_words, (CmifRequestFormat){
                                                   .request_id = command,
                                                   .data_size = size,
                                                   .num_handles = handles,
                                                   .send_pid = 1,
                                               });
    memcpy(req.data, payload, size);
    /* Kernel: the PID slot (after the special header) and the copied
     * handle are rewritten for the server. */
    g_words[3] = 213;
    g_words[4] = 0;
    if (handles)
        req.hipc.copy_handles[0] = 0x42;
    return g_words;
}

int main(void) {
    RoRequest out;

    const uint64_t pid_placeholder = 0;
    build(RoCmd_Initialize, &pid_placeholder, sizeof pid_placeholder, 1);
    assert(g_words[0] == 4); /* libnx: Request, not RequestWithContext */
    assert(roParseRequest(g_words, &out));
    assert(out.cmd_id == RoCmd_Initialize && out.pid == 213 && out.process == 0x42 &&
           out.token == 0);

    const uint64_t load[5] = {0, 0x80000000, 0x5000, 0x80005000, 0x2000};
    build(RoCmd_LoadNro, load, sizeof load, 0);
    assert(roParseRequest(g_words, &out));
    assert(out.cmd_id == RoCmd_LoadNro && out.process == 0);
    assert(out.args[0] == 0x80000000 && out.args[1] == 0x5000 && out.args[2] == 0x80005000 &&
           out.args[3] == 0x2000);

    const uint64_t unload[2] = {0, 0x80000000};
    build(RoCmd_UnloadNro, unload, sizeof unload, 0);
    assert(roParseRequest(g_words, &out) && out.args[0] == 0x80000000);

    const uint64_t nrr[3] = {0, 0x90000000, 0x1000};
    build(RoCmd_LoadNrr, nrr, sizeof nrr, 0);
    assert(roParseRequest(g_words, &out) && out.args[0] == 0x90000000 && out.args[1] == 0x1000);
    build(RoCmd_UnloadNrr, unload, sizeof unload, 0);
    assert(roParseRequest(g_words, &out) && out.cmd_id == RoCmd_UnloadNrr);

    /* Short payload: rejected. */
    build(RoCmd_LoadNro, load, 16, 0);
    assert(!roParseRequest(g_words, &out));
    /* Wrong handle count for Initialize: rejected. */
    build(RoCmd_Initialize, &pid_placeholder, sizeof pid_placeholder, 0);
    assert(!roParseRequest(g_words, &out));
    return 0;
}
