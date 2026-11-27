/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */
#ifndef FTP_SRV_H
#define FTP_SRV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

enum FTP_API_LOG_TYPE {
    FTP_API_LOG_TYPE_COMMAND,
    FTP_API_LOG_TYPE_RESPONSE,
    FTP_API_LOG_TYPE_ERROR,
};

enum FTP_API_LOOP_ERROR {
    FTP_API_LOOP_ERROR_OK,
    FTP_API_LOOP_ERROR_INIT, // call ftpsrv_exit and ftpsrv_init again
};

typedef void (*FtpSrvLogCallback)(enum FTP_API_LOG_TYPE, const char*);
typedef void (*FtpSrvProgressCallback)(void);

struct FtpSrvCustomCommand {
    char name[5];
    int (*func)(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len);
    void* userdata;
    bool auth_required;
    bool args_required;
};

struct FtpSrvConfig {
    char user[128];
    char pass[128];
    unsigned port;
    uint32_t bind_ipv4;
    uint32_t peer_ipv4;
    bool (*peer_allowed)(uint32_t ipv4);
    bool (*alternate_password)(uint32_t peer_ipv4, const char *password);

    // if set, anon access is allowed.
    bool anon;
    // if set, file uploads are not allowed.
    bool read_only;
    // if set, an account is required for storing files.
    bool write_account_required;
    // if set, it will use localtime over gmtime.
    bool use_localtime;
    // if set, sessions will be closed once this is elapsed.
    unsigned timeout;
    unsigned max_sessions; // 0 uses the compiled limit

    const struct FtpSrvCustomCommand* custom_command;
    unsigned custom_command_count;

    FtpSrvLogCallback log_callback;
    FtpSrvProgressCallback progress_callback;
};

int ftpsrv_init(const struct FtpSrvConfig* cfg);
struct FtpSrvStats { uint64_t sent,received; unsigned clients; bool transferring; };
/* Call from the worker that owns ftpsrv_loop. */
void ftpsrv_stats(struct FtpSrvStats *stats);
int ftpsrv_loop(int timeout_ms);
void ftpsrv_exit(void);

#ifdef __cplusplus
}
#endif

#endif // FTP_SRV_H
