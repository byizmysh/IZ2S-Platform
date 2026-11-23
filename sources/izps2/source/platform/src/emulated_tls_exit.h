#ifndef PS2_EMULATED_TLS_EXIT_H
#define PS2_EMULATED_TLS_EXIT_H
#include "emulated_tls.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Alternate, explicitly selected runtime profile. Run the calling thread's
 * TLS destructors before global/atexit destruction, but keep its storage and
 * SDK slot alive until ps2_tls_detach_current. The call is idempotent; 1 means
 * uninitialized runtime or recursive invocation from a TLS destructor.
 * Existing POD TLS (including exception bookkeeping) stays usable in global
 * destructors. Already destroyed C++ TLS objects must not be accessed again.
 * Registration of new nontrivial TLS destructors after this phase returns -1.
 * Keep all module code/metadata resident through final detach and shutdown.
 * The default, previously hardware-tested emulated_tls.c is unchanged. */
int ps2_tls_run_current_destructors(void);
#ifdef __cplusplus
}
#endif
#endif
