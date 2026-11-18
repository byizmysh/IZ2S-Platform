#ifndef IZ_MODULE_FOLDER_H
#define IZ_MODULE_FOLDER_H
#include <string.h>
/* Directory branding is separate from the signed executable identity. */
static inline const char *iz_module_folder(const char *id) {
    return !strcmp(id,"iz-file-deck") ? "foldrift" : id;
}
static inline const char *iz_module_identity(const char *folder) {
    return !strcmp(folder,"foldrift") ? "iz-file-deck" : folder;
}
static inline int iz_module_matches(const char *id,const char *folder) {
    return !strcmp(id,iz_module_identity(folder));
}
#endif
