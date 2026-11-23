#pragma once
#include "code_arena.h"
/* Install before creating any Play object. Remove after every JIT block is gone. */
void ps2_install_code_arena(Ps2CodeArena *);
Ps2CodeArena *ps2_code_arena();
