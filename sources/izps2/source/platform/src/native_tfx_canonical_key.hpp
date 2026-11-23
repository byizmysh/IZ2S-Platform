#pragma once
// CreateTFXPipeline declares line width and blend constants dynamic. Their
// current values and the frontend's constant-enable bookkeeping do not occur
// in its Vulkan create description or shader source. Preserve every other bit.
template<class Pipeline>
static inline Pipeline NativeTfxCanonicalKey(Pipeline key){
    key.line_width=0;
    key.bs.constant=0;
    key.bs.constant_enable=false;
    return key;
}
