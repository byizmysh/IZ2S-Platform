// Included only by the EE session owner. All shared PUB fields use atomic
// reads. Concurrent or regressed counters invalidate deltas without waiting.
static Ps2SlowBatch::State ps2_slow_batch_state{};
static Ps2SlowBatch::Sample ps2_slow_batch_sample(uint64_t id,uint64_t tick,uint64_t hz){
    Ps2SlowBatch::Sample a{};a.id=id;a.tick=tick;a.hz=hz;
    a.pc=cpuRegs.pc;a.cycles=cpuRegs.cycle;
    a.frames=__atomic_load_n(&ps2_hbxl_ee_wait_tally.word[12],__ATOMIC_ACQUIRE);
    const auto* c=hbxl_ctx;
    a.clockQualified=c&&c->magic==HBX_ABI_MAGIC&&c->abi_version==3&&c->size>=sizeof(*c)&&
        c->flags==0&&hz&&hz<=1000000000ull&&c->tick_hz==hz;
    const auto& p=ps2_hbxl_pub_timing;
    const auto load=[](const uint64_t& v){return __atomic_load_n(&v,__ATOMIC_ACQUIRE);};
    const uint64_t epoch=load(p.epoch),writers=load(p.writers);
    a.publishCalls=load(p.slot[PS2_PUB_RANGE].calls);a.publishTicks=load(p.slot[PS2_PUB_RANGE].total_ticks);
    a.mapCalls=load(p.slot[PS2_PUB_RO_MAP].calls);a.mapTicks=load(p.slot[PS2_PUB_RO_MAP].total_ticks);
    a.unmapCalls=load(p.slot[PS2_PUB_RO_UNMAP].calls);a.unmapTicks=load(p.slot[PS2_PUB_RO_UNMAP].total_ticks);
    a.bytes=load(p.publish_requested_bytes);a.epoch=load(p.epoch);
    a.countersStable=!writers&&!load(p.writers)&&epoch==a.epoch&&
        load(p.magic)==PS2_PUB_TIMING_MAGIC&&load(p.version)==PS2_PUB_TIMING_VERSION&&
        load(p.bytes)==PS2_PUB_TIMING_BYTES&&load(p.slots)==PS2_PUB_TIMING_SLOTS&&load(p.tick_hz)==hz;
    return a;
}
static void ps2_slow_batch_print(const char* kind,const Ps2SlowBatch::Record& r){
    const auto& a=r.begin;const auto& b=r.end;const bool q=r.counterDeltasQualified;
    const auto delta=[q](uint64_t x,uint64_t y){return (unsigned long long)(q?y-x:0);};
    std::fprintf(stderr,"PS2_EE_SLOW_BATCH kind=%s diagnosticOnly=1 ownershipAuthority=0 pureCPU=0 publicationAllThreads=1 batch=%llu tickBegin=%llu tickEnd=%llu tickHz=%llu elapsedTicks=%llu framesBegin=%llu framesEnd=%llu pcBegin=%08x pcEnd=%08x cyclesBegin=%llu cyclesEnd=%llu publicationDeltasQualified=%u publishCalls=%llu publishTicks=%llu mapCalls=%llu mapTicks=%llu unmapCalls=%llu unmapTicks=%llu publishBytes=%llu\n",
        kind,(unsigned long long)a.id,(unsigned long long)a.tick,(unsigned long long)b.tick,
        (unsigned long long)a.hz,(unsigned long long)r.ticks,(unsigned long long)a.frames,
        (unsigned long long)b.frames,a.pc,b.pc,(unsigned long long)a.cycles,(unsigned long long)b.cycles,
        unsigned(q),delta(a.publishCalls,b.publishCalls),delta(a.publishTicks,b.publishTicks),
        delta(a.mapCalls,b.mapCalls),delta(a.mapTicks,b.mapTicks),delta(a.unmapCalls,b.unmapCalls),
        delta(a.unmapTicks,b.unmapTicks),delta(a.bytes,b.bytes));
}
static void ps2_slow_batch_record(uint64_t id,unsigned phase,uint64_t tick,uint64_t hz){
    const auto a=ps2_slow_batch_sample(id,tick,hz);
    if(phase==0){Ps2SlowBatch::begin(ps2_slow_batch_state,a);return;}
    if(phase!=1)return;
    Ps2SlowBatch::Record r{};
    if(Ps2SlowBatch::end(ps2_slow_batch_state,a,r))ps2_slow_batch_print("first32-over40ms",r);
}
static void ps2_slow_batch_summary(){
    const auto& s=ps2_slow_batch_state;
    std::fprintf(stderr,"PS2_EE_SLOW_BATCH_SUMMARY diagnosticOnly=1 ownershipAuthority=0 starts=%llu completed=%llu slowOver40ms=%llu printed=%u cap=32 mismatch=%llu clockRejected=%llu regressions=%llu pending=%u\n",
        (unsigned long long)s.starts,(unsigned long long)s.completed,(unsigned long long)s.slow,s.printed,
        (unsigned long long)s.mismatch,(unsigned long long)s.clockRejected,(unsigned long long)s.regressions,unsigned(s.active));
    if(s.completed)ps2_slow_batch_print("maximum",s.maximum);
}
