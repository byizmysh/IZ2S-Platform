#ifndef IZ2S_MENU_VERSIONS_H
#define IZ2S_MENU_VERSIONS_H

/* Imported from the installed .iza header or the selected NRO's NACP. */
#define IZ2_MENU_VERSIONS_OFFSET (1024u + 0xf0200u)
#define IZ2_MENU_VERSION_BYTES 16u
#include "menu-official-versions.h"

static bool iz2_menu_valid_version(const char version[16])
{
    size_t size = strnlen(version, 16);
    if (!size || size >= 16) return false;
    for (size_t i = 0; i < size; ++i) {
        unsigned char ch = (unsigned char)version[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '-' || ch == '+' || ch == '_')) return false;
    }
    return true;
}

static bool iz2_menu_sd_version(char version[16], const IzAppHeader *header)
{
    uint8_t digest[32];
    sha256CalculateHash(digest, header, sizeof *header);
    /* Exact shipped header digests need no SD receipt lookup. */
    for (size_t i = 0; i < sizeof iz2_shipped_versions / sizeof iz2_shipped_versions[0]; ++i) {
        if (!memcmp(iz2_shipped_versions[i].header, digest, sizeof digest)) {
            memcpy(version, iz2_shipped_versions[i].version, 16);
            return true;
        }
    }
    char image[128], path[128];
    if (module_files(header->id, image, path)) {
        FILE *file = fopen(path, "rb");
        if (file) {
            IzVersionReceipt receipt;
            bool complete = fread(&receipt, 1, sizeof receipt, file) == sizeof receipt &&
                            fgetc(file) == EOF && !ferror(file);
            if (!fclose(file) && complete && !memcmp(receipt.digest, digest, sizeof digest) &&
                iz2_menu_valid_version(receipt.version)) {
                memcpy(version, receipt.version, 16);
                return true;
            }
        }
    }
    return false;
}

/* Metadata cache is rebuilt by every apps_page scan, never used to authorize launch. */
extern bool iz_ounce_apps_version(u32 token, char version[16]);
static bool iz2_menu_nro_version(char version[16], u32 program)
{
    return iz_ounce_apps_version(program, version) && iz2_menu_valid_version(version);
}

void iz2_menu_versions_clear(u8 *snapshot)
{
    memset(snapshot + IZ2_MENU_VERSIONS_OFFSET, 0, IZ_MAX_APPS*IZ2_MENU_VERSION_BYTES);
}

void iz2_menu_version_write(u8 *snapshot, u32 slot, const IzAppHeader *header, u32 program)
{
    if (slot >= IZ_MAX_APPS) return;
    char bytes[16] = {0};
    bool ok = header ? iz2_menu_sd_version(bytes, header) : iz2_menu_nro_version(bytes, program);
    if (ok) memcpy(snapshot + IZ2_MENU_VERSIONS_OFFSET + slot*IZ2_MENU_VERSION_BYTES, bytes, 16);
}

#endif
