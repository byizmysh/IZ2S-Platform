#pragma once
#include <stdint.h>
#define IZ_INCOMING_DIRECTORY "/iz2s/incoming"
/* Imports IZM ZIPs, standalone IZAPP files, and SD bundles at the ZIP root
 * or under iz2s/modules/<id>.
 * Recognized old images can be upgraded in place with the same adapter.
 * The source archive and existing application data are preserved. */
uint32_t iz_import_package(const char *source,const char *root,const char *pad,char id[48]);
/* The PC queue is a one-shot selection and supports updates of any admitted
 * version of an installed ID, while retaining the module directory. */
uint32_t iz_import_pc_package(const char *source,const char *root,const char *pad,char id[48],int *updated);
#define IZ_IMPORT_PC_UPDATED 0x495a0002u
typedef enum {
    IZ_IMPORT_EMPTY,
    IZ_IMPORT_CURRENT,
    IZ_IMPORT_ADDED,
    IZ_IMPORT_FAILED,
    IZ_IMPORT_PARTIAL,
    IZ_IMPORT_UPDATED,
} IzImportResult;
/* Explicit Add App restores missing tiles and updates the ID named by a
 * prepared inbox folder. Automatic scans do not restore missing tiles. */
IzImportResult iz_import_folder(const char *incoming,const char *root,const char *pad,int explicit_add);
IzImportResult iz_import_incoming(int explicit_add);
