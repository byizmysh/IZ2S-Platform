#ifndef IZ2_MENU_LIFECYCLE_H
#define IZ2_MENU_LIFECYCLE_H

/* Transition-only diagnostics. No background thread and no per-frame file I/O. */
#include <malloc.h>
#define IZ2_LIFECYCLE_LOG "sdmc:/iz2s/launcher-lifecycle.log"

static FILE *iz2_lifecycle_open(void)
{
    struct stat st;
    if (!stat(IZ2_LIFECYCLE_LOG, &st) && st.st_size > 65536) {
        if (unlink(IZ2_LIFECYCLE_LOG ".previous") && errno != ENOENT) return NULL;
        if (rename(IZ2_LIFECYCLE_LOG, IZ2_LIFECYCLE_LOG ".previous")) return NULL;
    }
    return fopen(IZ2_LIFECYCLE_LOG, "a");
}

void iz2_split_trace(const char *phase, const char *app, unsigned panes, u32 result, u64 frames)
{
    FILE *f = iz2_lifecycle_open();
    if (!f) return;
    fprintf(f, "build=IZ2S-Split-Close-Focus tick=%llu phase=%s app=%.48s panes=%u rc=%08x frames=%llu\n",
            (unsigned long long)armGetSystemTick(), phase, app, panes, result,
            (unsigned long long)frames);
    fclose(f);
}

void iz2_lifecycle_trace(const IzContext *c,const char *phase,u32 result)
{
    static const char *names[]={"menu-enter","launch-begin","stop-request",
        "native-stop-enter","native-stop-return","restore-menu","menu-restored","menu-exit"};
    static u32 seen, previous[8];
    unsigned index=0;
    while(index<8&&strcmp(phase,names[index]))++index;
    if(index==8)return;
    if(index<2)seen=0;
    if((seen&(1u<<index))&&previous[index]==result)return;
    seen|=1u<<index;previous[index]=result;
    if(!c)return;
    FILE *f=iz2_lifecycle_open();if(!f)return;
    u64 used=0,limit=0;
    Result ur=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    Result lr=svcGetInfo(&limit,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    struct mallinfo heap=mallinfo();
    const IzNative *n=&c->owner.native;
    fprintf(f,"build=RUN32-Lifecycle-Audit-1 tick=%llu phase=%s rc=%08x app=%.48s active=%u native_rc=%08x poisoned=%u worker=%u worker_handle=%llu presentation=%u image_busy=%u image_async=%u image_token=%llu process_used=%llu process_limit=%llu memory_rc=%08x/%08x heap_free=%llu\n",
        (unsigned long long)armGetSystemTick(),phase,result,c->last_id,c->active,
        c->native_status,c->poisoned,n->worker_state,(unsigned long long)n->worker_thread,
        n->presentation_owned,n->image_busy,n->image_async,(unsigned long long)n->image_token,
        (unsigned long long)used,(unsigned long long)limit,ur,lr,(unsigned long long)(size_t)heap.fordblks);
    /* Close flushes each transition before the following potentially failing call. */
    fclose(f);
}
#endif
