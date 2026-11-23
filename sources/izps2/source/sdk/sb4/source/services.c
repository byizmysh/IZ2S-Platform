/* Qualified IZlauncher SDK callback contracts implemented with libounce.
 * No SmileBASIC address or byte signature is manufactured by this backend. */
#include <switch.h>
#include <switch/services/bsd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sb4/services.h>
#include "hid.h"

#define WRAP(name) static u64 wrap_##name(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h)
#define P(type,value) ((type *)(uintptr_t)(value))
#define BAD MAKERESULT(Module_Libnx, LibnxError_BadInput)
_Static_assert(sizeof(HidNpadCommonState)==40, "SB4 pad state fields");
_Static_assert(offsetof(HidNpadCommonState,attributes)==32, "SB4 pad connection flags");
_Static_assert(sizeof(HidKeyboardState)==48, "SB4 keyboard state");
_Static_assert(sizeof(HidMouseState)==40, "SB4 mouse state");
_Static_assert(sizeof(HidSixAxisSensorState)==96, "SB4 six-axis state");
_Static_assert(sizeof(HidVibrationValue)==16, "SB4 amplitude/frequency envelope");

WRAP(style) {
    if (!a || !b) return BAD;
    *P(u32,a)=hidGetNpadStyleSet((HidNpadIdType)*P(u32,b)); return 0;
}
#define PAD(name,kind) WRAP(name) { \
    if (!a || !b) return BAD; \
    memset(P(void,a),0,sizeof(HidNpadCommonState)); \
    hidGetNpadStates##kind((HidNpadIdType)*P(u32,b),P(HidNpadCommonState,a),1); \
    return 0; }
PAD(padHandheld,Handheld)
PAD(padFullKey,FullKey)
PAD(padJoyDual,JoyDual)
PAD(padJoyLeft,JoyLeft)
PAD(padJoyRight,JoyRight)
WRAP(mouse) {
    if (!a) return BAD;
    memset(P(void,a),0,sizeof(HidMouseState)); hidGetMouseStates(P(HidMouseState,a),1); return 0;
}
WRAP(keyboard) {
    if (!a || !b || !c || c>17) return BAD;
    *P(u32,a)=0; memset(P(void,b),0,c*sizeof(HidKeyboardState));
    *P(u32,a)=(u32)hidGetKeyboardStates(P(HidKeyboardState,b),c); return 0;
}
WRAP(motionHandles) {
    if (!a || !c || !b || b>2) return 0;
    u32 count=d==HidNpadStyleTag_NpadJoyDual && b==2 ? 2:1;
    memset(P(void,a),0,b*sizeof(HidSixAxisSensorHandle));
    Result rc=hidGetSixAxisSensorHandles(P(HidSixAxisSensorHandle,a),count,
        (HidNpadIdType)*P(u32,c),(HidNpadStyleTag)d);
    return R_SUCCEEDED(rc)?count:0;
}
WRAP(motionStart) { return a?hidStartSixAxisSensor(*P(HidSixAxisSensorHandle,a)):BAD; }
WRAP(motionStop) { return a?hidStopSixAxisSensor(*P(HidSixAxisSensorHandle,a)):BAD; }
WRAP(motionRead) {
    if (!a || !b || !d || !c || c>17) return BAD;
    *P(u32,a)=0; memset(P(void,b),0,c*sizeof(HidSixAxisSensorState));
    *P(u32,a)=(u32)hidGetSixAxisSensorStates(*P(HidSixAxisSensorHandle,d),P(HidSixAxisSensorState,b),c); return 0;
}
WRAP(led) { return a&&b?hidGetPlayerLedPattern((HidNpadIdType)*P(u32,b),P(u8,a)):BAD; }
WRAP(vibrationHandles) {
    if (!a || !c || !b || b>2) return 0;
    const bool pair=d==HidNpadStyleTag_NpadFullKey || d==HidNpadStyleTag_NpadHandheld || d==HidNpadStyleTag_NpadJoyDual;
    u32 count=pair && b==2?2:1;
    memset(P(void,a),0,b*sizeof(HidVibrationDeviceHandle));
    /* libounce combines handle enumeration and activation. The subsequent
     * legacy Init callback verifies this actual initialized device. */
    Result rc=hidInitializeVibrationDevices(P(HidVibrationDeviceHandle,a),count,
        (HidNpadIdType)*P(u32,c),(HidNpadStyleTag)d);
    return R_SUCCEEDED(rc)?count:0;
}
WRAP(vibrationInit) {
    HidVibrationDeviceInfo info;
    return a?hidGetVibrationDeviceInfo(*P(HidVibrationDeviceHandle,a),&info):BAD;
}
WRAP(vibrationSend) { return a&&b?hidSendVibrationValue(*P(HidVibrationDeviceHandle,a),P(HidVibrationValue,b)):BAD; }
WRAP(vibrationRead) { return a&&b?hidGetActualVibrationValue(*P(HidVibrationDeviceHandle,b),P(HidVibrationValue,a)):BAD; }
WRAP(vibrationPermitted) { return a?hidIsVibrationPermitted(P(bool,a)):BAD; }
WRAP(serviceTick) { return armGetSystemTick(); }
WRAP(sleep) { if(a>INT64_MAX)return BAD; svcSleepThread((s64)a); return 0; }

typedef struct OwnedThread {
    Thread thread;
    u64 (*entry)(void *);
    void *context;
    u64 result;
    struct OwnedThread *next;
} OwnedThread;
/* Creation/poll/join run at the supervisor's serialized callback boundary. */
static OwnedThread *threads;
static OwnedThread *find_thread(u64 token) {
    for(OwnedThread *t=threads;t;t=t->next)if((u64)(uintptr_t)t==token)return t;
    return NULL;
}
static void thread_entry(void *context) {
    OwnedThread *t=context; t->result=t->entry(t->context);
}
WRAP(create) {
    if(!a || b || !c) return BAD;
    *P(u64,a)=0;
    OwnedThread *t=calloc(1,sizeof(*t)); if(!t)return ENOMEM;
    t->entry=(u64(*)(void *))(uintptr_t)c;t->context=P(void,d);
    /* CPU setup for hosted emulators uses this worker stack. */
    const size_t stack_bytes = 1024u * 1024u;
    Result rc=threadCreate(&t->thread,thread_entry,t,NULL,stack_bytes,0x2c,-2);
    if(R_FAILED(rc)){free(t);return rc;}
    t->next=threads;threads=t;
    rc=threadStart(&t->thread);
    if(R_FAILED(rc)){
        Result closed=threadClose(&t->thread);
        if(R_SUCCEEDED(closed)){threads=t->next;free(t);return rc;}
        /* Publish ownership even on failure: the supervisor must retain it. */
        *P(u64,a)=(u64)(uintptr_t)t; return closed;
    }
    *P(u64,a)=(u64)(uintptr_t)t;return 0;
}
WRAP(tryWait) {
    OwnedThread *t=find_thread(a);if(!t)return BAD;
    s32 index=-1;Result rc=svcWaitSynchronization(&index,&t->thread.handle,1,0);
    return rc==KERNELRESULT(TimedOut)?0:R_SUCCEEDED(rc)?1:rc;
}
WRAP(join) {
    OwnedThread *t=find_thread(a);if(!t || !b)return BAD;
    s32 index=-1;Result rc=svcWaitSynchronization(&index,&t->thread.handle,1,0);
    if(R_FAILED(rc))return rc; /* Never block the UI waiting for a live worker. */
    rc=threadClose(&t->thread);if(R_FAILED(rc))return rc;
    *P(u64,b)=t->result;
    OwnedThread **link=&threads;while(*link!=t)link=&(*link)->next;
    *link=t->next;free(t);return 0;
}

static bool socket_reference;
static int descriptors[32];
static unsigned descriptor_count;
static bool owned_socket(int fd) {
    for(unsigned i=0;i<descriptor_count;++i)if(descriptors[i]==fd)return true;
    return false;
}
WRAP(socketInitialized) {
    if(!socket_reference){Result rc=socketInitializeDefault();if(R_FAILED(rc))return 0;socket_reference=true;}
    return serviceIsActive(bsdGetServiceSession());
}
WRAP(socket) {
    if(!socket_reference || descriptor_count==32){errno=EMFILE;return (u64)-1;}
    int fd=socket((int)a,(int)b,(int)c);
    if(fd>=0)descriptors[descriptor_count++]=fd;
    return (u64)(s64)fd;
}
WRAP(sendto) {
    if(!owned_socket((int)a)){errno=EBADF;return (u64)-1;}
    if(!b || !e || c>1024 || f!=16){errno=EINVAL;return (u64)-1;}
    return (u64)(s64)sendto((int)a,P(void,b),(size_t)c,(int)d,P(struct sockaddr,e),(socklen_t)f);
}
WRAP(recv) {
    if(!owned_socket((int)a)){errno=EBADF;return (u64)-1;}
    if(!b || !c || c>1024){errno=EINVAL;return (u64)-1;}
    return (u64)(s64)recv((int)a,P(void,b),(size_t)c,(int)d);
}
WRAP(socketClose) {
    if(!owned_socket((int)a)){errno=EBADF;return (u64)-1;}
    int rc=close((int)a);
    if(!rc)for(unsigned i=0;i<descriptor_count;++i)if(descriptors[i]==(int)a){descriptors[i]=descriptors[--descriptor_count];break;}
    return (u64)(s64)rc;
}
WRAP(errno) { return errno; }

/* SDK object layouts are adapted, never cast to libounce service sessions. */
_Static_assert(sizeof(HidNpadControllerColor)==8,"Color ABI");
_Static_assert(sizeof(NfcDeviceHandle)==8&&sizeof(NfcTagInfo)==88,"NFC ABI");
static bool nifm_reference,irs_reference,nfc_reference;
static NfcDeviceHandle detections[9];static unsigned detection_count;
static u32 service_failure;
static u32 internet(void){if(nifm_reference)return 0;u32 rc=nifmInitialize(NifmServiceType_User);if(!rc)nifm_reference=true;return rc;}
WRAP(color){return a&&b&&c?hidGetNpadControllerColorSplit((HidNpadIdType)*P(u32,c),P(HidNpadControllerColor,a),P(HidNpadControllerColor,b)):BAD;}
WRAP(netStatus){
 if(!a)return BAD;
 u32 rc=internet();if(rc)return rc;
 NifmInternetConnectionType type;NifmInternetConnectionStatus status;u32 strength;
 rc=nifmGetInternetConnectionStatus(&type,&strength,&status);
 if(!rc){P(u32,a)[0]=(u32)type;P(u32,a)[1]=strength;P(u32,a)[2]=(u32)status;}return rc;
}
WRAP(netAddress){if(!a)return BAD;
 u32 rc=internet();return rc?rc:nifmGetCurrentIpAddress(P(u32,a));}
WRAP(irHandle){
 if(!a||!b)return BAD;
 if(!irs_reference){u32 rc=irsInitialize();if(rc)return rc;irs_reference=true;}
 return irsGetIrCameraHandle(P(IrsIrCameraHandle,a),(HidNpadIdType)*P(u32,b));
}
WRAP(irStatus){return irs_reference&&a&&b?irsGetIrCameraStatus(*P(IrsIrCameraHandle,b),P(IrsIrCameraStatus,a)):BAD;}
WRAP(nfcState){if(!nfc_reference)return NfcState_NonInitialized;NfcState state;u32 rc=nfcGetState(&state);return rc?UINT32_MAX:(u64)state;}
WRAP(nfcInit){if(nfc_reference||service_failure)return BAD;u32 rc=nfcInitialize(NfcServiceType_User);if(!rc)nfc_reference=true;return rc;}
WRAP(nfcFinalize){if(!nfc_reference||detection_count||service_failure)return BAD;nfcExit();nfc_reference=false;return 0;}
WRAP(nfcList){return nfc_reference&&a&&b&&c&&c<=9?nfcListDevices(P(s32,b),P(NfcDeviceHandle,a),(s32)c):BAD;}
WRAP(nfcStart){
 if(!nfc_reference||service_failure||!a||detection_count==9)return BAD;
 for(unsigned i=0;i<detection_count;i++)if(!memcmp(detections+i,P(void,a),8))return BAD;
 /* Retain the device if an IPC completion is uncertain. */
 detections[detection_count++]=*P(NfcDeviceHandle,a);
 u32 rc=nfcStartDetection(P(NfcDeviceHandle,a),(NfcProtocol)UINT32_MAX);if(rc)service_failure=rc;return rc;
}
WRAP(nfcStop){
 if(!nfc_reference||!a||service_failure)return BAD;
 for(unsigned i=0;i<detection_count;i++)if(!memcmp(detections+i,P(void,a),8)){
  u32 rc=nfcStopDetection(P(NfcDeviceHandle,a));if(rc)service_failure=rc;else detections[i]=detections[--detection_count];return rc;
 }return BAD;
}
WRAP(nfcTag){return nfc_reference&&a&&b?nfcGetTagInfo(P(NfcDeviceHandle,b),P(NfcTagInfo,a)):BAD;}
WRAP(nfcPad){return nfc_reference&&a&&b?nfcGetNpadId(P(NfcDeviceHandle,b),P(u32,a)):BAD;}

#define BIND(name,id) {id,0,#name,wrap_##name}
static const IzServiceBinding bindings[]={
    BIND(color,4801904), BIND(netStatus,3739888), BIND(netAddress,3740032),
    BIND(irHandle,4978784), BIND(irStatus,4979680), BIND(nfcState,3621600),
    BIND(nfcInit,3623024), BIND(nfcFinalize,3623200), BIND(nfcList,3624784),
    BIND(nfcStart,3624976), BIND(nfcStop,3625136), BIND(nfcTag,3625280), BIND(nfcPad,3623616),
    BIND(style,4801504), BIND(motionHandles,4850560), BIND(motionStart,4818208),
    BIND(motionStop,4818336), BIND(motionRead,4817696),
    BIND(vibrationHandles,4854688), BIND(vibrationInit,4839648),
    BIND(vibrationSend,4840144), BIND(vibrationRead,4840432), BIND(vibrationPermitted,4839328),
    BIND(mouse,4794464), BIND(keyboard,4793248), BIND(led,4797520),
    BIND(padHandheld,SB4_HID_READ_HANDHELD), BIND(padFullKey,SB4_HID_READ_FULL_KEY), BIND(padJoyDual,SB4_HID_READ_JOY_DUAL),
    BIND(padJoyLeft,SB4_HID_READ_JOY_LEFT), BIND(padJoyRight,SB4_HID_READ_JOY_RIGHT),
    BIND(socketInitialized,1315232), BIND(socket,1314112), BIND(sendto,1313776),
    BIND(recv,1313680), BIND(socketClose,1314192), BIND(errno,1314992),
    BIND(serviceTick,1218112), BIND(create,1255648), BIND(tryWait,1217808),
    BIND(join,1256736), BIND(sleep,1217792),
    {SB4_HID_SET_SUPPORTED_STYLES,0,"setSupportedStyles",sb4HidSetSupportedStyles},
    {SB4_HID_GET_SUPPORTED_STYLES,0,"getSupportedStyles",sb4HidGetSupportedStyles},
    {SB4_HID_SET_SUPPORTED_IDS,0,"setSupportedIds",sb4HidSetSupportedIds},
    {SB4_HID_DISCONNECT_PAD,0,"disconnectPad",sb4HidDisconnectPad},
};
static const IzServiceBridge bridge={1,sizeof(bridge),sizeof(bindings)/sizeof(*bindings),IZ_SERVICE_LIBOUNCE,bindings};
const IzServiceBridge *sb4GetServiceBridge(void){return &bridge;}
uint32_t sb4CloseServices(void){
    if(service_failure)return service_failure;
    if(threads || descriptor_count || detection_count)return EBUSY;
    const uint32_t hid_rc=sb4HidRestoreLauncher();
    if(hid_rc)return hid_rc;
    if(nfc_reference){nfcExit();nfc_reference=false;}
    if(irs_reference){irsExit();irs_reference=false;}
    if(nifm_reference){nifmExit();nifm_reference=false;}
    if(socket_reference){socketExit();socket_reference=false;}
    return 0;
}
