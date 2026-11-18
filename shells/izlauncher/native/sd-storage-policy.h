#ifndef IZ2S_SD_STORAGE_POLICY_H
#define IZ2S_SD_STORAGE_POLICY_H

#include <stdint.h>
#include <string.h>

/* Detached Ed25519 signature over the Deck ID, capability and exact .iza digest.
 * The digest must come from the bytes the launcher validated and will execute. */
typedef int (*Iz2sEd25519Verify)(const uint8_t public_key[32], const uint8_t signature[64],
                                const uint8_t *message, size_t length);

static inline int iz2sWholeSdApproved(const char *id, const uint8_t digest[32],
                                      const uint8_t public_key[32],
                                      const uint8_t signature[64], Iz2sEd25519Verify verify) {
    static const char domain[] = "IZ2S-SD-CAPABILITY-v1\0iz-file-deck\0whole-sd";
    if (!id || !digest || !public_key || !signature || !verify || strcmp(id,"iz-file-deck")) return 0;
    uint8_t message[sizeof domain + 32];
    memcpy(message,domain,sizeof domain);
    memcpy(message+sizeof domain,digest,32);
    return verify(public_key,signature,message,sizeof message)==1;
}

/* Whole-card paths are relative to the card. Only the empty path names root. */
static inline const char *iz2sWholeSdPath(const char *name, char full[769], int file_only) {
    if (!name || (!*name && file_only)) return NULL;
    size_t i=0, component=0;
    while (i<512) {
        unsigned char ch=(unsigned char)name[i];
        if (!ch || ch=='/') {
            size_t bytes=i-component;
            if ((!bytes && (i || ch)) ||
                (bytes==1 && name[component]=='.') ||
                (bytes==2 && name[component]=='.' && name[component+1]=='.')) return NULL;
            component=i+1;
        } else if (ch<32 || ch==127 || ch=='\\' || ch==':') return NULL;
        if (!ch) break;
        ++i;
    }
    if (i==512) return NULL;
    full[0]='/';
    memcpy(full+1,name,i+1);
    return full+1;
}

#endif
