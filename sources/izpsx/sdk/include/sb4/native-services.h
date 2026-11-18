#ifndef SB4_NATIVE_SERVICES_H
#define SB4_NATIVE_SERVICES_H
#include "compat.h"
/* Versioned C imports use libounce 9382b1ec48ac layouts. Resolve every import
 * before starting any work. Providers never contain application logic. */
#define SB4_NATIVE_SERVICES_VERSION 1u
#define SB4_NATIVE_SERVICES_IMPORT "sb4NativeServicesVersion"
#ifdef __cplusplus
extern "C" {
#endif
/* Launcher only. Grants process stdio/SD to an application already authorized
 * by launcher policy. Ordinary module storage remains scoped. One active app. */
const Sb4Compat *sb4GetApplicationIoCompat(const Sb4Compat *base);
#ifdef __cplusplus
}
#endif
#endif
