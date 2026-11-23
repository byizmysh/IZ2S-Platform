// Serial native GS diagnostic: reuse existing cumulative stage counters and
// the already measured creation duration, with no new clock or SDK calls.
struct NativeTfxStallStages {
    u64 sources{},sourceNs{},compiles{},compileNs{},moduleNs{},pipelines{},pipelineNs{};
};
static NativeTfxStallStages NativeTfxStages(){
    return {GSCompileStats::Get(GSCompileStats::ShaderSources),GSCompileStats::Get(GSCompileStats::ShaderSourceNs),
        GSCompileStats::Get(GSCompileStats::SpirvCompiles),GSCompileStats::Get(GSCompileStats::SpirvCompileNs),
        GSCompileStats::Get(GSCompileStats::ModuleCreateNs),GSCompileStats::Get(GSCompileStats::PipelineCreates),
        GSCompileStats::Get(GSCompileStats::PipelineCreateNs)};
}
static unsigned s_native_tfx_stall_records;
static void NativeTfxPrintStall(double elapsedMs,const NativeTfxStallStages&a,const void*key){
    if(elapsedMs<20.0||s_native_tfx_stall_records>=32)return;
    ++s_native_tfx_stall_records;const auto b=NativeTfxStages();
    const bool valid=b.sources>=a.sources&&b.sourceNs>=a.sourceNs&&b.compiles>=a.compiles&&
        b.compileNs>=a.compileNs&&b.moduleNs>=a.moduleNs&&b.pipelines>=a.pipelines&&b.pipelineNs>=a.pipelineNs;
    const auto delta=[valid](u64 x,u64 y){return (unsigned long long)(valid?y-x:0);};
    u32 words[8];std::memcpy(words,key,sizeof(words));
    Console.WriteLn("PS2_NATIVE_TFX_STALL diagnosticOnly=1 ownershipAuthority=0 countersAllThreads=1 qualified=%u elapsedMs=%.3f sources=%llu sourceNs=%llu spirvCompiles=%llu spirvCompileNs=%llu moduleNs=%llu pipelines=%llu pipelineNs=%llu key=%08x%08x%08x%08x%08x%08x%08x%08x record=%u cap=32",
        unsigned(valid),elapsedMs,delta(a.sources,b.sources),delta(a.sourceNs,b.sourceNs),delta(a.compiles,b.compiles),
        delta(a.compileNs,b.compileNs),delta(a.moduleNs,b.moduleNs),delta(a.pipelines,b.pipelines),
        delta(a.pipelineNs,b.pipelineNs),words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],s_native_tfx_stall_records);
}
