#ifndef IZ_NATIVE_RECOVERY_CRYPTO_H
#define IZ_NATIVE_RECOVERY_CRYPTO_H
#include <stdint.h>
#include <stddef.h>
void iz_recovery_sha256(const void *,size_t,unsigned char out[32]);
void iz_recovery_mac(const unsigned char key[32],const unsigned char nonce[32],const unsigned char header[128],const void *,size_t,unsigned char out[32]);
#endif
