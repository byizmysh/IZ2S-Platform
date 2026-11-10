#pragma once
#include <stdint.h>
// Raw CIV batches have all source and actions locally. Network loss does not
// stop them; every line still has a deadline and USB/reset/physical STOP checks.
// Generic interactive jobs retain the server-contact timeout.
inline bool stopForContact(bool rawBatch, uint32_t gap, uint32_t lineAge) {
  return rawBatch ? lineAge > 120000u : gap > 15000u;
}
