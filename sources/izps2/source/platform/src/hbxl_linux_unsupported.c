/* Horizon does not implement the Linux process/signal-return ABI. Keep musl's
 * raw svc #0 assembly out of this native image, and reject desktop process
 * creation explicitly. Private pthread clone remains provided by HBXL. */
#include "hbxl.h"
#include <errno.h>
#include <spawn.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
pid_t fork(void){errno=ENOSYS;return -1;}
pid_t vfork(void){errno=ENOSYS;return -1;}
int posix_spawn(pid_t *restrict pid,const char *restrict file,
    const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *restrict attributes,
    char *const argv[restrict],char *const envp[restrict]){
    (void)pid;(void)file;(void)actions;(void)attributes;(void)argv;(void)envp;
    return ENOSYS;
}
int posix_spawnp(pid_t *restrict pid,const char *restrict file,
    const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *restrict attributes,
    char *const argv[restrict],char *const envp[restrict]){
    return posix_spawn(pid,file,actions,attributes,argv,envp);
}
/* No Linux handler is installed by HBXL's signal compatibility. If a retained
 * restorer pointer is nevertheless entered, preserve its owner and report the
 * unsupported transition instead of issuing Linux rt_sigreturn to Horizon. */
__attribute__((noreturn)) void __restore_rt(void){
    hbxl_log("hbxl: Linux signal return is unsupported; native ownership retained");
    for(;;)hbxl_svc_sleep(10000000);
}
void __restore(void) __attribute__((alias("__restore_rt"),noreturn));
