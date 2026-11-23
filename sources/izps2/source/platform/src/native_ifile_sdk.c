#include "native_ifile.h"
#include <string.h>
int ps2_ifile_sdk_initialize(Ps2IFileMount*m,const Ps2IFileMountDescription*c,uintptr_t sdk,size_t bytes){
    /* The nn::os wrapper at 0x13f330 takes (handle, message, bytes).
     * Ps2IFileSend takes (message, bytes, handle), matching the raw SVC22 leaf
     * already used by PS1 and our qualified RO loader. It also avoids reading
     * the host's TLS after HBXL has installed musl's TLS. */
    static const unsigned char send_cut[]={0x41,0x04,0x00,0xd4,0xc0,0x03,0x5f,0xd6};
    const uintptr_t send_offset=0x15845c;
    if(!sdk||(sdk&4095)||bytes!=6164480||sdk>UINTPTR_MAX-bytes||
       memcmp((const void*)(sdk+send_offset),send_cut,sizeof(send_cut)))return PS2_IFILE_INVALID;
    return ps2_ifile_initialize(m,c,(Ps2IFileSend)(sdk+send_offset));
}
