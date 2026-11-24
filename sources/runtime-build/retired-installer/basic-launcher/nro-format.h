#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Structural validation before passing a local file to hbloader. */
bool iz_nro_header(const uint8_t *header, size_t available, uint64_t file_bytes);
bool iz_nro_suffix(const char *name);
bool iz_nro_arguments(const char *path, char *arguments, size_t capacity);
