#pragma once

#include <cstdint>
#include <cstddef>

void eeWriteShadowBegin(bool enable);
void eeWriteShadowEnd();
void eeWriteShadowMarkCandidate(uint32_t offset);
void eeWriteShadowRecordDiscard();
bool eeWriteRoutingActive();
const uintptr_t* eeSoftmemJitBase();
bool eeActivateEagerCode();
bool eeAllowEagerCode();
void eeStopEagerCodeForSettings();
void eeResetCodeWriteTracking();
void recEeObserveCodeSpan(const void* pointer, uint32_t bytes);
void eeObserveRawStore(void* pointer, size_t bytes, const void* source);
