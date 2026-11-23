#pragma once
#include "code_arena.h"
/* Linux-only model. It is never linked into the console image. */
void ps2_model_open(size_t bytes);
void ps2_model_close();
