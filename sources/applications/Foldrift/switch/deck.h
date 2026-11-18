#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAILBOX 0x7f8000u
#define ROWS 12u
#define WIDTH 96u
#define DECK_MAGIC 0x495a4644u

enum { Browse, Actions, Confirm, Preview, Keyboard, Copying };
enum {
    A=1, B=2, X=4, Y=8, L=64, R=128, Plus=1024,
    Left=4096, Up=8192, Right=16384, Down=32768, TouchRow=1u<<30
};

typedef struct {
    uint32_t folder;
    char name[60], size[16];
} Row;

typedef struct {
    char path[128];
    uint32_t count, first, shown, selected;
    Row rows[ROWS];
} PaneView;

typedef struct {
    uint32_t seq, keys, touch;
    uint8_t request_padding[52];
    uint32_t magic, ack, mode, clipboard, index, count, progress, error, caps, whole_sd;
    uint8_t status_padding[24];
    char status[128], title[128], input[128];
    char lines[ROWS][WIDTH];
    PaneView pane;
} Mailbox;

_Static_assert(sizeof(Row)==80 && sizeof(PaneView)==1104, "row ABI");
_Static_assert(offsetof(Mailbox,magic)==64 && offsetof(Mailbox,status)==128 &&
               offsetof(Mailbox,lines)==512 && offsetof(Mailbox,pane)==1664,
               "mailbox ABI");
