#ifndef PS2_SESSION_FRONTEND_H
#define PS2_SESSION_FRONTEND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* In-process application controls. This API performs no device, loader or
 * transport operation. The application CPU loop acknowledges each request. */
enum Ps2SessionOperation { PS2_SESSION_PAUSE, PS2_SESSION_RESUME, PS2_SESSION_STOP,
                           PS2_SESSION_SAVE, PS2_SESSION_LOAD };
typedef struct {
    uint64_t revision, request_id, queued, executing;
    uint32_t state, outcome, reason, unresolved;
} Ps2SessionStatus;
uint64_t ps2_session_request(unsigned operation, uint64_t expected_revision,
                             const char* checkpoint);
int ps2_session_status(uint64_t request_id, Ps2SessionStatus*);
uint64_t ps2_session_presented_frames(void);
#ifdef __cplusplus
}
#endif
#endif
